use crate::crypto::PacketCrypto;
use crate::nat::NetInput;
use crate::port_mapping::PortMapping;
use crate::tls::verifier::CertValidationMode;
use crate::tunnel_core::server::transport::config::{ConnectRegConfig, ProtocolAddress};
use anyhow::bail;
use ipnet::Ipv4Net;
use std::collections::HashSet;
use std::net::Ipv4Addr;

pub const MAX_NETWORK_CODE_LEN: usize = 32;
pub const MAX_DEVICE_ID_LEN: usize = 64;
pub const MAX_NAME_LEN: usize = 128;
pub const MAX_VERSION_LEN: usize = 32;
pub const MAX_MTU: u16 = 1500;

/// 将本房唯一允许的 UDP 目的地转换为本机游戏入口，不解析主机名。
pub fn shared_game_target(own: Ipv4Addr, shared: u16, destination: &str, port: u16) -> Option<std::net::SocketAddr> {
    (shared != 0 && port == shared && destination.parse::<Ipv4Addr>().ok() == Some(own))
        .then(|| std::net::SocketAddr::from((Ipv4Addr::LOCALHOST, shared)))
}

type OwnerCheck = std::sync::Arc<dyn Fn() -> bool + Send + Sync>;
type Grant = (u64, Option<u16>, Option<OwnerCheck>);
#[derive(Clone)]
pub struct GameUdpShare(tokio::sync::watch::Sender<Grant>);
impl std::fmt::Debug for GameUdpShare {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result { f.debug_tuple("GameUdpShare").field(&self.current()).finish() }
}
impl GameUdpShare {
    pub fn new(port: Option<u16>) -> Self { Self(tokio::sync::watch::channel((0, port, None)).0) }
    pub fn current(&self) -> Option<u16> { self.0.borrow().1 }
    pub fn grant_owned(&self, port: u16, valid: OwnerCheck) -> u64 {
        let mut generation=0;
        self.0.send_modify(|old| {generation=old.0.wrapping_add(1); *old=(generation,Some(port),Some(valid));});
        generation
    }
    pub fn set(&self, port: Option<u16>) { self.0.send_if_modified(|old| {
        if old.1 == port { false } else { *old = (old.0.wrapping_add(1), port, None); true }
    }); }
    pub fn revoke(&self, generation: u64) { self.0.send_if_modified(|old| {
        if old.0 != generation || old.1.is_none() { false } else { *old = (old.0.wrapping_add(1), None, None); true }
    }); }
    pub fn watch(&self) -> tokio::sync::watch::Receiver<Grant> { self.0.subscribe() }
    /// 持有读锁直到非阻塞发送返回；撤权写锁返回后，旧代不能再提交本机 UDP 写入。
    pub fn while_granted<T>(&self, generation:u64, port:u16, send:impl FnOnce()->T) -> Option<T> {
        let grant=self.0.borrow();
        if grant.0!=generation || grant.1!=Some(port) {return None;}
        if grant.2.as_ref().is_some_and(|check|!check()) {
            drop(grant); self.revoke(generation); return None;
        }
        Some(send())
    }
}

#[derive(Debug, Clone, Default)]
pub struct Config {
    pub server_addr: Vec<ProtocolAddress>,
    pub cert_mode: CertValidationMode,
    pub network_code: String,
    pub device_id: String,
    pub device_name: String,
    pub tun_name: Option<String>,
    pub ip: Option<Ipv4Addr>,
    pub password: Option<String>,
    pub room_identity: Option<crate::tls::RoomIdentity>,
    pub no_punch: bool,
    pub compress: bool,
    pub rtx: bool,
    pub fec: bool,
    pub input: Vec<NetInput>,
    pub output: Vec<Ipv4Net>,
    pub no_nat: bool,
    pub no_tun: bool,
    pub mtu: Option<u16>,
    pub port_mapping: Vec<PortMapping>,
    pub allow_port_mapping: bool,
    pub game_udp_port: Option<GameUdpShare>,
    pub udp_stun: Vec<String>,
    pub tcp_stun: Vec<String>,
    pub tunnel_port: Option<u16>,
}
impl Config {
    pub fn check(&self) -> anyhow::Result<()> {
        if let Some(share) = &self.game_udp_port && (!self.no_tun || self.allow_port_mapping || !self.no_nat ||
            share.current() == Some(0) || !self.port_mapping.is_empty()) {
            bail!("game UDP sharing requires an isolated host session");
        }
        if self.server_addr.is_empty() {
            bail!("服务器地址不能为空");
        }
        if self.server_addr.len() > 1 {
            let mut set = HashSet::new();

            for a in self.server_addr.iter() {
                if !set.insert(a.address.as_str()) {
                    bail!("服务器地址不能相同")
                }
            }
        }

        if self.network_code.len() > MAX_NETWORK_CODE_LEN {
            bail!(
                "network_code length exceeds {} characters (current: {})",
                MAX_NETWORK_CODE_LEN,
                self.network_code.len()
            )
        }

        if self.device_id.len() > MAX_DEVICE_ID_LEN {
            bail!(
                "device_id length exceeds {} characters (current: {})",
                MAX_DEVICE_ID_LEN,
                self.device_id.len()
            )
        }

        if self.device_name.len() > MAX_NAME_LEN {
            bail!(
                "name length exceeds {} characters (current: {})",
                MAX_NAME_LEN,
                self.device_name.len()
            )
        }
        if let Some(mtu) = self.mtu
            && mtu > MAX_MTU
        {
            bail!("MTU is too large (Maximum mtu: {MAX_MTU})",)
        }
        Ok(())
    }
    pub fn key_sign(&self) -> Option<String> {
        self.password.as_ref().map(|p| PacketCrypto::key_sign(p))
    }
    pub(crate) fn to_connect_config(&self, index: usize) -> ConnectRegConfig {
        ConnectRegConfig {
            server_addr: self.server_addr[index].clone(),
            cert_mode: self.cert_mode.clone(),
            network_code: self.network_code.clone(),
            device_id: self.device_id.clone(),
            device_name: self.device_name.clone(),
            ip: self.ip,
            key_sign: self.key_sign(),
            ip_variable: self.ip.is_none(),
        }
    }
}
