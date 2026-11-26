//! AppController 联机会话控制和状态同步。

use super::*;
use std::net::SocketAddrV4;

#[path = "vnt_room.rs"]
pub(super) mod room;
use room::RoomError;

impl VntLaunchOptions {
    pub(crate) fn guest_game_mappings(&self) -> Result<Vec<vnt_ipc::port_mapping::PortMapping>> {
        room::guest_mapping(self.no_tun, self.port_mapping_host).map_err(anyhow::Error::msg)
    }
}

impl AppController {
    /// 将用户输入的 VNT 服务器地址加入下拉框，并立即切换到这个地址。
    pub(super) fn add_vnt_server_option(&mut self, address: String) {
        let address = address.trim().to_string();
        if address.is_empty() {
            return;
        }

        let exists = (0..self.vnt_server_option_model.row_count()).any(|index| {
            self.vnt_server_option_model
                .row_data(index)
                .map(|item| item.as_str().eq_ignore_ascii_case(&address))
                .unwrap_or(false)
        });
        if !exists {
            self.vnt_server_option_model.push(address.clone().into());
        }

        self.ui.set_vnt_server_text(address.into());
        self.ui.set_vnt_new_server_text("".into());
        self.save_app_prefs();
    }

    /// 启动本地并入的 VNT 核心，并将状态流式同步到 UI。
    pub(super) fn start_vnt(&mut self) {
        if !self.is_admin {
            self.show_error_dialog(
                self.tr("联机", "Network", "ネットワーク"),
                self.tr(
                    "联机功能需要管理员模式启动工具箱。",
                    "Network requires running the toolbox as administrator.",
                    "ネットワーク機能には管理者モードでの起動が必要です。",
                ),
            );
            return;
        }
        if self.vnt_session.is_some() || self.ui.get_vnt_busy() {
            return;
        }
        self.save_app_prefs();

        // 无网卡邀请在回环接收游戏数据；启动现有组网不必改动 Windows 入站规则。
        let options = VntLaunchOptions {
            server_text: self.ui.get_vnt_server_text().to_string(),
            username: self.ui.get_vnt_username().to_string(),
            network_code: self.ui.get_vnt_network_code().to_string(),
            password: self.ui.get_vnt_password().to_string(),
            no_tun: self.ui.get_vnt_no_tun(),
            port_mapping_host: None,
            compress: self.ui.get_vnt_compress(),
            rtx: self.ui.get_vnt_rtx(),
            no_punch: self.ui.get_vnt_no_punch(),
        };
        let select_host_after_start = options.no_tun && self.ui.get_pending_friend_invite().is_empty();
        self.start_vnt_options(options, select_host_after_start);
    }

    fn start_vnt_options(&mut self, options: VntLaunchOptions, select_host_after_start: bool) {
        let tx = self.tx.clone();
        let sink = Arc::new(move |event| {
            let _ = tx.send(AppMessage::VntEvent(event));
        });

        self.vnt_host_selection_pending = select_host_after_start && options.no_tun;
        self.ui.set_show_vnt_host_dialog(false);
        self.ui.set_vnt_stopping(false);
        self.ui.set_vnt_busy(true);
        self.ui
            .set_vnt_status_text(self.tr("启动中", "Starting", "起動中").into());
        self.ui.set_vnt_detail_text(
            self.tr(
                "正在启动联机平台",
                "Starting the network platform",
                "ネットワーク基盤を起動中",
            )
            .into(),
        );
        self.append_log(&format!(
            "[{}] 启动 VNT 联机：{} / {}",
            core::now_text(),
            options.server_text,
            options.network_code
        ));

        match VntSession::start(options, sink) {
            Ok(session) => {
                self.vnt_session = Some(session);
            }
            Err(error) => {
                self.invite_auto_route = None;
                self.vnt_host_selection_pending = false;
                self.ui.set_vnt_busy(false);
                self.ui.set_vnt_running(false);
                self.ui
                    .set_vnt_status_text(self.tr("启动失败", "Start failed", "起動失敗").into());
                self.ui.set_vnt_detail_text(error.to_string().into());
                self.append_log(&format!("[{}] VNT 启动失败：{}", core::now_text(), error));
                self.show_error_dialog(
                    self.tr("联机", "Network", "ネットワーク"),
                    &error.to_string(),
                );
            }
        }
    }

    /// 请求 VNT 会话停止；最终清理结果会通过事件返回。
    pub(super) fn stop_vnt(&mut self) {
        self.invite_auto_route = None;
        self.vnt_host_selection_pending = false;
        self.vnt_restart_after_stop = None;
        self.ui.set_show_vnt_host_dialog(false);
        if let Some(session) = self.vnt_session.as_mut() {
            session.stop();
            self.ui.set_vnt_stopping(true);
            self.ui.set_vnt_busy(true);
            self.ui
                .set_vnt_status_text(self.tr("停止中", "Stopping", "停止中").into());
            self.ui.set_vnt_detail_text(
                self.tr(
                    "正在关闭联机平台",
                    "Stopping the network platform",
                    "ネットワーク基盤を停止中",
                )
                .into(),
            );
            self.append_log(&format!("[{}] 正在停止 VNT 联机", core::now_text()));
        } else {
            self.apply_vnt_snapshot(localized_vnt_idle_snapshot(self.language()));
        }
    }

    /// 等待下一次 VNT 快照刷新时先给出即时反馈。
    pub(super) fn refresh_vnt_status_hint(&mut self) {
        if self.vnt_session.is_none() && !self.ui.get_vnt_busy() {
            self.apply_vnt_snapshot(localized_vnt_idle_snapshot(self.language()));
        } else if self.vnt_session.is_some() {
            self.ui.set_vnt_detail_text(
                self.tr(
                    "等待联机核心刷新状态",
                    "Waiting for the network core to refresh",
                    "ネットワークコアの状態更新待ち",
                )
                .into(),
            );
        }
    }

    /// 应用 VNT 工作线程发出的生命周期事件。
    pub(super) fn apply_vnt_event(&mut self, event: VntEvent) {
        match event {
            VntEvent::Snapshot(_) if self.ui.get_vnt_stopping() => {}
            VntEvent::Snapshot(snapshot) => self.apply_vnt_snapshot(snapshot),
            VntEvent::Failed(error) => {
                self.invite_auto_route = None;
                if self.ui.get_vnt_stopping() {
                    self.apply_vnt_event(VntEvent::Stopped("已取消启动".into()));
                    return;
                }
                self.vnt_session = None;
                self.vnt_host_selection_pending = false;
                self.vnt_restart_after_stop = None;
                self.ui.set_show_vnt_host_dialog(false);
                self.ui.set_vnt_busy(false);
                self.ui.set_vnt_running(false);
                self.ui
                    .set_vnt_status_text(self.tr("启动失败", "Start failed", "起動失敗").into());
                self.ui.set_vnt_detail_text(error.clone().into());
                self.set_vnt_server_rows(vnt_server_placeholder_rows(self.language()));
                self.set_vnt_peer_rows(vnt_placeholder_rows(self.language()));
                self.append_log(&format!("[{}] VNT 异常：{}", core::now_text(), error));
                self.show_error_dialog(self.tr("联机", "Network", "ネットワーク"), &error);
            }
            VntEvent::Stopped(reason) => {
                self.ui.set_vnt_stopping(false);
                self.vnt_session = None;
                self.ui.set_show_vnt_host_dialog(false);
                if let Some(options) = self.vnt_restart_after_stop.take() {
                    if options.port_mapping_host.is_some() {
                        match self.vnt_mapping_port_conflicts() {
                            Ok(conflicts) if !conflicts.is_empty() => {
                                self.invite_auto_route = None;
                                self.show_vnt_mapping_conflict_dialog(&conflicts);
                                let mut snapshot = localized_vnt_idle_snapshot(self.language());
                                snapshot.detail = self
                                    .tr(
                                        "端口被占用，未启动无网卡映射。",
                                        "The port is occupied; no-adapter mapping was not started.",
                                        "ポートが使用中のため、アダプターなしの転送は開始されませんでした。",
                                    )
                                    .to_string();
                                self.apply_vnt_snapshot(snapshot);
                                return;
                            }
                            Err(error) => {
                                self.invite_auto_route = None;
                                let mut snapshot = localized_vnt_idle_snapshot(self.language());
                                snapshot.detail = error.to_string();
                                self.apply_vnt_snapshot(snapshot);
                                self.show_error_dialog(
                                    self.tr("联机", "Network", "ネットワーク"),
                                    &error.to_string(),
                                );
                                return;
                            }
                            _ => {}
                        }
                    }
                    self.append_log(&format!(
                        "[{}] VNT 已停止，正在以房主映射重启：{}",
                        core::now_text(),
                        reason
                    ));
                    self.start_vnt_options(options, false);
                    return;
                }
                self.invite_auto_route = None;
                let mut snapshot = localized_vnt_idle_snapshot(self.language());
                snapshot.detail = reason.clone();
                self.apply_vnt_snapshot(snapshot);
                self.append_log(&format!("[{}] VNT 已停止：{}", core::now_text(), reason));
                if should_show_vnt_stop_error(&reason) {
                    self.show_error_dialog(self.tr("联机", "Network", "ネットワーク"), &reason);
                }
            }
        }
    }

    /// 将 VNT 快照同步到 Slint 属性和节点模型。
    pub(super) fn apply_vnt_snapshot(&mut self, snapshot: vnt_platform::VntSnapshot) {
        self.ui.set_vnt_running(snapshot.running);
        self.ui.set_vnt_busy(snapshot.busy);
        self.ui.set_vnt_status_text(snapshot.status.into());
        self.ui.set_vnt_detail_text(snapshot.detail.into());
        let own_ip = room::own_ip(&snapshot.virtual_ip);
        self.ui.set_vnt_ip_text(snapshot.virtual_ip.into());
        self.ui.set_vnt_server_status_text(snapshot.server.into());
        self.ui.set_vnt_nat_text(snapshot.nat.into());
        self.ui
            .set_vnt_peer_summary_text(snapshot.peer_summary.into());
        if !snapshot.network_code.is_empty() && snapshot.network_code != "-" {
            self.ui.set_vnt_network_code(snapshot.network_code.into());
        }
        self.set_vnt_server_rows(
            snapshot
                .servers
                .into_iter()
                .map(vnt_server_to_row)
                .collect(),
        );
        self.set_vnt_peer_rows(
            snapshot
                .peers
                .into_iter()
                .filter(|peer| {
                    peer.address.is_empty()
                        || own_ip.is_some_and(|ip| {
                            peer.address
                                .parse::<Ipv4Addr>()
                                .is_ok_and(|remote| remote != ip)
                        })
                })
                .map(vnt_peer_to_row)
                .collect(),
        );
        self.resume_invite_after_mapping();
        self.maybe_show_vnt_host_dialog();
    }

    fn maybe_show_vnt_host_dialog(&self) {
        if self.vnt_host_selection_pending
            && self.ui.get_vnt_no_tun()
            && self.ui.get_vnt_running()
            && !self.ui.get_vnt_busy()
            && !self.ui.get_show_vnt_host_dialog()
        {
            self.ui.set_show_vnt_host_dialog(true);
        }
    }

    fn selected_vnt_room(&self, address: &str) -> Result<SocketAddrV4, RoomError> {
        if !self.ui.get_vnt_running()
            || self.ui.get_vnt_busy()
            || self.ui.get_busy()
            || self.vnt_session.is_none()
            || self.vnt_restart_after_stop.is_some()
        {
            return Err(RoomError::NotReady);
        }
        room::select_peer(
            self.ui.get_vnt_ip_text().as_str(),
            address,
            self.ui.get_vnt_game_port().as_str(),
            |ip| {
                self.vnt_peer_model
                    .iter()
                    .any(|peer| peer.online && peer.address.parse::<Ipv4Addr>().ok() == Some(ip))
            },
        )
    }

    fn show_vnt_room_error(&mut self, error: RoomError) {
        let message = match error {
            RoomError::NotReady => self.tr("联机尚未就绪，请等待虚拟 IP 和节点列表更新。", "Wait for the virtual IP and peer list to be ready.", "仮想 IP とノード一覧の更新を待ってください。"),
            RoomError::InvalidPeer => self.tr("房主虚拟 IP 无效。", "Invalid host virtual IP.", "ホストの仮想 IP が無効です。"),
            RoomError::OwnPeer => self.tr("不能加入本机节点，请选择另一位房主。", "Select another peer, not this device.", "この端末以外のホストを選択してください。"),
            RoomError::OfflinePeer => self.tr("该节点已不在当前在线列表中，请重新选择。", "This peer is no longer online. Select an online peer.", "このノードはオフラインです。再度選択してください。"),
            RoomError::InvalidPort => self.tr("房主游戏端口须为 1024 至 65535 的整数，6969、8000、9000 为保留端口。", "Use a host game port from 1024 to 65535, excluding 6969, 8000 and 9000.", "ホストのゲームポートは 1024～65535 で、6969、8000、9000 は使用できません。"),
            RoomError::WrongMapping => self.tr("当前无网卡映射与所选房主或端口不符。请停止联机，重新启动后选择该房主及游戏端口。", "Restart networking and select this host and game port. The current no-adapter mapping does not match.", "現在の転送先が一致しません。ネットワークを再起動し、このホストとゲームポートを選択してください。"),
        };
        self.show_error_dialog(
            self.tr("加入联机", "Join Network", "ネットワーク参加"),
            message,
        );
    }

    pub(super) fn join_vnt_room_peer(&mut self, address: &str) {
        let target = self.selected_vnt_room(address).and_then(|peer| {
            let (no_tun, mapped_peer) = self
                .vnt_session
                .as_ref()
                .ok_or(RoomError::NotReady)?
                .game_route();
            room::game_target(peer, no_tun, mapped_peer)
        });
        match target {
            Ok(target) => self.start_join_match(&target.to_string()),
            Err(error) => self.show_vnt_room_error(error),
        }
    }

    pub(super) fn join_vnt_invite(&mut self, peer: SocketAddrV4) {
        // 没有会话时保留邀请供玩家连接；不能把虚拟地址当作公网直连或替玩家建立新网络。
        if self.vnt_session.is_none() || !self.ui.get_vnt_running() || self.ui.get_vnt_busy() { return; }
        let host = match self.selected_vnt_room(&peer.ip().to_string()) {
            Ok(host) if host == peer => host,
            Ok(_) => { self.show_vnt_room_error(RoomError::InvalidPort); return; }
            Err(e) => { self.show_vnt_room_error(e); return; }
        };
        let Some((no_tun, mapping)) = self.vnt_session.as_ref().map(VntSession::game_route) else { return; };
        if let Ok(target) = room::game_target(host, no_tun, mapping) {
            self.clear_friend_invite();
            self.start_launch(LaunchMode::Join(target));
        } else if self.active_fixed_room.is_some() {
            self.show_error_dialog(self.tr("加入好友","Join a friend","フレンドに参加"),
                self.tr("请先关闭自己的房间，再更换无网卡映射。","Close your hosted room before changing its network mapping.","転送先を変える前に自分のルームを閉じてください。"));
        } else if self.rebind_vnt_host(host, mapping.is_some()) {
            self.invite_auto_route = Some((host, std::time::Instant::now() + std::time::Duration::from_secs(45)));
        }
    }

    fn resume_invite_after_mapping(&mut self) {
        let Some((peer, until)) = self.invite_auto_route else { return; };
        let typed = friend_invite::Invite::parse(self.ui.get_pending_friend_invite().as_str());
        if std::time::Instant::now() >= until || self.ui.get_page() != 3 || typed != Some(friend_invite::Invite::Vnt(peer)) {
            self.invite_auto_route = None; return;
        }
        if self.ui.get_show_app_dialog() || self.ui.get_vnt_stopping() || self.vnt_restart_after_stop.is_some() { return; }
        if self.vnt_session.as_ref().is_some_and(|s| s.game_route() == (true,Some(peer)))
            && self.selected_vnt_room(&peer.ip().to_string()) == Ok(peer) {
            self.clear_friend_invite();
            self.start_launch(LaunchMode::Join(room::LOCAL_GAME_ENTRY));
        }
    }

    pub(super) fn select_vnt_mapping_host(&mut self, address: String) {
        if !self.vnt_host_selection_pending
            || !self
                .vnt_session
                .as_ref()
                .is_some_and(|s| s.game_route() == (true, None))
        {
            self.show_vnt_room_error(RoomError::NotReady);
            return;
        }
        let host = match self.selected_vnt_room(&address) {
            Ok(host) => host,
            Err(error) => {
                self.show_vnt_room_error(error);
                return;
            }
        };
        self.rebind_vnt_host(host, false);
    }

    // 替换已有映射时，本会话仍占用旧入口；停止后原流程还会再次检查该端口。
    fn rebind_vnt_host(&mut self, host: SocketAddrV4, replacing_live_mapping: bool) -> bool {
        if !replacing_live_mapping {
            match self.vnt_mapping_port_conflicts() {
                Ok(conflicts) if !conflicts.is_empty() => {
                    self.show_vnt_mapping_conflict_dialog(&conflicts);
                    return false;
                }
                Err(error) => {
                    self.show_error_dialog(
                        self.tr("联机", "Network", "ネットワーク"),
                        &error.to_string(),
                    );
                    return false;
                }
                _ => {}
            }
        }

        let options = VntLaunchOptions {
            server_text: self.ui.get_vnt_server_text().to_string(),
            username: self.ui.get_vnt_username().to_string(),
            network_code: self.ui.get_vnt_network_code().to_string(),
            password: self.ui.get_vnt_password().to_string(),
            no_tun: true,
            port_mapping_host: Some(host),
            compress: self.ui.get_vnt_compress(),
            rtx: self.ui.get_vnt_rtx(),
            no_punch: self.ui.get_vnt_no_punch(),
        };

        self.vnt_host_selection_pending = false;
        self.ui.set_show_vnt_host_dialog(false);
        self.vnt_restart_after_stop = Some(options);
        if let Some(session) = self.vnt_session.as_mut() {
            session.stop();
            self.ui.set_vnt_busy(true);
            self.ui
                .set_vnt_status_text(self.tr("重启中", "Restarting", "再起動中").into());
            self.ui
                .set_vnt_detail_text(format!("正在应用端口映射：127.0.0.1:7777 → {host}").into());
            self.append_log(&format!(
                "[{}] 已选择无网卡房主：{}，准备重启 VNT 应用端口映射",
                core::now_text(),
                host
            ));
            true
        } else {
            self.vnt_restart_after_stop = None;
            false
        }
    }

    pub(super) fn cancel_vnt_host_selection(&mut self) {
        self.vnt_host_selection_pending = false;
        self.ui.set_show_vnt_host_dialog(false);
        self.append_log(&format!(
            "[{}] 无网卡模式：当前设备作为房主，不创建玩家端口映射",
            core::now_text()
        ));
    }

    fn vnt_mapping_port_conflicts(&self) -> Result<Vec<PortConflict>> {
        let conflicts = self.core.collect_port_conflicts()?;
        Ok(conflicts
            .into_iter()
            .filter(|conflict| {
                let protocol = conflict.protocol.to_ascii_uppercase();
                (protocol == "TCP"
                    && core::ONLINE_MAPPING_TCP_PORTS.contains(&(conflict.port as u16)))
                    || (protocol == "UDP"
                        && core::ONLINE_MAPPING_UDP_PORTS.contains(&(conflict.port as u16)))
            })
            .collect())
    }

    fn show_vnt_mapping_conflict_dialog(&mut self, conflicts: &[PortConflict]) {
        let message = format!(
            "{}\n{}\n\n{}",
            self.tr(
                "无网卡模式需要先占用本机 UDP 7777。",
                "No-adapter mode needs to reserve local UDP 7777 first.",
                "アダプターなしモードでは先にローカル UDP 7777 を確保する必要があります。",
            ),
            self.tr(
                "请先关闭游戏客户端或占用该端口的程序，选择房主成功后再进游戏连接 127.0.0.1:7777。",
                "Close the game client or the program using this port first. After host selection succeeds, start the game and connect to 127.0.0.1:7777.",
                "先にゲームクライアントまたはこのポートを使用中のプログラムを閉じてください。ホスト選択成功後にゲームを起動し、127.0.0.1:7777 に接続してください。",
            ),
            format_port_conflicts(conflicts)
        );
        self.append_log(&format!(
            "[{}] 无网卡映射端口被占用：{}",
            core::now_text(),
            format_port_conflicts(conflicts).replace('\n', "；")
        ));
        self.show_error_dialog(
            self.tr(
                "联机端口被占用",
                "Network Port Occupied",
                "通信ポート使用中",
            ),
            &message,
        );
    }

    /// 为 ListView 原地同步 VNT 服务器模型。
    pub(super) fn set_vnt_server_rows(&mut self, rows: Vec<VntServerRow>) {
        while self.vnt_server_model.row_count() > rows.len() {
            let _ = self
                .vnt_server_model
                .remove(self.vnt_server_model.row_count() - 1);
        }
        for (index, row) in rows.into_iter().enumerate() {
            if index < self.vnt_server_model.row_count() {
                self.vnt_server_model.set_row_data(index, row);
            } else {
                self.vnt_server_model.push(row);
            }
        }
    }

    /// 为 ListView 原地同步节点模型。
    pub(super) fn set_vnt_peer_rows(&mut self, rows: Vec<VntPeerRow>) {
        while self.vnt_peer_model.row_count() > rows.len() {
            let _ = self
                .vnt_peer_model
                .remove(self.vnt_peer_model.row_count() - 1);
        }
        for (index, row) in rows.into_iter().enumerate() {
            if index < self.vnt_peer_model.row_count() {
                self.vnt_peer_model.set_row_data(index, row);
            } else {
                self.vnt_peer_model.push(row);
            }
        }
    }

    /// 从当前 UI 收集联机页偏好设置。
    pub(super) fn current_vnt_prefs(&self) -> VntPrefs {
        let server_text = self.ui.get_vnt_server_text().trim().to_string();
        let mut server_options = Vec::new();
        for index in 0..self.vnt_server_option_model.row_count() {
            if let Some(option) = self.vnt_server_option_model.row_data(index) {
                let option = option.trim();
                if option.is_empty() {
                    continue;
                }
                if !server_options
                    .iter()
                    .any(|existing: &String| existing.eq_ignore_ascii_case(option))
                {
                    server_options.push(option.to_string());
                }
            }
        }
        if !server_text.is_empty()
            && !server_options
                .iter()
                .any(|existing| existing.eq_ignore_ascii_case(&server_text))
        {
            server_options.push(server_text.clone());
        }

        VntPrefs {
            server_text,
            server_options,
            username: self.ui.get_vnt_username().trim().to_string(),
            network_code: self.ui.get_vnt_network_code().trim().to_string(),
            password: self.ui.get_vnt_password().to_string(),
            no_tun: self.ui.get_vnt_no_tun(),
            no_punch: self.ui.get_vnt_no_punch(),
            compress: self.ui.get_vnt_compress(),
            rtx: self.ui.get_vnt_rtx(),
        }
    }

    /// 更新 GitHub 下载代理前缀，并立即保存到应用配置。
    pub(super) fn set_github_proxy_prefix(&mut self, proxy: String) {
        let normalized = core::normalize_github_proxy_prefix(&proxy);
        self.core.set_github_proxy_prefix(&normalized);
        self.app_prefs.github_proxy_prefix = normalized;
        self.sync_github_proxy_current_selection();
        self.save_app_prefs();
    }

    /// 更新本地代理软件地址，并立即保存到应用配置。
    pub(super) fn set_network_proxy_url(&mut self, proxy: String) {
        let normalized = core::normalize_network_proxy_url(&proxy);
        self.core.set_network_proxy_url(&normalized);
        self.app_prefs.network_proxy_url = normalized.clone();
        if self.ui.get_network_proxy_text().as_str() != normalized {
            self.ui.set_network_proxy_text(normalized.into());
        }
        self.save_app_prefs();
    }

    /// 从代理选择弹窗选中节点后写回设置并关闭弹窗。
    pub(super) fn select_github_proxy_from_dialog(&mut self, proxy: String) {
        if is_auto_select_github_proxy_url(&proxy) {
            self.auto_select_github_proxy_from_dialog();
            return;
        }
        let normalized = core::normalize_github_proxy_prefix(&proxy);
        self.ui.set_github_proxy_text(normalized.clone().into());
        self.set_github_proxy_prefix(normalized);
        self.ui.set_show_github_proxy_dialog(false);
    }

    /// 保存应用级偏好设置；失败只写日志，不打断用户操作。
    pub(super) fn save_app_prefs(&mut self) {
        self.app_prefs.launch_pve_server = self.ui.get_launch_pve_server();
        self.app_prefs.vnt = self.current_vnt_prefs();
        self.app_prefs.github_proxy_prefix =
            core::normalize_github_proxy_prefix(&self.ui.get_github_proxy_text());
        self.core
            .set_github_proxy_prefix(&self.app_prefs.github_proxy_prefix);
        self.app_prefs.network_proxy_url =
            core::normalize_network_proxy_url(&self.ui.get_network_proxy_text());
        self.core
            .set_network_proxy_url(&self.app_prefs.network_proxy_url);
        if let Err(error) = self.app_prefs.save(&self.core.installer_home) {
            self.append_log(&format!(
                "[{}] 应用配置保存失败：{}",
                core::now_text(),
                error
            ));
        }
    }
}

fn should_show_vnt_stop_error(reason: &str) -> bool {
    let reason = reason.trim();
    !reason.is_empty() && reason != "联机核心已停止" && reason != "已取消启动"
}
