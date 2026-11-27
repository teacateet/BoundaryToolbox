//! 玩家点复制或加入后才建临时组网；不读写高级网络的服务器、密码或端口。
use super::*;
use std::net::SocketAddrV4;
use std::time::{Duration, Instant};
use uuid::Uuid;

const WAIT: Duration = Duration::from_secs(45);

#[derive(Clone, Copy, PartialEq, Eq)]
enum Side {
    Host(u16),
    Guest(SocketAddrV4),
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Ready {
    Share(friend_invite::Invite),
    Join(SocketAddrV4),
}

pub(super) struct FriendSession {
    // 同一邀请可重试；每次尝试有独立标识，取消后的旧快照不能启动游戏或写剪贴板。
    attempt: Uuid,
    room: Uuid,
    side: Side,
    waiting: Option<Instant>,
    transport: VntSession,
    host_ip: Option<Ipv4Addr>,
}

fn network_options(room: Uuid, side: Side) -> VntLaunchOptions {
    use sha2::{Digest, Sha256};
    // 邀请持有者可进房，仅向中转登记派生的房号。已有的手动组网密码不会被复制。
    let fingerprint = format!("{:x}", Sha256::digest(room.as_bytes()));
    VntLaunchOptions {
        server_text: prefs::DEFAULT_VNT_SERVER.into(),
        username: match side {
            Side::Host(_) => "BRT Host",
            Side::Guest(_) => "BRT Guest",
        }
        .into(),
        network_code: fingerprint[..32].to_owned(),
        password: room.simple().to_string(),
        no_tun: true,
        port_mapping_host: match side {
            Side::Guest(peer) => Some(peer),
            Side::Host(_) => None,
        },
        compress: false,
        rtx: false,
        no_punch: false,
    }
}

impl FriendSession {
    fn observe(&mut self, snapshot: &vnt_platform::VntSnapshot) -> Option<Ready> {
        // 即使后台仍在重连，也绝不复用上一次的虚拟地址。
        self.host_ip = if snapshot.running && !snapshot.busy {
            vnt_controller::room::own_ip(&snapshot.virtual_ip).filter(Ipv4Addr::is_private)
        } else {
            None
        };
        let own = self.host_ip?;
        self.waiting?;
        let result = match self.side {
            Side::Host(port) => Ready::Share(friend_invite::Invite::Friend {
                room: self.room,
                peer: SocketAddrV4::new(own, port),
            }),
            Side::Guest(peer) => {
                if own == *peer.ip()
                    || !snapshot
                        .peers
                        .iter()
                        .any(|p| p.online && p.address.parse::<Ipv4Addr>().ok() == Some(*peer.ip()))
                {
                    return None;
                }
                let (no_adapter, mapped_host) = self.transport.game_route();
                Ready::Join(vnt_controller::room::game_target(peer, no_adapter, mapped_host).ok()?)
            }
        };
        self.waiting = None;
        Some(result)
    }
}

impl AppController {
    fn friend_notice(&self, zh: &'static str, en: &'static str, ja: &'static str) {
        self.ui.set_friend_room_notice(self.tr(zh, en, ja).into());
    }

    fn close_friend_connection(&mut self) {
        self.friend_room = None;
        self.ui.set_friend_room_working(false);
        self.ui.set_friend_room_connected(false);
    }

    pub(super) fn disconnect_friend_room(&mut self) {
        if self.friend_room.is_some() {
            self.close_friend_connection();
            self.friend_notice(
                "好友连接已断开。需要时可重新复制或加入邀请。",
                "Disconnected. Copy or join an invite to reconnect.",
                "切断しました。招待のコピーまたは参加で再接続できます。",
            );
        }
    }

    pub(super) fn close_friend_host(&mut self) {
        if self
            .friend_room
            .as_ref()
            .is_some_and(|r| matches!(r.side, Side::Host(_)))
        {
            self.close_friend_connection();
            self.ui.set_friend_room_notice("".into());
        }
    }

    fn can_start_friend_network(&mut self) -> bool {
        if self.vnt_session.is_some()
            || self.vnt_restart_after_stop.is_some()
            || self.ui.get_vnt_busy()
            || self.ui.get_vnt_running()
        {
            self.friend_notice(
                "已有高级组网。使用「高级选项」里的组网邀请，或自行断开后再用好友邀请。",
                "Custom network is active. Use its invite under More, or disconnect it first.",
                "詳細ネットワークに接続中です。「詳細」から招待するか、先に切断してください。",
            );
            return false;
        }
        true
    }

    fn begin_friend_network(&mut self, room: Uuid, side: Side) {
        let attempt = Uuid::new_v4();
        let tx = self.tx.clone();
        let sink = Arc::new(move |event| {
            let _ = tx.send(AppMessage::FriendRoomEvent(attempt, event));
        });
        match VntSession::start(network_options(room, side), sink) {
            Ok(transport) => {
                self.friend_room = Some(FriendSession {
                    attempt,
                    room,
                    side,
                    waiting: Some(Instant::now() + WAIT),
                    transport,
                    host_ip: None,
                });
                self.ui.set_friend_room_working(true);
                self.ui.set_friend_room_connected(true);
                match side {
                    Side::Host(_) => self.friend_notice(
                        "正在准备好友邀请，完成后会自动复制。",
                        "Preparing the invite. It will be copied when ready.",
                        "招待を準備中。完了後にコピーします。",
                    ),
                    Side::Guest(_) => self.friend_notice(
                        "正在连接好友房，找到房主后会自动进入。",
                        "Connecting. The game opens when the host is online.",
                        "接続中。ホストを確認したらゲームを開始します。",
                    ),
                }
            }
            Err(e) => self.friend_error(&e.to_string()),
        }
    }

    pub(super) fn share_friend_room(&mut self) {
        let Some(port) = self.open_room_port() else {
            return;
        };
        if self.ui.get_friend_room_working() {
            return;
        }
        if let Some(r) = self.friend_room.as_mut() {
            if r.side != Side::Host(port) {
                self.friend_notice(
                    "请先断开当前好友连接，再分享这间房。",
                    "Disconnect the current friend session first.",
                    "先に現在のフレンド接続を切断してください。",
                );
            } else if let Some(ip) = r.host_ip {
                let invite = friend_invite::Invite::Friend {
                    room: r.room,
                    peer: SocketAddrV4::new(ip, port),
                };
                self.finish_friend_share(invite);
            } else {
                r.waiting = Some(Instant::now() + WAIT);
                self.ui.set_friend_room_working(true);
                self.friend_notice(
                    "正在重新连接，完成后会复制邀请。",
                    "Reconnecting. The invite will be copied when ready.",
                    "再接続後に招待をコピーします。",
                );
            }
        } else if self.can_start_friend_network() {
            self.begin_friend_network(Uuid::new_v4(), Side::Host(port));
        }
    }

    pub(super) fn join_friend_room(&mut self, room: Uuid, peer: SocketAddrV4) {
        if self.active_fixed_room.is_some() || self.fixed_room_start.is_some() {
            self.friend_notice(
                "你正在开房。请先停止自己的房间，再加入朋友。",
                "Stop your own room before joining a friend.",
                "自分のルームを停止してから参加してください。",
            );
            return;
        }
        if let Some(r) = self.friend_room.as_mut() {
            if r.room != room || r.side != Side::Guest(peer) {
                self.friend_notice(
                    "已有好友连接。请先点「断开好友连接」，再加入新邀请。",
                    "Disconnect your friend session before joining another invite.",
                    "別の招待を使用するには先にフレンド接続を切断してください。",
                );
                return;
            }
            if r.waiting.is_none() {
                r.waiting = Some(Instant::now() + WAIT);
                self.ui.set_friend_room_working(true);
            }
            self.friend_notice(
                "正在确认房主在线，随后进入游戏。",
                "Waiting for the host before entering the game.",
                "ホストを確認してゲームに参加します。",
            );
            return;
        }
        if !self.can_start_friend_network() {
            return;
        }
        if let Err(e) = self.require_target() {
            self.show_error_dialog(
                self.tr("加入好友", "Join a friend", "フレンドに参加"),
                &format!("{e:#}"),
            );
            return;
        }
        // 房客的回环入口仅由本次映射占用。不终止或更改占用该端口的进程。
        if std::net::UdpSocket::bind(vnt_controller::room::LOCAL_GAME_ENTRY).is_err() {
            self.friend_notice("加入入口已占用。请先关闭本机旧房间或联机程序，再点「粘贴并加入」。",
                "Join port is in use. Close the old local room or network app, then paste the invite again.",
                "参加用ポートが使用中です。古いルームや通信アプリを閉じてから再度参加してください。");
            return;
        }
        self.begin_friend_network(room, Side::Guest(peer));
    }

    fn finish_friend_share(&mut self, invite: friend_invite::Invite) {
        if let Err(e) = servers::copy_text_to_clipboard(self.ui.window(), &invite.to_string()) {
            self.friend_notice(
                "剪贴板正在使用，请再点一次「复制好友邀请」。",
                "Clipboard is busy. Click Copy friend invite again.",
                "クリップボードを使用中です。もう一度コピーしてください。",
            );
            self.show_error_dialog(
                self.tr("复制邀请失败", "Could not copy invite", "コピー失敗"),
                &e.to_string(),
            );
        } else {
            self.friend_notice(
                "邀请已复制。发给朋友，然后点「进入房间」。",
                "Invite copied. Send it to your friend, then enter the room.",
                "招待をコピーしました。フレンドに送ってルームに入ってください。",
            );
        }
    }

    fn friend_error(&mut self, cause: &str) {
        self.close_friend_connection();
        self.friend_notice("好友连接未成功。确认房间仍开着，再试一次；同一 Wi-Fi 可改用局域网邀请。",
            "Connection failed. Check the room and retry, or use a LAN invite on the same Wi-Fi.", "接続できません。ルームを確認して再試行するか、同じ Wi-Fi では LAN 招待を使ってください。");
        self.show_error_dialog(
            self.tr(
                "好友房连接失败",
                "Friend room connection failed",
                "接続できません",
            ),
            cause,
        );
    }

    pub(super) fn check_friend_timeout(&mut self) {
        if self
            .friend_room
            .as_ref()
            .and_then(|r| r.waiting)
            .is_some_and(|until| Instant::now() >= until)
        {
            self.close_friend_connection();
            self.friend_notice("连接超时。请确认房主仍开着房间和工具箱，再重试；同一 Wi-Fi 可用局域网邀请。",
                "Timed out. Ask the host to keep the room and toolbox open, then retry. On the same Wi-Fi use a LAN invite.",
                "接続がタイムアウトしました。ホストのルームとツールを確認して再試行してください。同じ Wi-Fi では LAN 招待も使えます。");
        }
    }

    pub(super) fn friend_event(&mut self, attempt: Uuid, event: VntEvent) {
        self.check_friend_timeout();
        let Some(r) = self.friend_room.as_mut().filter(|r| r.attempt == attempt) else {
            return;
        };
        match event {
            VntEvent::Snapshot(s) => {
                if self.ui.get_busy() {
                    return;
                }
                match r.observe(&s) {
                    Some(Ready::Share(invite)) => {
                        self.ui.set_friend_room_working(false);
                        if let friend_invite::Invite::Friend { peer, .. } = invite {
                            if self.open_room_port() == Some(peer.port()) {
                                self.finish_friend_share(invite);
                            }
                        }
                    }
                    Some(Ready::Join(endpoint)) => {
                        self.ui.set_friend_room_working(false);
                        self.friend_notice(
                            "已连上房主，正在启动游戏。",
                            "Host connected. Starting the game.",
                            "ホストに接続しました。ゲームを開始します。",
                        );
                        self.start_launch(LaunchMode::Join(endpoint));
                    }
                    None => {}
                }
            }
            VntEvent::Failed(e) | VntEvent::Stopped(e) => self.friend_error(&e),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn session(room: Uuid, side: Side) -> FriendSession {
        FriendSession {
            attempt: Uuid::new_v4(),
            room,
            side,
            waiting: Some(Instant::now() + WAIT),
            transport: VntSession::without_network(&network_options(room, side)),
            host_ip: None,
        }
    }
    fn snapshot(own: &str, guest_view: &[(&str, bool)]) -> vnt_platform::VntSnapshot {
        vnt_platform::VntSnapshot {
            running: true,
            virtual_ip: own.into(),
            peers: guest_view
                .iter()
                .map(|(ip, online)| vnt_platform::VntPeer {
                    name: String::new(),
                    address: ip.to_string(),
                    detail: String::new(),
                    online: *online,
                })
                .collect(),
            ..vnt_platform::idle_snapshot()
        }
    }

    #[test]
    fn friend_invite_waits_for_room_ip_and_selected_peer_then_enters_only_once() {
        let key = Uuid::new_v4();
        let mut host = session(key, Side::Host(18777));
        assert_eq!(host.observe(&snapshot("-", &[])), None);
        let Ready::Share(invite) = host.observe(&snapshot("10.193.43.8/24", &[])).unwrap() else {
            panic!()
        };
        let Some(friend_invite::Invite::Friend { room, peer }) =
            friend_invite::Invite::parse(&invite.to_string())
        else {
            panic!()
        };
        assert_eq!(peer.port(), 18777);
        assert_eq!(host.observe(&snapshot("10.193.43.8/24", &[])), None);
        let host_opts = network_options(key, Side::Host(18777));
        let guest_opts = network_options(room, Side::Guest(peer));
        assert_eq!(host_opts.server_text, prefs::DEFAULT_VNT_SERVER);
        assert_eq!(guest_opts.network_code, host_opts.network_code);
        assert_eq!(guest_opts.password, host_opts.password);
        assert_ne!(host_opts.password, host_opts.network_code);
        assert!(host_opts.no_tun && guest_opts.no_tun && host_opts.port_mapping_host.is_none());
        assert_ne!(
            network_options(Uuid::new_v4(), Side::Host(18777)).network_code,
            guest_opts.network_code
        );
        let mut guest = session(room, Side::Guest(peer));
        assert_eq!(
            guest.observe(&snapshot(
                "10.193.43.6",
                &[("10.193.43.8", false), ("10.193.43.7", true)]
            )),
            None
        );
        assert_eq!(
            guest.observe(&snapshot("10.193.43.8", &[("10.193.43.8", true)])),
            None
        );
        let ready = snapshot("10.193.43.6", &[("10.193.43.8", true)]);
        assert_eq!(
            guest.observe(&ready),
            Some(Ready::Join(vnt_controller::room::LOCAL_GAME_ENTRY))
        );
        assert_eq!(guest.observe(&ready), None);
    }

    #[test]
    fn stale_callbacks_and_timeout_do_not_enter_another_friend_room_or_change_custom_settings()
    -> Result<()> {
        use slint::platform::software_renderer::{MinimalSoftwareWindow, RepaintBufferType};
        use slint::platform::{Platform, WindowAdapter};
        struct Offscreen(Rc<MinimalSoftwareWindow>);
        impl Platform for Offscreen {
            fn create_window_adapter(
                &self,
            ) -> std::result::Result<Rc<dyn WindowAdapter>, slint::PlatformError> {
                Ok(self.0.clone())
            }
        }
        slint::platform::set_platform(Box::new(Offscreen(MinimalSoftwareWindow::new(
            RepaintBufferType::NewBuffer,
        ))))?;
        let ui = AppWindow::new()?;
        ui.set_page(2);
        let controller = AppController::new(ui.clone_strong())?;
        let mut app = controller.borrow_mut();
        app.stop_background.store(true, Ordering::Relaxed);
        ui.set_vnt_network_code("private-existing-code".into());
        ui.set_vnt_password("private-existing-password".into());
        ui.set_vnt_server_text("127.0.0.1:32100".into());
        ui.set_vnt_running(true);
        let key = Uuid::new_v4();
        let peer: SocketAddrV4 = "10.193.43.8:18777".parse()?;
        app.join_friend_room(key, peer);
        assert!(app.friend_room.is_none() && ui.get_vnt_running());
        ui.set_vnt_running(false);
        let first = session(key, Side::Guest(peer));
        let old = first.attempt;
        app.friend_room = Some(first);
        app.disconnect_friend_room();
        let second = session(key, Side::Guest(peer));
        let active = second.attempt;
        app.friend_room = Some(second);
        ui.set_friend_room_working(true);
        app.friend_event(
            old,
            VntEvent::Snapshot(snapshot("10.193.43.6", &[("10.193.43.8", true)])),
        );
        assert_eq!(app.friend_room.as_ref().unwrap().attempt, active);
        assert!(ui.get_friend_room_working() && !ui.get_busy());
        app.friend_room.as_mut().unwrap().waiting = Some(Instant::now());
        app.friend_event(
            active,
            VntEvent::Snapshot(snapshot("10.193.43.6", &[("10.193.43.8", true)])),
        );
        assert!(app.friend_room.is_none() && !ui.get_friend_room_working() && !ui.get_busy());
        let failed = session(key, Side::Guest(peer));
        let failed_id = failed.attempt;
        app.friend_room = Some(failed);
        app.friend_event(
            failed_id,
            VntEvent::Failed("fixture registration failed".into()),
        );
        assert!(app.friend_room.is_none() && ui.get_app_dialog_error() && !ui.get_busy());
        assert_eq!(ui.get_vnt_network_code().as_str(), "private-existing-code");
        assert_eq!(ui.get_vnt_password().as_str(), "private-existing-password");
        assert_eq!(ui.get_vnt_server_text().as_str(), "127.0.0.1:32100");
        Ok(())
    }

    // BRT_TEST_FRIEND_SERVER 只在测试程序里读取；发行版邀请不可改变组网服务器。
    #[test]
    #[ignore = "requires a loopback VNTS test server"]
    fn two_real_clients_exchange_game_udp_through_an_invite() -> Result<()> {
        use std::net::UdpSocket;
        let server = std::env::var("BRT_TEST_FRIEND_SERVER")?.parse::<SocketAddrV4>()?;
        anyhow::ensure!(server.ip().is_loopback());
        let key = Uuid::new_v4();
        let game = UdpSocket::bind((Ipv4Addr::LOCALHOST, 0))?;
        game.set_read_timeout(Some(Duration::from_secs(18)))?;
        let port = game.local_addr()?.port();
        let start = |side| -> Result<(FriendSession, Receiver<VntEvent>)> {
            let mut o = network_options(key, side);
            // 只替换注册服务器；端口映射、口令和组网角色都来自玩家路径。
            o.server_text = server.to_string();
            let (tx, rx) = unbounded();
            let transport = VntSession::start(
                o,
                Arc::new(move |e| {
                    let _ = tx.send(e);
                }),
            )?;
            Ok((
                FriendSession {
                    attempt: Uuid::new_v4(),
                    room: key,
                    side,
                    transport,
                    waiting: Some(Instant::now() + WAIT),
                    host_ip: None,
                },
                rx,
            ))
        };
        let ready = |s: &mut FriendSession, rx: Receiver<VntEvent>| -> Result<Ready> {
            let end = Instant::now() + WAIT;
            loop {
                match rx.recv_deadline(end)? {
                    VntEvent::Snapshot(snap) => {
                        if let Some(action) = s.observe(&snap) {
                            return Ok(action);
                        }
                    }
                    VntEvent::Failed(e) | VntEvent::Stopped(e) => bail!(e),
                }
            }
        };
        let (mut host, h) = start(Side::Host(port))?;
        let Ready::Share(invite) = ready(&mut host, h)? else {
            bail!("missing host invite")
        };
        let Some(friend_invite::Invite::Friend { room, peer }) =
            friend_invite::Invite::parse(&invite.to_string())
        else {
            bail!("invalid issued invite")
        };
        anyhow::ensure!(room == key && peer.port() == port);
        let (mut guest, g) = start(Side::Guest(peer))?;
        let Ready::Join(entry) = ready(&mut guest, g)? else {
            bail!("guest has no game route")
        };
        let client = UdpSocket::bind((Ipv4Addr::LOCALHOST, 0))?;
        client.set_read_timeout(Some(Duration::from_secs(18)))?;
        let payload = key.as_bytes();
        let mut buffer = [0u8; 64];
        client.send_to(payload, entry)?;
        let (n, source) = game.recv_from(&mut buffer)?;
        anyhow::ensure!(&buffer[..n] == payload);
        game.send_to(b"host-reply", source)?;
        let (n, from) = client.recv_from(&mut buffer)?;
        anyhow::ensure!(&buffer[..n] == b"host-reply" && from == std::net::SocketAddr::V4(entry));
        Ok(())
    }
}
