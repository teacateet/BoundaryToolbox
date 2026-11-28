//! “联机”页面使用的原生 VNT 集成。
//!
//! 本地并入的 VNT v2 源码以 Rust crate 形式链接。这个封装让面向 UI 的
//! 对外 API 保持很小：带选项启动、发出快照/事件、通过 oneshot channel 停止。
//! 整个流程不涉及 web UI 或 webview。

use std::collections::HashMap;
use std::fmt::Write as _;
use std::fs;
use std::io::{Cursor, Read};
use std::net::{Ipv4Addr, SocketAddrV4};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use anyhow::{Context, Result, anyhow, bail};
use chrono::{Local, TimeZone};
use sha2::{Digest, Sha256};
use tokio::sync::oneshot;
use vnt_core::context::config::{Config, MAX_NAME_LEN};
use vnt_core::core::{NetworkManager, RegisterResponse};
use vnt_core::tls::verifier::CertValidationMode;
use vnt_core::tunnel_core::server::transport::config::ProtocolAddress;
use vnt_core::utils::task_control::TaskGroupManager;
use vnt_ipc as vnt_core;

use crate::core::{APP_VERSION, INSTALLER_FOLDER_NAME, WINTUN_RELEASE_NAME, WINTUN_RELEASE_URL};

/// 用户提供给 VNT 核心的启动选项。
#[derive(Debug, Clone)]
pub struct VntLaunchOptions {
    pub server_text: String,
    pub username: String,
    pub network_code: String,
    pub password: String,
    pub no_tun: bool,
    pub port_mapping_host: Option<SocketAddrV4>,
    pub shared_game_port: Option<u16>,
    pub compress: bool,
    pub rtx: bool,
    pub no_punch: bool,
}

/// 在 UI 中展示的单个节点/客户端行。
#[derive(Debug, Clone)]
pub struct VntPeer {
    pub name: String,
    pub address: String,
    pub detail: String,
    pub online: bool,
}

/// 在 UI 中展示的单个 VNT 服务器行。
#[derive(Debug, Clone)]
pub struct VntServer {
    pub name: String,
    pub address: String,
    pub detail: String,
    pub online: bool,
}

/// 当前 VNT 网络状态的 UI 快照。
#[derive(Debug, Clone)]
pub struct VntSnapshot {
    pub running: bool,
    pub busy: bool,
    pub status: String,
    pub detail: String,
    pub network_code: String,
    pub virtual_ip: String,
    pub server: String,
    pub nat: String,
    pub peer_summary: String,
    pub servers: Vec<VntServer>,
    pub peers: Vec<VntPeer>,
}

/// 由 VNT 工作线程发给 Slint 控制器的事件。
#[derive(Debug, Clone)]
pub enum VntEvent {
    Snapshot(VntSnapshot),
    Failed(String),
    Stopped(String),
}

type EventSink = Arc<dyn Fn(VntEvent) + Send + Sync + 'static>;

/// 正在运行的 VNT 会话句柄。
///
/// 丢弃该对象会请求关闭，避免窗口关闭后原生核心继续存活。
pub struct VntSession {
    stop_tx: Option<oneshot::Sender<()>>,
    game_route: (bool, Option<SocketAddrV4>),
    share: vnt_core::context::config::GameUdpShare,
    watched_port: Option<u16>,
}

impl VntSession {
    #[cfg(test)]
    pub(crate) fn without_network(options: &VntLaunchOptions) -> Self {
        Self::pending(options).0
    }
    fn pending(options: &VntLaunchOptions) -> (Self, oneshot::Receiver<()>) {
        let (stop_tx, stop_rx) = oneshot::channel();
        (
            Self {
                stop_tx: Some(stop_tx),
                game_route: (options.no_tun, options.port_mapping_host),
                share: vnt_core::context::config::GameUdpShare::new(options.shared_game_port),
                watched_port: None,
            },
            stop_rx,
        )
    }

    /// 在独立 OS 线程上启动 VNT，并使用自己的 Tokio runtime。
    pub fn start(options: VntLaunchOptions, sink: EventSink) -> Result<Self> {
        validate_options(&options)?;
        let (session, stop_rx) = Self::pending(&options);
        let thread_sink = sink.clone();
        let share = session.share.clone();

        thread::Builder::new()
            .name("boundary-vnt".to_string())
            .spawn(move || {
                let result = run_vnt_thread(options, stop_rx, thread_sink.clone(), share);
                if let Err(error) = result {
                    thread_sink(VntEvent::Failed(format_error_chain(error)));
                }
            })
            .context("启动 VNT 后台线程失败")?;

        Ok(session)
    }

    /// 返回本次启动的无网卡模式和映射目标，不受界面输入变更影响。
    pub(crate) fn game_route(&self) -> (bool, Option<SocketAddrV4>) {
        self.game_route
    }

    /// 请求优雅关闭；最终状态会通过 VntEvent::Stopped 返回。
    pub fn stop(&mut self) {
        self.share.set(None);
        if let Some(stop_tx) = self.stop_tx.take() {
            let _ = stop_tx.send(());
        }
    }

    pub(crate) fn share_owned_room(&mut self, target: PathBuf, port: u16) -> Result<()> {
        anyhow::ensure!(self.game_route.1.is_none() && crate::core::fixed_host::valid_room_port(port) &&
            crate::core::fixed_host::owned_room_listening(&target, port)?, "房间没有持有该游戏端口");
        if self.watched_port == Some(port) && self.share.current() == Some(port) { return Ok(()); }
        let share = self.share.clone();
        share.set(Some(port));
        let permit = share.watch();
        let generation = permit.borrow().0;
        if let Err(e) = thread::Builder::new().name("game-port-owner".into()).spawn(move || {
            while matches!(permit.has_changed(), Ok(false)) && share.current() == Some(port) {
                if !crate::core::fixed_host::owned_room_listening(&target, port).unwrap_or(false) {
                    share.revoke(generation);
                    break;
                }
                thread::sleep(Duration::from_secs(1));
            }
        }) { self.share.set(None); return Err(e.into()); }
        self.watched_port = Some(port);
        Ok(())
    }

    pub(crate) fn unshare_room(&self) { self.share.set(None); }
}

impl Drop for VntSession {
    fn drop(&mut self) {
        self.stop();
    }
}

fn format_error_chain(error: anyhow::Error) -> String {
    let mut lines = Vec::new();
    for (index, cause) in error.chain().enumerate() {
        if index == 0 {
            lines.push(cause.to_string());
        } else {
            lines.push(format!("原因：{cause}"));
        }
    }
    lines.join("\n")
}

/// 首次渲染和关闭后使用的未连接状态。
pub fn idle_snapshot() -> VntSnapshot {
    VntSnapshot {
        running: false,
        busy: false,
        status: "未连接".to_string(),
        detail: "填写房间号后启动联机平台".to_string(),
        network_code: "-".to_string(),
        virtual_ip: "-".to_string(),
        server: "-".to_string(),
        nat: "-".to_string(),
        peer_summary: "0 个用户".to_string(),
        servers: vec![VntServer {
            name: "暂无服务器".to_string(),
            address: "启动联机后会显示 VNT 服务器".to_string(),
            detail: "VNT 原生核心未运行".to_string(),
            online: false,
        }],
        peers: vec![VntPeer {
            name: "暂无在线用户".to_string(),
            address: String::new(),
            detail: "启动联机后会显示同房间用户".to_string(),
            online: false,
        }],
    }
}

/// 按常见分隔符拆分用户输入的服务器列表。
pub fn split_servers(raw: &str) -> Vec<String> {
    raw.split(['\n', '\r', '\t', ' ', ',', ';', '，', '；'])
        .map(str::trim)
        .filter(|part| !part.is_empty())
        .map(ToOwned::to_owned)
        .collect()
}

/// 在创建后台线程前校验必填字段。
fn validate_options(options: &VntLaunchOptions) -> Result<()> {
    if split_servers(&options.server_text).is_empty() {
        bail!("请填写 VNT 服务器地址。");
    }
    if options.network_code.trim().is_empty() {
        bail!("请填写房间号。");
    }
    if options.network_code.trim().chars().count() > 32 {
        bail!("房间号最多 32 个字符。");
    }
    let device_name = resolved_device_name(&options.username);
    if device_name.len() > MAX_NAME_LEN {
        bail!("用户名最多 {MAX_NAME_LEN} 字节。");
    }
    Ok(())
}

/// 持有 Tokio runtime，使 Slint UI 线程保持同步模型。
fn run_vnt_thread(
    options: VntLaunchOptions,
    stop_rx: oneshot::Receiver<()>,
    sink: EventSink,
    share: vnt_core::context::config::GameUdpShare,
) -> Result<()> {
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .thread_name("boundary-vnt-runtime")
        .build()
        .context("创建 VNT Tokio runtime 失败")?;

    runtime.block_on(run_vnt(options, stop_rx, sink, share))
}

/// 运行 VNT 核心生命周期：配置、注册、启动 TUN、轮询状态。
async fn run_vnt(
    options: VntLaunchOptions,
    mut stop_rx: oneshot::Receiver<()>,
    sink: EventSink,
    share: vnt_core::context::config::GameUdpShare,
) -> Result<()> {
    sink(VntEvent::Snapshot(VntSnapshot {
        busy: true,
        status: "连接中".to_string(),
        detail: "正在初始化 VNT 原生核心".to_string(),
        network_code: options.network_code.trim().to_string(),
        server: split_servers(&options.server_text).join(", "),
        servers: configured_server_rows(&options.server_text),
        ..idle_snapshot()
    }));

    if !options.no_tun {
        // 在 Windows 上，VNT 会从进程目录加载 wintun.dll。
        extract_wintun_dll().context("准备 wintun.dll 失败")?;
    }

    let device_id = resolved_device_id(&options.username).context("读取设备 ID 失败")?;
    let mut config = build_config(&options, device_id).context("VNT 配置无效")?;
    if options.port_mapping_host.is_none() { config.game_udp_port = Some(share); }
    let task_group_manager = TaskGroupManager::new();
    let (task_group, task_group_guard) = task_group_manager
        .create_task()
        .context("创建 VNT 任务组失败")?;
    let mut network_manager = tokio::select! {
        biased;
        _ = &mut stop_rx => {
            task_group.stop();
            sink(VntEvent::Stopped("已取消启动".into()));
            return Ok(());
        }
        created = NetworkManager::create_network(Box::new(config), task_group.clone()) =>
            created.context("创建 VNT 网络失败")?,
    };
    let api = network_manager.vnt_api();

    sink(VntEvent::Snapshot(VntSnapshot {
        busy: true,
        status: "注册中".to_string(),
        detail: "正在连接服务器并注册虚拟网络".to_string(),
        network_code: options.network_code.trim().to_string(),
        server: split_servers(&options.server_text).join(", "),
        servers: server_rows_from_api(&api),
        ..idle_snapshot()
    }));

    let reg_msg = loop {
        tokio::select! {
            _ = &mut stop_rx => {
                task_group.stop();
                sink(VntEvent::Stopped("已取消启动".to_string()));
                return Ok(());
            }
            result = network_manager.register() => {
                match result {
                    Ok(RegisterResponse::Success(reg_msg)) => break reg_msg,
                    Ok(RegisterResponse::Failed(error)) => {
                        bail!("注册失败：{}", error.message);
                    }
                    Err(error) => {
                        sink(VntEvent::Snapshot(VntSnapshot {
                            busy: true,
                            status: "重试中".to_string(),
                            detail: format!("注册失败，5 秒后重试：{error}"),
                            network_code: options.network_code.trim().to_string(),
                            server: split_servers(&options.server_text).join(", "),
                            servers: server_rows_from_api(&api),
                            ..idle_snapshot()
                        }));
                        tokio::select! {
                            _ = &mut stop_rx => {
                                task_group.stop();
                                sink(VntEvent::Stopped("已取消启动".to_string()));
                                return Ok(());
                            }
                            _ = tokio::time::sleep(Duration::from_secs(5)) => {}
                        }
                    }
                }
            }
        }
    };

    if !network_manager.is_no_tun() {
        // 注册后才会拿到虚拟 IP，之后才能启动本地网卡并分配网络地址。
        sink(VntEvent::Snapshot(VntSnapshot {
            busy: true,
            status: "创建网卡".to_string(),
            detail: format!("已获得虚拟 IP {}，正在配置虚拟网卡", reg_msg.ip),
            network_code: options.network_code.trim().to_string(),
            virtual_ip: reg_msg.ip.to_string(),
            server: split_servers(&options.server_text).join(", "),
            servers: server_rows_from_api(&api),
            ..idle_snapshot()
        }));
        tokio::select! {
            biased;
            _ = &mut stop_rx => {
                task_group.stop();
                sink(VntEvent::Stopped("已取消启动".into()));
                return Ok(());
            }
            configured = async {
                network_manager.start_tun().await.context("创建 TUN 虚拟网卡失败")?;
                network_manager.set_tun_network_ip(reg_msg.ip, reg_msg.prefix_len).await.context("设置虚拟网卡 IP 失败")
            } => configured?,
        }
    }

    let local_device_name = resolved_device_name(&options.username);
    let client_names = client_name_map(&api).await;
    sink(VntEvent::Snapshot(snapshot_from_api(
        &api,
        &client_names,
        &local_device_name,
        false,
        "已联机",
        "VNT 虚拟局域网运行中",
    )));

    let status_api = api.clone();
    let status_sink = sink.clone();
    let status_local_device_name = local_device_name.clone();
    let status_handle = tokio::spawn(async move {
        // 定期轮询 VNT API，让 UI 模型保持最新。
        loop {
            let client_names = client_name_map(&status_api).await;
            status_sink(VntEvent::Snapshot(snapshot_from_api(
                &status_api,
                &client_names,
                &status_local_device_name,
                false,
                "已联机",
                "VNT 虚拟局域网运行中",
            )));
            tokio::time::sleep(Duration::from_secs(2)).await;
        }
    });

    tokio::select! {
        _ = network_manager.wait_all_stopped() => {}
        _ = &mut stop_rx => {
            let client_names = client_name_map(&api).await;
            sink(VntEvent::Snapshot(VntSnapshot {
                busy: true,
                status: "停止中".to_string(),
                detail: "正在关闭 VNT 联机核心".to_string(),
                ..snapshot_from_api(&api, &client_names, &local_device_name, true, "停止中", "正在关闭 VNT 联机核心")
            }));
            task_group.stop();
            network_manager.wait_all_stopped().await;
        }
    }

    // 在丢弃 NetworkManager 前先停止轮询任务，避免快照读取和清理过程竞争。
    status_handle.abort();
    let _ = status_handle.await;
    drop(network_manager);
    drop(task_group_guard);
    sink(VntEvent::Stopped("联机核心已停止".to_string()));
    Ok(())
}

/// 将 UI 选项转换为 VNT 核心需要的配置。
fn build_config(options: &VntLaunchOptions, device_id: String) -> Result<Config> {
    let server_addr = split_servers(&options.server_text)
        .into_iter()
        .map(|server| {
            server
                .parse::<ProtocolAddress>()
                .map_err(|error| anyhow!("服务器地址 `{server}` 无效：{error}"))
        })
        .collect::<Result<Vec<_>>>()?;

    let password = if options.password.trim().is_empty() {
        None
    } else {
        Some(options.password.trim().to_string())
    };

    if let Some(port) = options.shared_game_port {
        anyhow::ensure!(options.no_tun && options.port_mapping_host.is_none() &&
            crate::core::fixed_host::valid_room_port(port), "本次连接没有可分享的游戏端口");
    }
    Ok(Config {
        server_addr,
        network_code: options.network_code.trim().to_string(),
        device_id,
        device_name: resolved_device_name(&options.username),
        tun_name: Some("boundary-vnt".to_string()),
        password,
        // 内置 VNT 默认以该模式连接公共社区服务器。
        cert_mode: CertValidationMode::InsecureSkipVerification,
        no_punch: options.no_punch,
        compress: options.compress,
        rtx: options.rtx,
        no_tun: options.no_tun,
        port_mapping: options.guest_game_mappings()?,
        // 房客只发起游戏连接；房主仅共享本次房间的 UDP 端口。
        allow_port_mapping: false,
        game_udp_port: options.shared_game_port.map(|p| vnt_core::context::config::GameUdpShare::new(Some(p))),
        no_nat: true,
        ..Default::default()
    })
}

async fn client_name_map(api: &vnt_core::api::VntApi) -> HashMap<Ipv4Addr, String> {
    match api.server_rpc().client_list().await {
        Ok(client_list) => {
            let mut names = HashMap::<Ipv4Addr, (String, bool, i64)>::new();
            for client in client_list.list {
                let name = client.name.trim();
                if name.is_empty() {
                    continue;
                }
                let ip = Ipv4Addr::from(client.ip);
                let should_replace = names
                    .get(&ip)
                    .map(|(_, online, last_connected_time)| {
                        (client.online && !*online)
                            || (client.online == *online
                                && client.last_connected_time > *last_connected_time)
                    })
                    .unwrap_or(true);
                if should_replace {
                    names.insert(
                        ip,
                        (name.to_string(), client.online, client.last_connected_time),
                    );
                }
            }
            names
                .into_iter()
                .map(|(ip, (name, _, _))| (ip, name))
                .collect()
        }
        Err(error) => {
            log::debug!("读取 VNT 客户端用户名失败：{error}");
            HashMap::new()
        }
    }
}

/// 从 VNT API 读取实时状态，并整理成 UI 所需结构。
fn snapshot_from_api(
    api: &vnt_core::api::VntApi,
    client_names: &HashMap<Ipv4Addr, String>,
    local_device_name: &str,
    busy: bool,
    status: &str,
    detail: &str,
) -> VntSnapshot {
    let config = api.get_config();
    let network = api.network();
    let clients = api.client_ips();
    let direct_count = clients
        .iter()
        .filter(|client| api.is_direct(&client.ip))
        .count();
    let online_count = clients.iter().filter(|client| client.online).count();
    let server_nodes = api.server_node_list();
    let connected_servers = server_nodes
        .iter()
        .filter(|server| server.connected)
        .count();
    let servers = if server_nodes.is_empty() {
        config
            .as_ref()
            .map(|config| {
                config
                    .server_addr
                    .iter()
                    .map(ToString::to_string)
                    .collect::<Vec<_>>()
                    .join(", ")
            })
            .map(|text| configured_server_rows(&text))
            .unwrap_or_else(|| idle_snapshot().servers)
    } else {
        server_rows_from_nodes(server_nodes.clone())
    };
    let nat_info = api.nat_info();
    let local_ip = network.map(|network| network.ip);

    let mut peers = if clients.is_empty() {
        vec![VntPeer {
            name: "暂无在线用户".to_string(),
            address: String::new(),
            detail: "等待同房间用户上线".to_string(),
            online: false,
        }]
    } else {
        clients
            .into_iter()
            .map(|client| {
                // 路由和 RTT 是尽力而为的诊断信息，缺失时不应导致整个快照失败。
                let rtt = api
                    .get_rtt(&client.ip)
                    .map(|value| format!("{value} ms"))
                    .unwrap_or_else(|| "-".to_string());
                let route = api
                    .find_route(&client.ip)
                    .map(|route| route.route_key().to_string())
                    .unwrap_or_else(|| "经服务器转发".to_string());
                let connection = if api.is_direct(&client.ip) {
                    "P2P 直连"
                } else {
                    "服务器转发"
                };
                let name = if Some(client.ip) == local_ip {
                    local_device_name.to_string()
                } else {
                    client_names
                        .get(&client.ip)
                        .map(String::as_str)
                        .map(str::trim)
                        .filter(|value| !value.is_empty())
                        .unwrap_or("未知用户")
                        .to_string()
                };
                VntPeer {
                    name,
                    address: client.ip.to_string(),
                    detail: format!("{connection} / 延迟 {rtt} / 路由 {route}"),
                    online: client.online,
                }
            })
            .collect()
    };
    peers.sort_by_key(vnt_peer_sort_key);

    VntSnapshot {
        running: status != "未连接",
        busy,
        status: status.to_string(),
        detail: detail.to_string(),
        network_code: config
            .as_ref()
            .map(|config| config.network_code.clone())
            .unwrap_or_else(|| "-".to_string()),
        virtual_ip: network
            .map(|network| format!("{}/{}", network.ip, network.prefix_len))
            .unwrap_or_else(|| "-".to_string()),
        server: if server_nodes.is_empty() {
            config
                .as_ref()
                .map(|config| {
                    config
                        .server_addr
                        .iter()
                        .map(ToString::to_string)
                        .collect::<Vec<_>>()
                        .join(", ")
                })
                .filter(|text| !text.is_empty())
                .unwrap_or_else(|| "-".to_string())
        } else {
            format!("{} / {} 已连接", connected_servers, server_nodes.len())
        },
        nat: nat_info
            .map(|info| {
                let ipv4 = if info.public_ips.is_empty() {
                    "-".to_string()
                } else {
                    info.public_ips
                        .iter()
                        .map(ToString::to_string)
                        .collect::<Vec<_>>()
                        .join(", ")
                };
                format!("{:?} / {}", info.nat_type, ipv4)
            })
            .unwrap_or_else(|| "-".to_string()),
        peer_summary: format!(
            "{} 在线 / {} 直连 / {} 总计",
            online_count,
            direct_count,
            peers
                .iter()
                .filter(|peer| peer.name != "暂无在线用户")
                .count()
        ),
        servers,
        peers,
    }
}

fn vnt_peer_sort_key(peer: &VntPeer) -> (u8, String, String) {
    let group = if peer.address.is_empty() {
        2
    } else if peer.online {
        0
    } else {
        1
    };
    (group, peer.name.to_lowercase(), peer.address.clone())
}

/// 在 VNT 核心尚未创建 API 时，从用户输入生成服务器占位行。
fn configured_server_rows(raw: &str) -> Vec<VntServer> {
    let servers = split_servers(raw);
    if servers.is_empty() {
        return idle_snapshot().servers;
    }

    servers
        .into_iter()
        .enumerate()
        .map(|(index, address)| VntServer {
            name: format!("服务器 {}", index + 1),
            address,
            detail: "等待 VNT 原生核心连接".to_string(),
            online: false,
        })
        .collect()
}

/// 从 VNT 原生 API 读取所有服务器节点。
fn server_rows_from_api(api: &vnt_core::api::VntApi) -> Vec<VntServer> {
    server_rows_from_nodes(api.server_node_list())
}

/// 将 VNT 原生服务器节点整理为 UI 行，保持 server_id 顺序稳定。
fn server_rows_from_nodes(mut nodes: Vec<vnt_core::context::ServerNodeInfo>) -> Vec<VntServer> {
    if nodes.is_empty() {
        return idle_snapshot().servers;
    }

    nodes.sort_by_key(|node| node.server_id);
    nodes
        .into_iter()
        .map(|node| {
            let rtt = node
                .rtt
                .map(|value| format!("{value} ms"))
                .unwrap_or_else(|| "-".to_string());
            let online_clients = node
                .client_map
                .values()
                .filter(|client| client.online)
                .count();
            let version = node
                .server_version
                .as_deref()
                .filter(|value| !value.trim().is_empty())
                .unwrap_or("-");
            let time_text = if node.connected {
                format_vnt_time(node.last_connected_time)
                    .map(|time| format!("连接于 {time}"))
                    .unwrap_or_else(|| "已连接".to_string())
            } else {
                format_vnt_time(node.disconnected_time)
                    .map(|time| format!("断开于 {time}"))
                    .unwrap_or_else(|| "等待连接".to_string())
            };

            VntServer {
                name: format!("服务器 {}", node.server_id + 1),
                address: node.server_addr.to_string(),
                detail: format!(
                    "延迟 {rtt} / 节点 {online_clients} / 版本 {version} / {time_text}"
                ),
                online: node.connected,
            }
        })
        .collect()
}

/// 将 VNT 内部毫秒时间戳格式化为本地时间，异常时间戳直接忽略。
fn format_vnt_time(timestamp_ms: Option<i64>) -> Option<String> {
    let timestamp_ms = timestamp_ms?;
    Local
        .timestamp_millis_opt(timestamp_ms)
        .single()
        .map(|time| time.format("%H:%M:%S").to_string())
}

/// 在 VNT 网络内使用的稳定设备显示名。
fn resolved_device_id(username: &str) -> Result<String> {
    let base_id = vnt_core::utils::device_id::get_device_id()?;
    let username = username.trim();
    if username.is_empty() {
        return Ok(base_id);
    }

    let mut hasher = Sha256::new();
    hasher.update(base_id.as_bytes());
    hasher.update([0]);
    hasher.update(username.as_bytes());
    let digest = hasher.finalize();
    let mut suffix = String::with_capacity(32);
    for byte in digest.iter().take(16) {
        let _ = write!(&mut suffix, "{byte:02x}");
    }
    Ok(format!("boundary-{suffix}"))
}

fn resolved_device_name(username: &str) -> String {
    let username = username.trim();
    if username.is_empty() {
        default_device_name()
    } else {
        username.to_string()
    }
}

fn default_device_name() -> String {
    let host = std::env::var("COMPUTERNAME")
        .or_else(|_| std::env::var("HOSTNAME"))
        .unwrap_or_else(|_| "Windows".to_string());
    format!("Boundary-{host}")
}

#[cfg(windows)]
/// 使用 TUN 模式时，按需下载 Wintun 并解压到可执行文件旁边。
fn extract_wintun_dll() -> Result<()> {
    let runtime_dir = std::env::current_exe()
        .ok()
        .and_then(|path| path.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| PathBuf::from("."));
    let path = runtime_dir.join("wintun.dll");
    if !path.exists() {
        let cache_dir = if runtime_dir.file_name().is_some_and(|name| {
            name.to_string_lossy()
                .eq_ignore_ascii_case(INSTALLER_FOLDER_NAME)
        }) {
            runtime_dir.join("downloads")
        } else {
            runtime_dir.join(INSTALLER_FOLDER_NAME).join("downloads")
        };
        let zip_bytes = cached_or_downloaded_wintun_zip(&cache_dir)?;
        let dll = read_wintun_dll(&zip_bytes)?;
        fs::write(&path, dll).with_context(|| format!("写入 {} 失败", path.display()))?;
    }
    Ok(())
}

#[cfg(windows)]
fn cached_or_downloaded_wintun_zip(cache_dir: &Path) -> Result<Vec<u8>> {
    fs::create_dir_all(cache_dir)
        .with_context(|| format!("创建下载缓存目录失败：{}", cache_dir.display()))?;
    let zip_path = cache_dir.join(WINTUN_RELEASE_NAME);
    if zip_path.exists() {
        return fs::read(&zip_path)
            .with_context(|| format!("读取 Wintun 缓存失败：{}", zip_path.display()));
    }
    let bytes = reqwest::blocking::Client::builder()
        .timeout(Duration::from_secs(90))
        .user_agent(format!("boundary-toolbox/{APP_VERSION}"))
        .build()?
        .get(WINTUN_RELEASE_URL)
        .send()
        .context("下载 Wintun 失败")?
        .error_for_status()
        .context("Wintun 下载地址返回错误状态")?
        .bytes()
        .context("读取 Wintun 下载内容失败")?
        .to_vec();
    fs::write(&zip_path, &bytes)
        .with_context(|| format!("写入 Wintun 缓存失败：{}", zip_path.display()))?;
    Ok(bytes)
}

#[cfg(windows)]
fn read_wintun_dll(zip_bytes: &[u8]) -> Result<Vec<u8>> {
    let arch = match std::env::consts::ARCH {
        "x86_64" => "amd64",
        "x86" => "x86",
        "aarch64" => "arm64",
        "arm" => "arm",
        other => bail!("当前架构暂不支持自动下载 Wintun：{other}"),
    };
    let entry_name = format!("wintun/bin/{arch}/wintun.dll");
    let mut archive =
        zip::ZipArchive::new(Cursor::new(zip_bytes)).context("无法读取 Wintun 压缩包")?;
    let mut entry = archive
        .by_name(&entry_name)
        .with_context(|| format!("Wintun 压缩包缺少 {entry_name}"))?;
    let mut bytes = Vec::new();
    entry
        .read_to_end(&mut bytes)
        .with_context(|| format!("读取 {entry_name} 失败"))?;
    if bytes.is_empty() {
        bail!("Wintun 压缩包中的 {entry_name} 是空文件");
    }
    Ok(bytes)
}

#[cfg(not(windows))]
/// 非 Windows 构建不需要内置 Wintun DLL。
fn extract_wintun_dll() -> Result<()> {
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cancel_registration_to_a_silent_local_server_then_retry() -> Result<()> {
        use std::net::TcpListener;
        use std::sync::mpsc;
        use std::time::Instant;
        let listener = TcpListener::bind(("127.0.0.1", 0))?;
        for _ in 0..2 {
            let options = VntLaunchOptions {
                server_text: format!("tcp://{}", listener.local_addr()?), username: "qa".into(),
                network_code: "brt-local-qa".into(), password: String::new(), no_tun: true,
                port_mapping_host: None, shared_game_port: None, compress: false, rtx: false, no_punch: true,
            };
            let (tx, rx) = mpsc::channel();
            let mut session = VntSession::start(options, Arc::new(move |e| { let _ = tx.send(e); }))?;
            let until = Instant::now() + Duration::from_secs(10);
            loop {
                match rx.recv_timeout(until.saturating_duration_since(Instant::now()))? {
                    VntEvent::Snapshot(s) if s.status == "注册中" || s.status == "重试中" => break,
                    VntEvent::Failed(e) => bail!("{e}"),
                    _ => {}
                }
            }
            session.stop();
            let until = Instant::now() + Duration::from_secs(3);
            loop {
                match rx.recv_timeout(until.saturating_duration_since(Instant::now()))? {
                    VntEvent::Stopped(reason) => { anyhow::ensure!(reason.contains("取消"), "Unexpected stop: {reason}"); break; }
                    VntEvent::Failed(e) => bail!("{e}"),
                    _ => {}
                }
            }
        }
        Ok(())
    }

    #[test]
    fn vnt_room_session_route_matches_core_config_without_starting_vnt() -> Result<()> {
        let host: SocketAddrV4 = "10.26.0.8:18777".parse()?;
        let mut options = VntLaunchOptions {
            server_text: "tcp://127.0.0.1:11010".into(),
            username: "guest".into(),
            network_code: "test-room".into(),
            password: String::new(),
            no_tun: true,
            port_mapping_host: Some(host),
            shared_game_port: None,
            compress: false,
            rtx: false,
            no_punch: false,
        };
        let (session, _stop_rx) = VntSession::pending(&options);
        let config = build_config(&options, "test-device".into())?;
        config.check()?;
        assert!(config.no_tun && !config.allow_port_mapping && config.no_nat && config.game_udp_port.is_none());
        let [mapping] = config.port_mapping.as_slice() else {
            panic!("expected one UDP game mapping");
        };
        assert_eq!(
            mapping.to_string(),
            "udp://127.0.0.1:7777-10.26.0.8-10.26.0.8:18777"
        );

        options.no_tun = false;
        assert!(build_config(&options, "test-device".into()).is_err());
        options.port_mapping_host = None;
        for mode in [false, true] {
            options.no_tun = mode;
            let (next, _rx) = VntSession::pending(&options);
            let config = build_config(&options, "test-device".into())?;
            config.check()?;
            assert_eq!(next.game_route(), (mode, None));
            assert_eq!(config.no_tun, mode);
            assert!(config.port_mapping.is_empty());
        }
        assert_eq!(session.game_route(), (true, Some(host)));
        // 纯策略回归：不向内网、回环档案或其他进程发起连接。
        let own = *host.ip();
        let target = |dest, port| vnt_core::context::config::shared_game_target(own, 18777, dest, port);
        for (dest, port) in [("127.0.0.1",18777), ("localhost",18777), ("192.168.1.1",18777),
            ("10.26.0.9",18777), ("10.26.0.8",8000), ("::1",18777)] {
            assert!(target(dest, port).is_none());
        }
        assert_eq!(target("10.26.0.8",18777), Some((Ipv4Addr::LOCALHOST,18777).into()));
        options.no_tun = true;
        options.shared_game_port = Some(18777);
        let shared = build_config(&options, "host".into())?;
        shared.check()?;
        assert!(shared.no_nat && !shared.allow_port_mapping && shared.game_udp_port.as_ref().unwrap().current() == Some(18777));
        options.shared_game_port = Some(9001);
        assert!(build_config(&options, "host".into()).is_err());
        let share = shared.game_udp_port.unwrap();
        let before = share.watch().borrow().0;
        share.set(None);
        share.set(Some(18777));
        share.revoke(before);
        assert_eq!(share.current(), Some(18777));
        Ok(())
    }
}
