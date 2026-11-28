//! AppController 社区服务器列表刷新。

use super::*;

use anyhow::Context;

#[cfg(windows)]
use windows::Win32::{
    Foundation::{GlobalFree, HANDLE, HGLOBAL, HWND},
    System::{
        DataExchange::{CloseClipboard, EmptyClipboard, GetClipboardData, OpenClipboard, SetClipboardData},
        Memory::{GMEM_MOVEABLE, GlobalAlloc, GlobalLock, GlobalSize, GlobalUnlock},
        Ole::CF_UNICODETEXT,
    },
};

impl AppController {
    pub(super) fn refresh_room_counts(&mut self) {
        if self.reading_room_counts || self.ui.get_page() != 2 { return; }
        let Some(target) = self.current_target.clone() else {
            self.ui.set_room_host_players(-1); self.ui.set_room_client_players(-1); return;
        };
        self.reading_room_counts = true;
        let hosted = self.active_fixed_room.clone();
        let tx = self.tx.clone();
        thread::spawn(move || {
            let (host, client) = core::fixed_host::player_count::current(&target, hosted.as_ref());
            let _ = tx.send(AppMessage::RoomCounts {target, hosted, host, client});
        });
    }

    pub(super) fn clear_friend_invite(&mut self) {
        self.invite_auto_route = None;
        self.ui.set_pending_friend_invite("".into());
    }

    pub(super) fn cancel_friend_invite(&mut self) {
        let applying = self.invite_auto_route.is_some() &&
            (self.vnt_restart_after_stop.is_some() || self.ui.get_vnt_busy());
        self.clear_friend_invite();
        if applying { self.stop_vnt(); }
    }

    pub(super) fn open_room_port(&mut self) -> Option<u16> {
        if self.ui.get_busy() || !self.ui.get_fixed_room_running() { return None; }
        let (target, port) = self.active_fixed_room.as_ref()?;
        let port = *port;
        match core::fixed_host::owned_room_listening(target, port) {
            Ok(true) => Some(port),
            Ok(false) => {
                self.close_friend_host();
                self.active_fixed_room = None;
                self.ui.set_fixed_room_running(false);
                self.ui.set_fixed_room_error(false);
                self.ui.set_fixed_room_status(self.tr("房间已关闭，请重新开启。", "Room closed. Start it again.", "ルームは停止しました。再起動してください。").into());
                None
            }
            Err(error) => {
                self.show_error_dialog(self.tr("无法确认房间状态","Could not check the room","ルームを確認できません"),&error.to_string());
                None
            }
        }
    }

    pub(super) fn join_own_room(&mut self) {
        if let Some(port) = self.open_room_port() {
            self.clear_friend_invite();
            self.start_launch(LaunchMode::Join(std::net::SocketAddrV4::new(Ipv4Addr::LOCALHOST, port)));
        }
    }

    pub(super) fn copy_room_invite(&mut self, vnt: bool) {
        let Some(port) = self.open_room_port() else { return };
        let virtual_ip = if self.ui.get_vnt_running() && !self.ui.get_vnt_busy() && self.vnt_session.is_some() {
            vnt_controller::room::own_ip(self.ui.get_vnt_ip_text().as_str())
        } else { None };
        let ip = if vnt { virtual_ip } else { friend_invite::local_lan_ip(virtual_ip) };
        let Some(invite) = ip.and_then(|ip| friend_invite::Invite::new(vnt, ip, port)) else {
            let message = if vnt { self.tr("请先连接房主所在的组网。","Connect to the host’s private network first.","先に仮想 LAN に接続してください。") }
                else { self.tr("没有可确定的局域网地址。请确认已连接同一 Wi-Fi 或有线网络，也可直接分享本机局域网 IP 和游戏端口。","No unique LAN address. Check Wi-Fi or Ethernet, or share your LAN IP and game port directly.","LAN アドレスを特定できません。Wi-Fi または有線 LAN を確認するか、IP とゲームポートを共有してください。") };
            self.show_error_dialog(self.tr("复制邀请","Copy invite","招待をコピー"), message);
            return;
        };
        if vnt && let Some((target, _)) = &self.active_fixed_room && let Some(session) = &mut self.vnt_session {
            if let Err(e) = session.share_owned_room(target.clone(), port) {
                self.show_error_dialog(self.tr("无法共享游戏端口","Could not share the game port","ゲームポートを共有できません"), &e.to_string());
                return;
            }
        }
        match copy_text_to_clipboard(self.ui.window(), &invite.to_string()) {
            Ok(()) => self.ui.set_fixed_room_status(format!("{} · UDP {port}", self.tr("邀请已复制","Invite copied","招待をコピーしました")).into()),
            Err(error) => self.show_error_dialog(self.tr("复制邀请失败","Could not copy invite","招待をコピーできません"),&error.to_string()),
        }
    }

    pub(super) fn join_clipboard_invite(&mut self) {
        if self.ui.get_busy() { return; }
        match read_clipboard_address(self.ui.window()) {
            Ok(s) => self.join_friend_input(&s),
            Err(_) => self.invalid_friend_input(),
        }
    }

    fn invalid_friend_input(&mut self) {
        self.clear_friend_invite();
        self.show_error_dialog(self.tr("加入好友","Join a friend","フレンドに参加"),
            self.tr("请复制好友发来的完整 BRT 邀请，或填写 IPv4 地址和游戏端口。","Copy the full BRT invite or enter an IPv4 address and game port.","BRT 招待をすべてコピーするか、IPv4 アドレスとゲームポートを入力してください。"));
    }

    pub(super) fn join_friend_input(&mut self, s: &str) {
        use friend_invite::Invite;
        if self.ui.get_busy() { return; }
        if self.ui.get_friend_room_working() { return; }
        self.clear_friend_invite();
        match Invite::parse(s) {
            Some(Invite::Friend { room, peer }) => self.join_friend_room(room, peer),
            Some(Invite::Lan(endpoint)) => self.start_launch(LaunchMode::Join(endpoint)),
            Some(invite @ Invite::Vnt(peer)) => {
                self.ui.set_pending_friend_invite(invite.to_string().into());
                self.ui.set_vnt_game_port(peer.port().to_string().into());
                self.ui.set_page(3);
                self.join_vnt_invite(peer);
            }
            None => match core::game_endpoint(s) {
                Some(endpoint) => self.start_launch(LaunchMode::Join(endpoint)),
                None => self.invalid_friend_input(),
            },
        }
    }

    pub(super) fn copy_feedback_group(&mut self) {
        if let Err(error) = copy_feedback(&self.ui, |text| copy_text_to_clipboard(self.ui.window(), text)) {
            self.show_error_dialog(
                self.tr("复制群号失败", "Could not copy group number", "グループ番号をコピーできません"),
                &error.to_string(),
            );
        }
    }

    /// 在工作线程中拉取远程社区服列表。
    pub(super) fn start_refresh_servers(&mut self) {
        if self.ui.get_servers_loading() {
            return;
        }

        self.ui.set_servers_loading(true);
        self.ui.set_server_status_text(
            self.tr(
                "服务器列表：刷新中...",
                "Server list: refreshing...",
                "サーバー一覧: 更新中...",
            )
            .into(),
        );
        let tx = self.tx.clone();
        thread::spawn(move || match fetch_servers() {
            Ok(rows) => {
                let _ = tx.send(AppMessage::ServerRows(rows));
            }
            Err(error) => {
                let _ = tx.send(AppMessage::ServerRowsFailed(error.to_string()));
            }
        });
    }

    /// 复制单行服务器地址。剪贴板写入很短，留在 UI 回调中执行，失败时给出明确反馈。
    pub(super) fn copy_server_address(&mut self, address: String) {
        let Some(address) = normalize_copyable_server_address(&address) else {
            self.show_error_dialog(
                self.tr(
                    "复制服务器地址",
                    "Copy Server Address",
                    "サーバーアドレスをコピー",
                ),
                self.tr(
                    "当前行没有可复制的服务器地址。",
                    "This row has no server address to copy.",
                    "この行にはコピーできるサーバーアドレスがありません。",
                ),
            );
            return;
        };

        match copy_text_to_clipboard(self.ui.window(), &address) {
            Ok(()) => {
                self.ui.set_status_text(
                    format!(
                        "{}：{}",
                        self.tr(
                            "已复制服务器地址",
                            "Server address copied",
                            "サーバーアドレスをコピーしました",
                        ),
                        address
                    )
                    .into(),
                );
                self.append_log(&format!(
                    "[{}] 已复制服务器地址：{}",
                    core::now_text(),
                    address
                ));
            }
            Err(error) => {
                self.append_log(&format!(
                    "[{}] 复制服务器地址失败：{}",
                    core::now_text(),
                    error
                ));
                self.show_error_dialog(
                    self.tr(
                        "复制服务器地址失败",
                        "Failed to Copy Server Address",
                        "サーバーアドレスのコピーに失敗",
                    ),
                    &error.to_string(),
                );
            }
        }
    }
}

pub(super) fn copy_feedback(ui: &AppWindow, write: impl FnOnce(&str) -> Result<()>) -> Result<()> {
    let result = write(ui.get_feedback_group().as_str());
    ui.set_feedback_copied(result.is_ok());
    result
}

fn normalize_copyable_server_address(address: &str) -> Option<String> {
    let address = address.trim();
    let (host, port) = address.rsplit_once(':')?;
    let host = host.trim();
    if host.is_empty() || host == "-" || host.contains(char::is_whitespace) {
        return None;
    }

    let port = port.trim().parse::<u16>().ok().filter(|port| *port != 0)?;
    Some(format!("{host}:{port}"))
}

#[cfg(windows)]
struct ClipboardGuard;

#[cfg(windows)]
impl Drop for ClipboardGuard {
    fn drop(&mut self) {
        let _ = unsafe { CloseClipboard() };
    }
}

#[cfg(windows)]
fn clipboard_window(window: &slint::Window) -> Result<HWND> {
    use slint::winit_030::{WinitWindowAccessor, winit::raw_window_handle::{HasWindowHandle, RawWindowHandle}};
    window.with_winit_window(|win| {
        match win.window_handle().ok()?.as_raw() {
            RawWindowHandle::Win32(handle) => Some(HWND(handle.hwnd.get() as *mut _)),
            _ => None,
        }
    }).flatten().context("无法取得工具箱窗口句柄")
}

// 仅在玩家点「粘贴并加入」时读取短文本，不记录剪贴板内容。
#[cfg(windows)]
fn read_clipboard_address(window: &slint::Window) -> Result<String> {
    let owner = clipboard_window(window)?;
    unsafe {
        OpenClipboard(Some(owner))?;
        let _clipboard = ClipboardGuard;
        let handle = HGLOBAL(GetClipboardData(u32::from(CF_UNICODETEXT.0))?.0);
        let units = GlobalSize(handle) / 2;
        if units == 0 { bail!("Clipboard text unavailable"); }
        let ptr = GlobalLock(handle).cast::<u16>();
        if ptr.is_null() { bail!("Clipboard text unavailable"); }
        let text = std::slice::from_raw_parts(ptr, units.min(193));
        let value = text.iter().position(|ch| *ch == 0).and_then(|n| String::from_utf16(&text[..n]).ok());
        let _ = GlobalUnlock(handle);
        value.context("Clipboard is not a short invite")
    }
}

#[cfg(not(windows))]
fn read_clipboard_address(_window: &slint::Window) -> Result<String> { bail!("Clipboard unavailable") }

#[cfg(windows)]
pub(super) fn copy_text_to_clipboard(window: &slint::Window, text: &str) -> Result<()> {
    let owner = clipboard_window(window)?;
    let wide_text = text
        .encode_utf16()
        .chain(std::iter::once(0))
        .collect::<Vec<_>>();
    let byte_len = wide_text
        .len()
        .checked_mul(std::mem::size_of::<u16>())
        .context("剪贴板文本过长")?;

    unsafe {
        OpenClipboard(Some(owner)).context("打开系统剪贴板失败")?;
        let _clipboard = ClipboardGuard;
        EmptyClipboard().context("清空系统剪贴板失败")?;

        let memory = GlobalAlloc(GMEM_MOVEABLE, byte_len).context("分配剪贴板内存失败")?;
        let locked = GlobalLock(memory).cast::<u16>();
        if locked.is_null() {
            let _ = GlobalFree(Some(memory));
            bail!("锁定剪贴板内存失败");
        }

        std::ptr::copy_nonoverlapping(wide_text.as_ptr(), locked, wide_text.len());
        let _ = GlobalUnlock(memory);

        let handle = HANDLE(memory.0);
        match SetClipboardData(u32::from(CF_UNICODETEXT.0), Some(handle)) {
            Ok(_) => Ok(()),
            Err(error) => {
                let _ = GlobalFree(Some(memory));
                Err(error).context("写入系统剪贴板失败")
            }
        }
    }
}

#[cfg(not(windows))]
pub(super) fn copy_text_to_clipboard(_window: &slint::Window, _text: &str) -> Result<()> {
    bail!("当前平台不支持复制到剪贴板");
}

#[cfg(test)]
mod tests {
    use super::normalize_copyable_server_address;

    #[test]
    fn match_endpoints_use_the_requested_game_port() {
        use crate::core::{game_endpoint, LaunchMode};
        for (address, expected) in [(" 192.168.2.8 ", "192.168.2.8:7777"),
                                     ("127.0.0.1:7778", "127.0.0.1:7778"),
                                     ("10.2.4.6:17777", "10.2.4.6:17777")] {
            let endpoint = game_endpoint(address).unwrap();
            assert_eq!(endpoint.to_string(), expected);
            assert!(LaunchMode::Join(endpoint).uses_login_server());
        }
        for invalid in ["", "-:0", "0.0.0.0:7777", "224.0.0.1", "255.255.255.255",
                        "127.0.0.1:0", "127.0.0.1:65536", "127.0.0.1 -debuglog", "http://127.0.0.1"] {
            assert!(game_endpoint(invalid).is_none(), "{invalid}");
        }
    }

    #[test]
    fn joining_leaves_the_listening_game_server_and_p2p_proxy_alone() {
        use crate::core::{game_endpoint, LaunchMode};
        let join = LaunchMode::Join(game_endpoint("127.0.0.1:7777").unwrap());
        for mode in [join, LaunchMode::Pvp] {
            for protocol in ["UDP", "TCP"] {
                assert!(!mode.binds_local_port(protocol, 7777));
                assert!(LaunchMode::Pve.binds_local_port(protocol, 7777));
            }
            for (protocol, port) in [("TCP", 6969), ("TCP", 8000), ("TCP", 9000), ("UDP", 9000)] {
                assert!(mode.binds_local_port(protocol, port));
            }
            assert!(!mode.binds_local_port("UDP", 8000));
        }
    }

    #[test]
    fn accepts_host_port_addresses() {
        assert_eq!(
            normalize_copyable_server_address("  192.168.1.2:3000  ").as_deref(),
            Some("192.168.1.2:3000")
        );
        assert_eq!(
            normalize_copyable_server_address("example.org:65535").as_deref(),
            Some("example.org:65535")
        );
    }

    #[test]
    fn rejects_placeholder_or_invalid_addresses() {
        for address in [
            "",
            "-",
            "等待接口返回数据",
            "-:3000",
            "server only",
            "1.2.3.4:0",
        ] {
            assert!(normalize_copyable_server_address(address).is_none());
        }
    }
}
