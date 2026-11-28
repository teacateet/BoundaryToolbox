use crate::context::AppState;
use crate::enhanced_tunnel::quic_over::enhanced_io::enhanced_inbound::{
    QuicDataInbound, create_enhanced_inbound,
};
use crate::enhanced_tunnel::quic_over::enhanced_io::enhanced_outbound::create_enhanced_outbound;
use crate::enhanced_tunnel::quic_over::enhanced_io::socket::ExtendedQuicSocket;
use crate::enhanced_tunnel::quic_over::quic_client::QuicTunnelClient;
use crate::enhanced_tunnel::quic_over::quic_inbound::EnhancedQuicInbound;
use crate::enhanced_tunnel::quic_over::quic_outbound::EnhancedQuicOutbound;
use crate::enhanced_tunnel::quic_over::{quic_client, quic_server};
use crate::nat::SubnetExternalRoute;
use crate::nat::internal_nat::{InternalNatInbound, PortMappingManager};
use crate::port_mapping::PortMapping;
use crate::tls;
use crate::tun::TunDataInbound;
use crate::tunnel_core::outbound::HybridOutbound;
use crate::utils::task_control::TaskGroup;
use anyhow::Context;
use quinn::congestion::BbrConfig;
use quinn::crypto::rustls::QuicServerConfig;
use quinn::{ClientConfig, Endpoint, EndpointConfig, TransportConfig, default_runtime};
use rustls::ServerConfig;
use sha2::{Digest, Sha256};
use std::io;
use std::sync::Arc;
use std::time::Duration;
use tcp_ip::{IpStackConfig, IpStackRecv};

pub(crate) struct QuicTunnelConfig {
    pub mtu: u16,
    pub password: String,
    pub identity: Option<crate::tls::RoomIdentity>,
    pub open_quic_client: bool,
    pub port_mapping: Vec<PortMapping>,
}

pub(crate) struct QuicTunnelComponents {
    pub hybrid_outbound: HybridOutbound,
    pub external_route: SubnetExternalRoute,
    pub internal_nat_manager: Option<InternalNatInbound>,
    pub port_mapping_manager: PortMappingManager,
}

pub(crate) async fn quic_tunnel_start(
    app_state: AppState,
    task_group: TaskGroup,
    tun_data_sender: Option<TunDataInbound>,
    config: QuicTunnelConfig,
    components: QuicTunnelComponents,
) -> anyhow::Result<(EnhancedQuicInbound, Option<EnhancedQuicOutbound>)> {
    let ip_stack_config = IpStackConfig {
        mtu: config.mtu,
        ..Default::default()
    };
    let (ip_stack, ip_socket, quic_outbound) = if let Some(tun_data_sender) = tun_data_sender {
        let (ip_stack, ip_stack_send, ip_stack_recv) = tcp_ip::ip_stack(ip_stack_config)?;
        let ip_socket = tcp_ip::ip::IpSocket::bind_all(None, ip_stack.clone()).await?;
        let ip_socket = Arc::new(ip_socket);
        task_group.spawn(ip_stack_recv_task(
            ip_stack_recv,
            app_state.clone(),
            tun_data_sender,
        ));
        let quic_outbound =
            EnhancedQuicOutbound::new(config.open_quic_client, ip_stack_send, ip_stack.clone());

        (Some(ip_stack), Some(ip_socket), Some(quic_outbound))
    } else {
        (None, None, None)
    };

    let (inbound, endpoint) = create_quic_endpoint(
        config.password,
        config.identity,
        task_group.clone(),
        components.hybrid_outbound,
    )
    .await?;
    quic_server::server_listen(
        &task_group,
        endpoint.clone(),
        ip_socket.clone(),
        ip_stack.clone(),
        components.internal_nat_manager,
        components.port_mapping_manager,
    )
    .await;
    if config.open_quic_client {
        let quic_client =
            QuicTunnelClient::new(app_state.clone(), endpoint, components.external_route);

        // 客户端使用指纹验证
        if let (Some(ip_stack), Some(ip_socket)) = (ip_stack, ip_socket) {
            quic_client::create_client(
                quic_client.clone(),
                task_group.clone(),
                ip_stack.clone(),
                ip_socket,
            )
            .await;
        }
        if !config.port_mapping.is_empty() {
            crate::port_mapping::port_mapping_start(&task_group, config.port_mapping, quic_client)
                .await?;
        }
    } else if !config.port_mapping.is_empty() {
        let quic_client =
            QuicTunnelClient::new(app_state.clone(), endpoint, components.external_route);

        crate::port_mapping::port_mapping_start(&task_group, config.port_mapping, quic_client)
            .await?;
    }

    let quic_inbound = EnhancedQuicInbound::new(inbound);
    Ok((quic_inbound, quic_outbound))
}
pub fn room_tls(password:&str,identity:Option<crate::tls::RoomIdentity>)->anyhow::Result<(ServerConfig,rustls::ClientConfig)> {
    anyhow::ensure!(!password.is_empty(), "需要房间凭据");
    let (member,member_key)=tls::cert::generate_deterministic_cert(password)?;
    let mut roots=rustls::RootCertStore::empty(); roots.add(member.clone())?;
    let check=rustls::server::WebPkiClientVerifier::builder(Arc::new(roots)).build()?;
    let (server_cert,server_key,pin)=match identity {
        Some(tls::RoomIdentity::Host(host))=>{let (cert,key)=host.keys(); (cert,key,host.fingerprint())},
        Some(tls::RoomIdentity::Guest(pin))=>(member.clone(),member_key.clone_key(),pin),
        None => (member.clone(),member_key.clone_key(),Sha256::digest(member.as_ref()).into()),
    };
    let server=ServerConfig::builder().with_client_cert_verifier(check).with_single_cert(vec![server_cert],server_key)?;
    let client=rustls::ClientConfig::builder().dangerous().with_custom_certificate_verifier(
        Arc::new(tls::verifier::FingerprintVerifier::new(pin))).with_client_auth_cert(vec![member],member_key)?;
    Ok((server,client))
}
async fn create_quic_endpoint(
    password: String,
    identity: Option<crate::tls::RoomIdentity>,
    task_group: TaskGroup,
    hybrid_outbound: HybridOutbound,
) -> anyhow::Result<(QuicDataInbound, Endpoint)> {
    let (server_config, client_config) = room_tls(&password,identity)?;

    let outbound = create_enhanced_outbound(task_group.clone(), hybrid_outbound).await;
    let (inbound, inbound_receiver) = create_enhanced_inbound();
    let socket = ExtendedQuicSocket::new(inbound_receiver, outbound);

    let server_crypto = QuicServerConfig::try_from(server_config)
        .map_err(|e| anyhow::anyhow!("QUIC TLS config error: {:?}", e))?;
    let server_config = quinn::ServerConfig::with_crypto(Arc::new(server_crypto));
    // 替换运行时
    let runtime = default_runtime().ok_or_else(|| io::Error::other("no async runtime found"))?;
    let mut client_config = ClientConfig::new(Arc::new(
        quinn::crypto::rustls::QuicClientConfig::try_from(client_config)
            .context("Failed to create QUIC client config")?,
    ));
    client_config.transport_config(build_transport_config());
    let mut endpoint_config = EndpointConfig::default();
    endpoint_config.max_udp_payload_size(1300)?;
    let mut endpoint = quinn::Endpoint::new_with_abstract_socket(
        endpoint_config,
        Some(server_config),
        Arc::new(socket),
        runtime,
    )
    .context("quic server create failed")?;
    endpoint.set_default_client_config(client_config);
    Ok((inbound, endpoint))
}

fn build_transport_config() -> Arc<TransportConfig> {
    let mut transport = TransportConfig::default();
    transport.congestion_controller_factory(Arc::new(BbrConfig::default()));
    transport.keep_alive_interval(Some(Duration::from_secs(5)));

    transport.max_idle_timeout(Some(Duration::from_secs(10).try_into().unwrap()));

    Arc::new(transport)
}

async fn ip_stack_recv_task(
    mut ip_stack_recv: IpStackRecv,
    app_state: AppState,
    tun_data_sender: TunDataInbound,
) {
    let mut buf = vec![0u8; 1500];
    loop {
        let len = match ip_stack_recv.recv(&mut buf).await {
            Ok(len) => len,
            Err(e) => {
                log::error!("IP stack recv error: {:?}", e);
                break;
            }
        };
        let Some(net) = app_state.get_network() else {
            log::error!("not network");
            break;
        };
        match tun_data_sender.send((&buf[..len]).into(), &net).await {
            Ok(_) => {}
            Err(e) => {
                log::error!("IP stack send error: {:?}", e);
                break;
            }
        }
    }
}
