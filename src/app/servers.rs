//! AppController 社区服务器列表刷新。

use super::*;

use anyhow::Context;

#[cfg(windows)]
use windows::Win32::{
    Foundation::{GlobalFree, HANDLE, HWND},
    System::{
        DataExchange::{CloseClipboard, EmptyClipboard, OpenClipboard, SetClipboardData},
        Memory::{GMEM_MOVEABLE, GlobalAlloc, GlobalLock, GlobalUnlock},
        Ole::CF_UNICODETEXT,
    },
};

impl AppController {
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
fn copy_text_to_clipboard(window: &slint::Window, text: &str) -> Result<()> {
    use slint::winit_030::{WinitWindowAccessor, winit::raw_window_handle::{HasWindowHandle, RawWindowHandle}};
    let owner = window.with_winit_window(|win| {
        match win.window_handle().ok()?.as_raw() {
            RawWindowHandle::Win32(handle) => Some(HWND(handle.hwnd.get() as *mut _)),
            _ => None,
        }
    }).flatten().context("无法取得工具箱窗口句柄")?;
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
fn copy_text_to_clipboard(_window: &slint::Window, _text: &str) -> Result<()> {
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
