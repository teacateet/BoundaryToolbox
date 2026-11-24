use super::*;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum RoomAction {
    Start,
    StartPve,
    Stop,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum RoomOutcome {
    Open,
    Closed,
    AlreadyClosed,
    Cancelled,
}

fn room_port(text: &str) -> Option<u16> {
    let text = text.trim();
    if !text.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    text.parse::<u16>()
        .ok()
        .filter(|port| *port >= 1024 && ![6969, 8000, 9000].contains(port))
}

impl RoomAction {
    fn mode_prefix(self) -> &'static str {
        match self {
            Self::Start => "PvP · ",
            Self::StartPve => "PvE · ",
            Self::Stop => "",
        }
    }

    fn execute(self, target: &Path, port: u16, start: &core::fixed_host::StartAttempt) -> Result<RoomOutcome> {
        match self {
            Self::Start | Self::StartPve => Ok(if core::fixed_host::start_room(target, port, self == Self::StartPve, start)? {
                RoomOutcome::Open
            } else { RoomOutcome::Cancelled }),
            Self::Stop => match core::fixed_host::stop(target, port)? {
                true => Ok(RoomOutcome::Closed),
                false => Ok(RoomOutcome::AlreadyClosed),
            },
        }
    }
}

impl AppController {
    pub(super) fn request_fixed_room(&mut self, action: RoomAction) {
        if action == RoomAction::Stop {
            if let Some(start) = self.fixed_room_start.as_ref() {
                if start.cancel() {
                    self.ui.set_fixed_room_can_cancel(false);
                    self.ui.set_fixed_room_status(self.tr("正在取消建房…", "Cancelling room…", "起動をキャンセル中…").into());
                }
                return;
            }
            if !self.ui.get_busy() && self.ui.get_fixed_room_error() && self.active_fixed_room.is_none() {
                self.ui.set_fixed_room_error(false);
                self.ui.set_fixed_room_status(self.tr("未开房", "No room", "未起動").into());
                return;
            }
        }
        if self.ui.get_busy() || (action != RoomAction::Stop && self.active_fixed_room.is_some()) {
            return;
        }
        // 本次开启的房间固定使用原目录和端口；重启工具箱后也可按选定目录与端口关闭。
        let (target, port) = if let Some(room) = &self.active_fixed_room {
            room.clone()
        } else {
            let Some(port) = room_port(self.ui.get_fixed_room_port().as_str()) else {
                let message = self.tr(
                    "使用 1024 至 65535 的游戏端口，6969、8000、9000 为保留端口",
                    "Use 1024 to 65535. Ports 6969, 8000 and 9000 are reserved.",
                    "1024～65535 を指定してください。6969、8000、9000 は予約済みです",
                );
                self.ui.set_fixed_room_error(true);
                self.ui.set_fixed_room_status(message.into());
                return;
            };
            match self.require_target() {
                Ok(target) => (target, port),
                Err(error) => {
                    self.fixed_room_failure(action, port, &format!("{error:#}"));
                    return;
                }
            }
        };
        self.ui.set_fixed_room_port(port.to_string().into());
        self.ui.set_fixed_room_error(false);
        self.ui.set_busy(true);
        let start = Arc::new(core::fixed_host::StartAttempt::default());
        if action != RoomAction::Stop {
            self.fixed_room_start = Some(start.clone());
            self.ui.set_fixed_room_can_cancel(true);
        }
        let status = match action {
            RoomAction::Start | RoomAction::StartPve => self.tr("正在启动", "Starting", "起動中"),
            RoomAction::Stop => self.tr("正在关闭", "Stopping", "停止中"),
        };
        self.ui
            .set_fixed_room_status(format!("{}{status} · UDP {port}", action.mode_prefix()).into());
        let tx = self.tx.clone();
        thread::spawn(move || {
            let result = action
                .execute(&target, port, &start)
                .map_err(|error| format!("{error:#}"));
            let _ = tx.send(AppMessage::FixedRoomFinished {
                target,
                port,
                action,
                result,
            });
        });
    }

    pub(super) fn finish_fixed_room(
        &mut self,
        target: PathBuf,
        port: u16,
        action: RoomAction,
        result: std::result::Result<RoomOutcome, String>,
    ) {
        self.fixed_room_start = None;
        self.ui.set_fixed_room_can_cancel(false);
        self.ui.set_busy(false);
        let outcome = match result {
            Ok(outcome) => outcome,
            Err(error) => {
                self.fixed_room_failure(action, port, &error);
                return;
            }
        };
        self.active_fixed_room = (outcome == RoomOutcome::Open).then_some((target, port));
        self.ui
            .set_fixed_room_running(self.active_fixed_room.is_some());
        let state = match outcome {
            RoomOutcome::Open => self.tr("房间已开启", "Room open", "ルーム起動済み"),
            RoomOutcome::Closed => self.tr("房间已关闭", "Room closed", "ルーム停止済み"),
            RoomOutcome::Cancelled => self.tr("建房已取消，可以重试", "Cancelled. Ready to retry", "キャンセルしました。再試行できます"),
            RoomOutcome::AlreadyClosed => self.tr(
                "此端口没有运行中的固定房间",
                "No managed room on this port",
                "このポートに起動中の固定ルームはありません",
            ),
        };
        let status = format!("{}{state} · UDP {port}", action.mode_prefix());
        self.ui.set_fixed_room_error(false);
        self.ui.set_fixed_room_status(status.clone().into());
        self.append_log(&format!("[{}] {status}", core::now_text()));
    }

    fn fixed_room_failure(&mut self, action: RoomAction, port: u16, error: &str) {
        let reason = match action {
            RoomAction::Start | RoomAction::StartPve => self.tr("房间启动失败", "Room start failed", "ルームの起動に失敗"),
            RoomAction::Stop => self.tr("房间关闭失败", "Room stop failed", "ルームの停止に失敗"),
        };
        let status = format!("{}{reason} · UDP {port}", action.mode_prefix());
        self.ui.set_fixed_room_error(true);
        self.ui.set_fixed_room_status(status.clone().into());
        self.append_log(&format!("[{}] {status}：{error}", core::now_text()));
        self.show_error_dialog(&status, error);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use slint::platform::software_renderer::{MinimalSoftwareWindow, RepaintBufferType};
    use slint::platform::{Platform, PointerEventButton, WindowAdapter, WindowEvent};
    use std::net::UdpSocket;
    use std::time::Instant;

    #[test]
    fn validates_udp_port_before_dispatch() {
        for (value, port) in [
            ("1024", 1024),
            (" 7777 ", 7777),
            ("18777", 18777),
            ("65535", 65535),
        ] {
            assert_eq!(room_port(value), Some(port));
        }
        for value in [
            "",
            "0",
            "1023",
            "6969",
            "8000",
            "9000",
            "65536",
            "7777.0",
            "+7777",
            "-1",
            "127.0.0.1:7777",
        ] {
            assert_eq!(room_port(value), None);
        }
    }

    struct Offscreen(Rc<MinimalSoftwareWindow>);

    impl Platform for Offscreen {
        fn create_window_adapter(
            &self,
        ) -> std::result::Result<Rc<dyn WindowAdapter>, slint::PlatformError> {
            Ok(self.0.clone())
        }
    }

    fn paint(
        window: &MinimalSoftwareWindow,
        w: u32,
        h: u32,
    ) -> slint::SharedPixelBuffer<slint::Rgb8Pixel> {
        window.set_size(slint::PhysicalSize::new(w, h));
        window.request_redraw();
        let mut buffer = slint::SharedPixelBuffer::new(w, h);
        assert!(window.draw_if_needed(|renderer| {
            renderer.render(buffer.make_mut_slice(), w as usize);
        }));
        buffer
    }

    fn click(window: &MinimalSoftwareWindow, x: f32, y: f32) {
        let position = slint::LogicalPosition::new(x, y);
        let button = PointerEventButton::Left;
        window.dispatch_event(WindowEvent::PointerPressed { position, button });
        window.dispatch_event(WindowEvent::PointerReleased { position, button });
    }

    fn wait_for_room(controller: &Rc<RefCell<AppController>>, ui: &AppWindow) {
        let deadline = Instant::now() + Duration::from_secs(8);
        while ui.get_busy() {
            assert!(Instant::now() < deadline, "Fixed room worker timed out");
            controller.borrow_mut().drain_messages();
            thread::sleep(Duration::from_millis(10));
        }
    }

    // 离屏复验：cargo test --offline --locked --bin BRT app::fixed_room::tests
    #[test]
    fn offscreen_controls_handle_udp_conflict_and_room_results() -> Result<()> {
        let window = MinimalSoftwareWindow::new(RepaintBufferType::NewBuffer);
        slint::platform::set_platform(Box::new(Offscreen(window.clone())))?;
        let ui = AppWindow::new()?;
        ui.set_page(2);
        let controller = AppController::new(ui.clone_strong())?;
        // 固定列表状态，避免切换页签时的公共服务器请求和本地房间回执竞速。
        ui.set_servers_loading(true);
        ui.set_server_status_text("List refresh pending".into());
        controller
            .borrow()
            .stop_background
            .store(true, Ordering::Relaxed);
        AppController::bind_callbacks(&controller);
        ui.set_language(1);
        ui.show()?;
        assert_eq!(ui.get_fixed_room_port().as_str(), "7777");
        let buffer = paint(&window, 980, 600);
        let colors: std::collections::BTreeSet<_> = buffer
            .as_bytes()
            .chunks_exact(3)
            .step_by(31)
            .map(|p| [p[0], p[1], p[2]])
            .collect();
        assert!(colors.len() > 50, "Expected a rendered server page");
        ui.set_fixed_room_port("0".into());
        let start = ui.invoke_room_action_center(true);
        click(&window, start.x, start.y);
        assert!(
            ui.get_fixed_room_error(),
            "Start button did not validate the port"
        );
        assert!(!ui.get_busy());

        let dir = std::env::temp_dir().join(format!("brt-room-ui-{}", uuid::Uuid::new_v4()));
        let target = dir.join("Win64");
        fs::create_dir_all(target.join("ServerLauncher"))?;
        let run = (|| -> Result<()> {
            fs::write(target.join(core::GAME_EXE), [])?;
            fs::write(
                target.join("ServerLauncher/ProjectReboundServerLauncher.exe"),
                [],
            )?;
            controller
                .borrow_mut()
                .set_current_target(Some(target.clone()), "");
            let foreign = UdpSocket::bind(("127.0.0.1", 0))?;
            let port = foreign.local_addr()?.port();
            ui.set_fixed_room_port(port.to_string().into());
            let before = ui.get_server_status_text();
            let center = ui.invoke_room_mode_center(true);
            click(&window, center.x, center.y);
            assert!(ui.get_fixed_room_pve());
            let start = ui.invoke_room_action_center(true);
            click(&window, start.x, start.y);
            assert!(ui.get_busy());
            assert!(ui.get_fixed_room_status().contains(&port.to_string()));
            assert!(ui.get_fixed_room_status().contains("PvE"));
            wait_for_room(&controller, &ui);
            assert!(ui.get_fixed_room_error() && ui.get_app_dialog_error());
            assert!(ui.get_app_dialog_text().contains(&format!("UDP {port}")));
            controller.borrow_mut().hide_app_dialog();
            assert_eq!(ui.get_server_status_text(), before);
            paint(&window, 980, 600);
            let back = ui.invoke_room_action_center(false);
            click(&window, back.x, back.y);
            assert!(!ui.get_busy(), "Returning from failure must not stop unrelated processes");
            assert!(!ui.get_fixed_room_running() && !ui.get_fixed_room_error());
            let peer = UdpSocket::bind(("127.0.0.1", 0))?;
            peer.send_to(b"alive", foreign.local_addr()?)?;
            foreign.set_read_timeout(Some(Duration::from_secs(1)))?;
            let mut received = [0; 5];
            foreign.recv_from(&mut received)?;
            assert_eq!(&received, b"alive");

            // 工作线程尚未回报时，真实页面的取消按钮应接收点击；第二次点击不能重复取消。
            let attempt = Arc::new(core::fixed_host::StartAttempt::default());
            controller.borrow_mut().fixed_room_start = Some(attempt.clone());
            ui.set_busy(true);
            ui.set_fixed_room_can_cancel(true);
            paint(&window, 980, 600);
            let cancel = ui.invoke_room_action_center(false);
            click(&window, cancel.x, cancel.y);
            assert!(!ui.get_fixed_room_can_cancel() && !attempt.cancel());
            assert!(ui.get_busy());
            assert_eq!(ui.get_fixed_room_status().as_str(),
                controller.borrow().tr("正在取消建房…", "Cancelling room…", "起動をキャンセル中…"));
            controller.borrow_mut().finish_fixed_room(
                target.clone(), port, RoomAction::StartPve, Ok(RoomOutcome::Cancelled));
            assert!(!ui.get_busy() && !ui.get_fixed_room_error());

            // 注册失败与主动取消同一拍到达，取消后不应再弹“启动失败”。
            ui.set_vnt_stopping(true);
            controller.borrow_mut().apply_vnt_event(VntEvent::Failed("registration closed".into()));
            assert!(!ui.get_vnt_stopping() && !ui.get_vnt_busy() && !ui.get_show_app_dialog());

            // 成功回执与系统端口冲突分别覆盖 UI 消费队列和核心工作线程。
            let tx = controller.borrow().tx.clone();
            for (action, outcome, open) in [
                (RoomAction::Start, RoomOutcome::Open, true),
                (RoomAction::Stop, RoomOutcome::Closed, false),
                (RoomAction::StartPve, RoomOutcome::Open, true),
                (RoomAction::Stop, RoomOutcome::Closed, false),
            ] {
                let previous = ui.get_fixed_room_status();
                tx.send(AppMessage::FixedRoomFinished {
                    target: target.clone(),
                    port,
                    action,
                    result: Ok(outcome),
                })?;
                controller.borrow_mut().drain_messages();
                assert_eq!(ui.get_fixed_room_running(), open);
                assert!(!ui.get_fixed_room_error());
                assert!(ui.get_fixed_room_status().contains(&port.to_string()));
                assert_ne!(ui.get_fixed_room_status(), previous);
                if action == RoomAction::StartPve {
                    assert!(ui.get_fixed_room_status().contains("PvE"));
                }
                paint(&window, 1260, 780);
            }
            Ok(())
        })();
        ui.hide()?;
        fs::remove_dir_all(dir)?;
        run
    }
}
