//! AppController 生命周期和 Slint 回调绑定。

use super::*;

impl AppController {
    /// 创建模型、日志文件、核心服务和初始 UI 状态。
    pub(super) fn new(ui: AppWindow) -> Result<Rc<RefCell<Self>>> {
        let (tx, rx) = unbounded();
        // 来自 Core 的日志可能来自工作线程，因此和其它后台结果一样走 UI 安全的 channel。
        let log_tx = tx.clone();
        let logger = Arc::new(move |message: String| {
            let _ = log_tx.send(AppMessage::Log(message));
        });

        let core = Arc::new(InstallerCore::new(logger)?);
        let active_page = Arc::new(AtomicI32::new(0));
        let port_target = Arc::new(RwLock::new(None));
        let logs_dir = core.installer_home.join("logs");
        fs::create_dir_all(&logs_dir)?;
        let session_log_path = logs_dir.join(format!(
            "installer_{}.log",
            chrono::Local::now().format("%Y%m%d_%H%M%S")
        ));
        let mut session_log_file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&session_log_path)?;
        writeln!(
            session_log_file,
            "[{}] 运行目录：{}",
            core::now_text(),
            core.runtime_dir.display()
        )?;
        writeln!(
            session_log_file,
            "[{}] 载荷目录：{}",
            core::now_text(),
            core.payload_label()
        )?;
        writeln!(
            session_log_file,
            "[{}] 安装器目录：{}",
            core::now_text(),
            core.installer_home.display()
        )?;
        writeln!(
            session_log_file,
            "[{}] 会话日志：{}",
            core::now_text(),
            session_log_path.display()
        )?;
        let is_admin = core::is_running_as_administrator();
        writeln!(
            session_log_file,
            "[{}] 管理员模式：{}",
            core::now_text(),
            if is_admin { "是" } else { "否" }
        )?;
        let mut app_prefs = match AppPrefs::load(&core.installer_home) {
            Ok(prefs) => prefs,
            Err(error) => {
                let backup_text = match AppPrefs::preserve_invalid(&core.installer_home) {
                    Ok(Some(path)) => format!("；已备份到 {}", path.display()),
                    Ok(None) => String::new(),
                    Err(backup_error) => format!("；备份损坏配置失败：{backup_error}"),
                };
                writeln!(
                    session_log_file,
                    "[{}] 应用配置读取失败，使用默认值：{}{}",
                    core::now_text(),
                    error,
                    backup_text
                )?;
                AppPrefs::default()
            }
        };
        app_prefs.language = i18n::normalize_language_preference(app_prefs.language);
        app_prefs.github_proxy_prefix =
            core::normalize_github_proxy_prefix(&app_prefs.github_proxy_prefix);
        app_prefs.network_proxy_url =
            core::normalize_network_proxy_url(&app_prefs.network_proxy_url);
        core.set_github_proxy_prefix(&app_prefs.github_proxy_prefix);
        core.set_network_proxy_url(&app_prefs.network_proxy_url);
        let language_mode = app_prefs.language;
        let language = i18n::resolve_language(language_mode);
        let github_proxy_prefix = app_prefs.github_proxy_prefix.clone();
        let network_proxy_url = app_prefs.network_proxy_url.clone();
        let vnt_prefs = app_prefs.vnt.clone();
        ui.set_language(language);
        ui.set_language_mode(language_mode);
        for steam in core::pathing::steam_registry_paths() {
            let artwork = steam.join("appcache/librarycache").join(core::APP_ID).join("library_hero.jpg");
            if let Ok(image) = slint::Image::load_from_path(&artwork) {
                ui.set_library_art(image);
                break;
            }
        }

        // 对 Slint 模型只创建一次，后续原地更新，避免破坏已有 ListView 绑定。
        let port_model = Rc::new(VecModel::from(
            MONITORED_PORTS
                .iter()
                .map(|(protocol, port)| PortRow {
                    label: SharedString::from(format!("{}/{}", protocol, port)),
                    detail: SharedString::from(i18n::tr(
                        language,
                        "检测中...",
                        "Checking...",
                        "確認中...",
                    )),
                    protocol: SharedString::from(*protocol),
                    port: i32::from(*port),
                    pid: 0,
                    occupied: false,
                    expected: false,
                })
                .collect::<Vec<_>>(),
        ));
        let drive_model = Rc::new(VecModel::<DriveRow>::default());
        let server_model = Rc::new(VecModel::from(vec![server_placeholder_row(
            i18n::tr(
                language,
                "正在加载服务器列表",
                "Loading server list",
                "サーバー一覧を読み込み中",
            ),
            i18n::tr(
                language,
                "等待接口返回数据",
                "Waiting for the API response",
                "API 応答待ち",
            ),
            language,
        )]));
        let github_proxy_model = Rc::new(VecModel::from(initial_github_proxy_rows(
            &github_proxy_prefix,
            language,
        )));
        let vnt_server_option_model = Rc::new(VecModel::from(
            vnt_prefs
                .server_options
                .iter()
                .map(|server| SharedString::from(server.as_str()))
                .collect::<Vec<_>>(),
        ));
        let vnt_server_model = Rc::new(VecModel::from(vnt_server_placeholder_rows(language)));
        let vnt_peer_model = Rc::new(VecModel::from(vnt_placeholder_rows(language)));
        ui.set_port_rows(ModelRc::from(port_model.clone()));
        ui.set_drive_rows(ModelRc::from(drive_model.clone()));
        ui.set_server_rows(ModelRc::from(server_model.clone()));
        ui.set_github_proxy_rows(ModelRc::from(github_proxy_model.clone()));
        ui.set_vnt_server_options(ModelRc::from(vnt_server_option_model.clone()));
        ui.set_vnt_server_rows(ModelRc::from(vnt_server_model.clone()));
        ui.set_vnt_peer_rows(ModelRc::from(vnt_peer_model.clone()));
        ui.set_payload_label(core.payload_label().into());
        ui.set_detected_text(
            i18n::tr(
                language,
                "正在检测 Steam 安装目录...",
                "Detecting Steam install path...",
                "Steam のインストール先を検出中...",
            )
            .into(),
        );
        ui.set_target_text(
            i18n::tr(
                language,
                "未解析到有效的安装目录",
                "No valid install path resolved",
                "有効なインストール先が見つかりません",
            )
            .into(),
        );
        ui.set_status_text(
            format!(
                "{} / {}",
                i18n::tr(language, "准备就绪", "Ready", "準備完了"),
                APP_VERSION
            )
            .into(),
        );
        ui.set_launch_pve_server(app_prefs.launch_pve_server);
        ui.set_process_status_text(
            i18n::tr(
                language,
                "运行进程：未检测",
                "Runtime processes: not checked",
                "実行中プロセス: 未確認",
            )
            .into(),
        );
        ui.set_system_status_text(
            i18n::tr(
                language,
                "系统检查：未检测",
                "System check: not checked",
                "システムチェック: 未確認",
            )
            .into(),
        );
        ui.set_system_status_warning(false);
        ui.set_show_logs(false);
        ui.set_busy(false);
        ui.set_is_admin(is_admin);
        ui.set_pulse(false);
        ui.set_install_progress_visible(false);
        ui.set_install_progress_cancelable(true);
        ui.set_install_progress_dialog_title(
            i18n::tr(language, "任务进度", "Task Progress", "タスク進捗").into(),
        );
        ui.set_install_progress_value(0.0);
        ui.set_install_progress_percent("0%".into());
        ui.set_install_progress_title("".into());
        ui.set_install_progress_detail("".into());
        ui.set_install_progress_detail_lines(1);
        ui.set_show_github_proxy_dialog(false);
        ui.set_has_target(false);
        ui.set_servers_loading(false);
        ui.set_server_status_text(
            i18n::tr(
                language,
                "服务器列表：未刷新",
                "Server list: not refreshed",
                "サーバー一覧: 未更新",
            )
            .into(),
        );
        ui.set_update_checking(false);
        ui.set_update_status_text(
            i18n::tr(
                language,
                "更新：未检查",
                "Update: not checked",
                "更新: 未確認",
            )
            .into(),
        );
        ui.set_github_proxy_text(github_proxy_prefix.into());
        ui.set_network_proxy_text(network_proxy_url.into());
        ui.set_github_proxy_loading(false);
        ui.set_github_proxy_status_text(
            i18n::tr(
                language,
                "代理列表：未刷新",
                "Proxy list: not refreshed",
                "プロキシ一覧: 未更新",
            )
            .into(),
        );
        ui.set_show_drive_dialog(false);
        ui.set_show_app_dialog(false);
        ui.set_show_vnt_host_dialog(false);
        ui.set_app_dialog_confirm(false);
        ui.set_app_dialog_input(false);
        ui.set_app_dialog_error(false);
        ui.set_app_dialog_text_lines(1);
        ui.set_app_dialog_title("".into());
        ui.set_app_dialog_text("".into());
        ui.set_app_dialog_input_text("".into());
        ui.set_app_dialog_primary_text(i18n::tr(language, "确定", "OK", "OK").into());
        ui.set_app_dialog_secondary_text(i18n::tr(language, "取消", "Cancel", "キャンセル").into());
        ui.set_app_dialog_tertiary_text("".into());
        ui.set_app_dialog_has_tertiary(false);
        ui.set_vnt_server_text(vnt_prefs.server_text.into());
        ui.set_vnt_new_server_text("".into());
        ui.set_vnt_username(vnt_prefs.username.into());
        ui.set_vnt_network_code(vnt_prefs.network_code.into());
        ui.set_vnt_password(vnt_prefs.password.into());
        ui.set_vnt_no_tun(vnt_prefs.no_tun);
        ui.set_vnt_no_punch(vnt_prefs.no_punch);
        ui.set_vnt_compress(vnt_prefs.compress);
        ui.set_vnt_rtx(vnt_prefs.rtx);
        ui.set_vnt_busy(false);
        ui.set_vnt_running(false);
        apply_vnt_idle_to_ui(&ui, language);

        let controller = Rc::new(RefCell::new(Self {
            ui,
            core,
            tx,
            rx,
            stop_background: Arc::new(AtomicBool::new(false)),
            active_page,
            port_target,
            session_log_file,
            current_target: None,
            active_fixed_room: None,
            fixed_room_start: None,
            drive_model,
            port_model,
            server_model,
            github_proxy_model,
            vnt_server_option_model,
            vnt_server_model,
            vnt_peer_model,
            vnt_session: None,
            vnt_host_selection_pending: false,
            invite_auto_route: None,
            vnt_restart_after_stop: None,
            system_report: None,
            app_prefs,
            is_admin,
            install_cancel: None,
            auto_select_github_proxy_after_refresh: false,
            pending_dialog_action: PendingDialogAction::None,
        }));

        // 端口监控会立即启动；目标目录相关检查只在用户执行诊断操作时运行。
        spawn_port_thread(
            controller.borrow().core.clone(),
            controller.borrow().tx.clone(),
            controller.borrow().stop_background.clone(),
            controller.borrow().active_page.clone(),
            controller.borrow().port_target.clone(),
        );
        Ok(controller)
    }

    /// 将所有 Slint 回调绑定到控制器。
    pub(super) fn bind_callbacks(controller: &Rc<RefCell<Self>>) {
        let app = controller.borrow().ui.clone_strong();

        {
            let controller = Rc::clone(controller);
            app.on_page_changed(move |page| {
                controller.borrow_mut().on_page_changed(page);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_language_changed(move |language| {
                controller.borrow_mut().set_language(language);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_github_proxy_changed(move |proxy| {
                controller
                    .borrow_mut()
                    .set_github_proxy_prefix(proxy.to_string());
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_network_proxy_changed(move |proxy| {
                controller
                    .borrow_mut()
                    .set_network_proxy_url(proxy.to_string());
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_open_github_proxy_dialog_clicked(move || {
                let controller = controller.borrow_mut();
                controller.ui.set_show_github_proxy_dialog(true);
                if controller.ui.get_tutorial_active() && controller.ui.get_tutorial_step() == 2 {
                    controller.ui.set_tutorial_step(3);
                }
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_github_proxy_selected(move |proxy| {
                controller
                    .borrow_mut()
                    .select_github_proxy_from_dialog(proxy.to_string());
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_github_proxy_dialog_cancelled(move || {
                controller.borrow().ui.set_show_github_proxy_dialog(false);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_refresh_github_proxies_clicked(move || {
                controller
                    .borrow_mut()
                    .start_refresh_github_proxy_list(false);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_manual_path_changed(move |text| {
                controller
                    .borrow_mut()
                    .on_manual_path_changed(text.to_string());
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_browse_clicked(move || {
                controller.borrow_mut().browse_path();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_redetect_clicked(move || {
                controller.borrow_mut().refresh_target_from_detection(false);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_install_clicked(move || {
                controller.borrow_mut().start_install();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_community_source_clicked(move || {
                let url = core::community_install::REPOSITORY;
                if std::process::Command::new("explorer.exe").arg(url).spawn().is_err() {
                    controller.borrow_mut().show_error_dialog("ProjectRebound", url);
                }
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_install_progress_closed(move || {
                let controller = controller.borrow();
                if !controller.ui.get_busy() {
                    controller.ui.set_install_progress_visible(false);
                }
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_install_progress_cancelled(move || {
                controller.borrow_mut().cancel_install();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_tutorial_target_clicked(move |step| {
                controller.borrow_mut().handle_tutorial_target_clicked(step);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_tutorial_next_clicked(move || {
                controller.borrow_mut().handle_tutorial_next_clicked();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_tutorial_skip_clicked(move || {
                controller.borrow_mut().handle_tutorial_skip_clicked();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_tutorial_finish_clicked(move || {
                controller.borrow_mut().handle_tutorial_finish_clicked();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_uninstall_clicked(move || {
                controller.borrow_mut().start_uninstall();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_launch_client_clicked(move || {
                let mode = if controller.borrow().ui.get_launch_pve_server() {
                    LaunchMode::Pve
                } else {
                    LaunchMode::Pvp
                };
                controller.borrow_mut().start_launch(mode);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_launch_pve_server_changed(move |enabled| {
                let mut controller = controller.borrow_mut();
                controller.app_prefs.launch_pve_server = enabled;
                controller.save_app_prefs();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_detect_processes_clicked(move || {
                controller.borrow_mut().start_detect_processes();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_stop_processes_clicked(move || {
                controller.borrow_mut().start_stop_processes();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_stop_port_process_clicked(move |protocol, port, pid| {
                controller
                    .borrow_mut()
                    .start_stop_port_process(protocol.to_string(), port, pid);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_open_logs_clicked(move || {
                controller.borrow().open_logs_dir();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_check_updates_clicked(move || {
                controller.borrow_mut().start_update_check(false);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_refresh_servers_clicked(move || {
                controller.borrow_mut().start_refresh_servers();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_copy_feedback_clicked(move || controller.borrow_mut().copy_feedback_group());
        }
        {
            let controller = Rc::clone(controller);
            app.on_copy_server_address_clicked(move |address| {
                controller
                    .borrow_mut()
                    .copy_server_address(address.to_string());
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_join_server_clicked(move |address| {
                controller.borrow_mut().join_friend_input(address.as_str());
            });
        }
        {
            let c = Rc::clone(controller);
            app.on_join_clipboard_clicked(move || c.borrow_mut().join_clipboard_invite());
        }
        {
            let c = Rc::clone(controller);
            app.on_copy_room_invite_clicked(move |vnt| c.borrow_mut().copy_room_invite(vnt));
        }
        {
            let c = Rc::clone(controller);
            app.on_join_own_room_clicked(move || c.borrow_mut().join_own_room());
        }
        {
            let c = Rc::clone(controller);
            app.on_cancel_room_invite_clicked(move || c.borrow_mut().cancel_friend_invite());
        }
        {
            let controller = Rc::clone(controller);
            app.on_start_fixed_room_clicked(move || {
                let mut controller = controller.borrow_mut();
                let action = if controller.ui.get_fixed_room_pve() {
                    fixed_room::RoomAction::StartPve
                } else {
                    fixed_room::RoomAction::Start
                };
                controller.request_fixed_room(action);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_stop_fixed_room_clicked(move || {
                controller.borrow_mut().request_fixed_room(fixed_room::RoomAction::Stop);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_vnt_start_clicked(move || {
                controller.borrow_mut().start_vnt();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_vnt_stop_clicked(move || {
                controller.borrow_mut().stop_vnt();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_vnt_refresh_clicked(move || {
                controller.borrow_mut().refresh_vnt_status_hint();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_vnt_add_server_clicked(move |address| {
                controller
                    .borrow_mut()
                    .add_vnt_server_option(address.to_string());
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_vnt_join_peer_clicked(move |address| {
                controller
                    .borrow_mut()
                    .join_vnt_room_peer(address.as_str());
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_vnt_host_selected(move |address| {
                controller
                    .borrow_mut()
                    .select_vnt_mapping_host(address.to_string());
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_vnt_host_cancelled(move || {
                controller.borrow_mut().cancel_vnt_host_selection();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_vnt_settings_changed(move || {
                controller.borrow_mut().save_app_prefs();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_drive_toggled(move |index, checked| {
                controller.borrow_mut().toggle_drive(index, checked);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_drive_confirmed(move || {
                controller.borrow_mut().start_drive_scan();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_drive_cancelled(move || {
                controller.borrow().ui.set_show_drive_dialog(false);
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_app_dialog_primary(move || {
                controller.borrow_mut().handle_dialog_primary();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_app_dialog_secondary(move || {
                controller.borrow_mut().handle_dialog_secondary();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.on_app_dialog_tertiary(move || {
                controller.borrow_mut().handle_dialog_tertiary();
            });
        }
        {
            let controller = Rc::clone(controller);
            app.window().on_close_requested(move || {
                if controller.borrow_mut().request_window_close() {
                    CloseRequestResponse::HideWindow
                } else {
                    CloseRequestResponse::KeepWindowShown
                }
            });
        }
        {
            let weak_app = app.as_weak();
            app.on_window_drag_started(move || {
                if let Some(app) = weak_app.upgrade() {
                    use slint::winit_030::WinitWindowAccessor;
                    app.window().with_winit_window(|window| {
                        let _ = window.drag_window();
                    });
                }
            });
        }
        {
            let weak_app = app.as_weak();
            app.on_window_minimize_clicked(move || {
                if let Some(app) = weak_app.upgrade() {
                    app.window().set_minimized(true);
                }
            });
        }
        {
            let controller = Rc::clone(controller);
            let weak_app = app.as_weak();
            app.on_window_close_clicked(move || {
                if weak_app.upgrade().is_some() {
                    let mut controller = controller.borrow_mut();
                    if controller.request_window_close() {
                        controller.close_window_now();
                    }
                }
            });
        }
    }

    /// 启动 UI 线程定时器，用于拉取 channel 消息和驱动忙碌动画。
    pub(super) fn start_background_timers(controller: &Rc<RefCell<Self>>) {
        let log_controller = Rc::clone(controller);
        let log_timer = Timer::default();
        // 因为 Slint UI 对象不是 Send，所以工作线程只入队消息；
        // 该定时器在 UI 线程应用这些消息。
        log_timer.start(TimerMode::Repeated, Duration::from_millis(100), move || {
            log_controller.borrow_mut().drain_messages();
        });
        std::mem::forget(log_timer);

        let pulse_controller = Rc::clone(controller);
        let pulse_timer = Timer::default();
        pulse_timer.start(TimerMode::Repeated, Duration::from_millis(650), move || {
            let ui = &pulse_controller.borrow().ui;
            if ui.get_busy() {
                ui.set_pulse(!ui.get_pulse());
            } else if ui.get_pulse() {
                ui.set_pulse(false);
            }
        });
        std::mem::forget(pulse_timer);
    }

    /// 执行初始自动识别和服务器列表刷新。
    pub(super) fn initialize(&mut self) {
        self.refresh_target_from_detection(true);
        self.start_system_check();
    }

    pub(super) fn on_page_changed(&mut self, page: i32) {
        if page != 3 { self.invite_auto_route = None; }
        if self.ui.get_busy() {
            let locked_page = self.active_page.load(Ordering::Relaxed);
            if page != locked_page {
                self.ui.set_page(locked_page);
            }
            return;
        }

        if !(0..=4).contains(&page) {
            self.ui.set_page(0);
            return;
        }

        self.ui.set_feedback_copied(false);
        self.active_page.store(page, Ordering::Relaxed);
        self.advance_tutorial_for_page(page);
        if page == 1 {
            self.refresh_port_rows_once();
        }
        if page == 2 {
            self.start_refresh_servers();
        }
        if page == 3 {
            self.refresh_vnt_status_hint();
        }
    }

    fn refresh_port_rows_once(&self) {
        let core = self.core.clone();
        let tx = self.tx.clone();
        let target = self.port_target.read().ok().and_then(|guard| guard.clone());
        thread::spawn(move || {
            if let Ok(rows) = core.port_status_rows_for_target(target.as_deref()) {
                let _ = tx.send(AppMessage::PortRows(rows));
            }
        });
    }

    /// 在 UI 退出前停止后台会话。
    pub(super) fn shutdown(&mut self) {
        self.save_app_prefs();
        if let Some(session) = self.vnt_session.as_mut() {
            session.stop();
        }
    }
}
