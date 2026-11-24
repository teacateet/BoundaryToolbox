use super::*;
use anyhow::ensure;
use slint::platform::software_renderer::{MinimalSoftwareWindow, RepaintBufferType};
use slint::platform::{Platform, PointerEventButton, WindowAdapter, WindowEvent};

struct Offscreen(Rc<MinimalSoftwareWindow>);
impl Platform for Offscreen {
    fn create_window_adapter(&self) -> Result<Rc<dyn WindowAdapter>, slint::PlatformError> {
        Ok(self.0.clone())
    }
}

fn click(window: &MinimalSoftwareWindow, x: f32, y: f32) {
    let position = slint::LogicalPosition::new(x, y);
    let button = PointerEventButton::Left;
    window.dispatch_event(WindowEvent::PointerPressed { position, button });
    window.dispatch_event(WindowEvent::PointerReleased { position, button });
}
fn frame(
    w: &MinimalSoftwareWindow,
    width: u32,
    height: u32,
) -> slint::SharedPixelBuffer<slint::Rgb8Pixel> {
    w.set_size(slint::PhysicalSize::new(width, height));
    slint::platform::update_timers_and_animations();
    w.request_redraw();
    let mut pixels = slint::SharedPixelBuffer::new(width, height);
    assert!(w.draw_if_needed(|r| {
        r.render(pixels.make_mut_slice(), width as usize);
    }));
    pixels
}

fn seed(ui: &AppWindow) {
    ui.set_is_admin(false);
    ui.set_has_target(true);
    ui.set_target_text(
        r"C:\BrtWork\SteamLibrary\steamapps\common\Boundary\ProjectBoundary\Binaries\Win64".into(),
    );
    ui.set_vnt_username("Guest-07".into());
    ui.set_vnt_network_code("ORB-19".into());
    ui.set_vnt_server_text("127.0.0.1:11010".into());
    ui.set_vnt_game_port("18777".into());
    ui.set_fixed_room_port("18777".into());
    ui.set_server_rows(ModelRc::new(VecModel::from(vec![ServerRow {
        name: "BRT Test Room".into(),
        address: "192.0.2.8:18777".into(),
        meta: "TDM".into(),
        players: "2 / 10".into(),
        state: "UDP".into(),
        active: true,
        joinable: true,
    }])));
    ui.set_port_rows(ModelRc::new(VecModel::from(vec![PortRow {
        label: "UDP / 18777".into(),
        detail: "Boundary / PID 200".into(),
        protocol: "UDP".into(),
        port: 18777,
        pid: 200,
        occupied: true,
        expected: false,
    }])));
}
fn nav(window: &MinimalSoftwareWindow, width: u32, tab: usize) {
    click(
        window,
        18. + (width as f32 - 36.) * (tab as f32 + 0.5) / 4.,
        60.,
    );
}

fn online_tab(window: &MinimalSoftwareWindow, width: u32, network: bool) {
    let left = if width < 776 { 32. } else { 40. };
    click(window, left + if network { 142. } else { 47. }, if width < 776 { 132. } else { 140. });
}

fn type_text(w: &MinimalSoftwareWindow, value: &str) {
    for ch in value.chars() {
        let text: SharedString = ch.to_string().into();
        w.dispatch_event(WindowEvent::KeyPressed { text: text.clone() });
        w.dispatch_event(WindowEvent::KeyReleased { text });
    }
}


#[test]
fn workbench_controller_standard_user_can_inspect_p2p() -> Result<()> {
    let window = MinimalSoftwareWindow::new(RepaintBufferType::NewBuffer);
    slint::platform::set_platform(Box::new(Offscreen(window.clone())))?;
    let ui = AppWindow::new()?;
    let controller = AppController::new(ui.clone_strong())?;
    controller
        .borrow()
        .stop_background
        .store(true, Ordering::Relaxed);
    controller.borrow_mut().is_admin = false;
    ui.set_is_admin(false);
    AppController::bind_callbacks(&controller);
    ui.show()?;
    frame(&window, 640, 480);
    ui.set_servers_loading(true);
    nav(&window, 640, 1);
    frame(&window, 640, 480);
    assert_eq!(controller.borrow().active_page.load(Ordering::Relaxed), 2);
    online_tab(&window, 640, true);
    frame(&window, 640, 480);
    assert_eq!(ui.get_page(), 3);
    assert_eq!(controller.borrow().active_page.load(Ordering::Relaxed), 3);
    nav(&window, 640, 0);
    frame(&window, 640, 480);
    ui.set_vnt_status_text("stale status".into());
    nav(&window, 640, 1);
    frame(&window, 640, 480);
    assert_eq!(ui.get_page(), 3);
    assert_ne!(ui.get_vnt_status_text(), "stale status");
    ui.set_vnt_status_text("stale status".into());
    ui.invoke_vnt_refresh_clicked();
    assert_ne!(ui.get_vnt_status_text(), "stale status");
    assert!(!ui.get_show_app_dialog());
    ui.invoke_vnt_start_clicked();
    assert!(ui.get_app_dialog_error() && ui.get_show_app_dialog());
    assert!(controller.borrow().vnt_session.is_none());
    ui.hide()?;
    Ok(())
}

#[test]
fn workbench_actions_as_standard_user() -> Result<()> {
    let w = MinimalSoftwareWindow::new(RepaintBufferType::NewBuffer);
    slint::platform::set_platform(Box::new(Offscreen(w.clone())))?;
    let ui = AppWindow::new()?;
    seed(&ui);
    ui.set_language(1);
    ui.show()?;
    let events = Rc::new(RefCell::new(Vec::new()));
    macro_rules! on_action {
        ($callback:ident, $name:literal) => {{
            let events = events.clone();
            ui.$callback(move || events.borrow_mut().push($name.to_string()));
        }};
    }
    on_action!(on_refresh_servers_clicked, "refresh-list");
    on_action!(on_vnt_refresh_clicked, "refresh-network");
    on_action!(on_vnt_start_clicked, "start-network");
    on_action!(on_detect_processes_clicked, "processes");
    on_action!(on_stop_processes_clicked, "stop-related");
    on_action!(on_check_updates_clicked, "updates");
    on_action!(on_open_logs_clicked, "logs");
    on_action!(on_open_github_proxy_dialog_clicked, "select-proxy");
    on_action!(on_refresh_github_proxies_clicked, "refresh-proxy");
    let joined = events.clone();
    ui.on_join_server_clicked(move |ip| joined.borrow_mut().push(format!("join {ip}")));
    let copied = events.clone();
    ui.on_copy_server_address_clicked(move |ip| copied.borrow_mut().push(format!("copy {ip}")));
    let stopped = events.clone();
    ui.on_stop_port_process_clicked(move |proto, port, pid| {
        stopped.borrow_mut().push(format!("{proto}/{port}/{pid}"))
    });
    let weak = ui.as_weak();
    let clipboard = events.clone();
    ui.on_copy_feedback_clicked(move || {
        servers::copy_feedback(&weak.unwrap(), |number| {
            clipboard.borrow_mut().push(number.to_owned());
            Ok(())
        })
        .unwrap();
    });

    frame(&w, 640, 480);
    nav(&w, 640, 1);
    assert_eq!(ui.get_page(), 2);
    assert!(!ui.get_fixed_room_pve());
    for (scale, width, height) in [(1.0, 640, 480), (1.5, 960, 720)] {
        w.dispatch_event(WindowEvent::ScaleFactorChanged { scale_factor: scale });
        frame(&w, width, height);
        assert_eq!(ui.window().scale_factor(), scale);
        for pve in [true, false] {
            let center = ui.invoke_room_mode_center(pve);
            click(&w, center.x, center.y);
            assert_eq!(ui.get_fixed_room_pve(), pve,
                "Room mode click at ({}, {}), scale {scale}", center.x, center.y);
        }
    }
    w.dispatch_event(WindowEvent::ScaleFactorChanged { scale_factor: 1.0 });
    frame(&w, 640, 480);
    let center = ui.invoke_room_mode_center(true);
    click(&w, center.x, center.y);
    online_tab(&w, 640, true);
    frame(&w, 640, 480);
    click(&w, 530., 132.);
    // 窄窗口会将新版联机说明折行；根据管理员按钮的实际渲染位置点击。
    ui.set_is_admin(true);
    let rendered = frame(&w, 640, 480);
    let start_y = (280..470).find(|&y| {
        let b = rendered.as_bytes();
        let at = (y * 640 + 50) * 3;
        b[at..at+3] == [0x7b, 0xdb, 0xba]
    }).expect("Network start button must be visible") as f32 + 12.;
    ui.set_is_admin(false);
    frame(&w, 640, 480);
    click(&w, 105., start_y);
    assert_eq!(*events.borrow(), ["refresh-network"]);
    ui.set_is_admin(true);
    frame(&w, 640, 480);
    click(&w, 105., start_y);
    assert_eq!(events.borrow().last().unwrap(), "start-network");

    nav(&w, 640, 3);
    frame(&w, 640, 480);
    click(&w, 548., 265.);
    assert!(ui.get_feedback_copied());
    assert_eq!(events.borrow().last().unwrap(), "1048069240");
    assert!(servers::copy_feedback(&ui, |_| anyhow::bail!("clipboard busy")).is_err());
    assert!(!ui.get_feedback_copied());
    frame(&w, 1260, 780);
    click(&w, 105., 390.);
    click(&w, 250., 390.);
    click(&w, 100., 522.);
    click(&w, 210., 522.);

    nav(&w, 1260, 2);
    frame(&w, 1260, 780);
    click(&w, 100., 328.);
    click(&w, 255., 328.);
    click(&w, 1150., 425.);

    nav(&w, 1260, 1);
    frame(&w, 1260, 780);
    online_tab(&w, 1260, false);
    frame(&w, 1260, 780);
    assert!(ui.get_fixed_room_pve());
    assert_eq!(ui.get_fixed_room_port(), "18777");
    assert_eq!(ui.get_vnt_network_code(), "ORB-19");
    click(&w, 180., 214.);
    type_text(&w, "192.0.2.9:18777");
    click(&w, 1160., 215.);
    click(&w, 1160., 395.);
    click(&w, 1120., 485.);
    click(&w, 1195., 485.);
    assert_eq!(
        &events.borrow()[3..],
        [
            "updates",
            "logs",
            "select-proxy",
            "refresh-proxy",
            "processes",
            "stop-related",
            "UDP/18777/200",
            "join 192.0.2.9:18777",
            "refresh-list",
            "join 192.0.2.8:18777",
            "copy 192.0.2.8:18777",
        ]
    );
    ui.hide()?;
    Ok(())
}

#[test]
fn offline_no_tun_preference_allows_local_pve_lobby_preflight() -> Result<()> {
    let window = MinimalSoftwareWindow::new(RepaintBufferType::NewBuffer);
    slint::platform::set_platform(Box::new(Offscreen(window.clone())))?;
    let ui = AppWindow::new()?;
    let controller = AppController::new(ui.clone_strong())?;
    controller.borrow().stop_background.store(true, Ordering::Relaxed);
    AppController::bind_callbacks(&controller);

    // 故意选择缺失的目录，让真实启动回调在目录预检处结束。
    let missing = std::env::temp_dir().join(format!("brt-missing-{}", uuid::Uuid::new_v4()));
    let expected = controller.borrow().core.normalize_selected_path(&missing).unwrap_err().to_string();
    controller.borrow_mut().set_current_target(Some(missing), "");
    ui.set_launch_pve_server(true);
    ui.set_vnt_no_tun(true);
    assert!(!ui.get_vnt_running());
    ui.show()?;
    frame(&window, 640, 480);
    assert!(!ui.get_show_app_dialog());
    click(&window, 135., 302.);
    assert!(ui.get_show_app_dialog() && ui.get_app_dialog_error());
    assert_eq!(ui.get_app_dialog_text().as_str(), expected);
    ui.hide()?;
    Ok(())
}

#[test]
fn setup_guide_opens_and_visits_both_online_routes() -> Result<()> {
    let w = MinimalSoftwareWindow::new(RepaintBufferType::NewBuffer);
    slint::platform::set_platform(Box::new(Offscreen(w.clone())))?;
    let ui = AppWindow::new()?;
    let controller = AppController::new(ui.clone_strong())?;
    controller.borrow().stop_background.store(true, Ordering::Relaxed);
    AppController::bind_callbacks(&controller);
    seed(&ui);
    ui.set_language(0);
    ui.set_servers_loading(true);
    let weak = ui.as_weak();
    ui.on_copy_feedback_clicked(move || {
        servers::copy_feedback(&weak.unwrap(), |value| {
            ensure!(value == "1048069240");
            Ok(())
        }).unwrap();
    });
    ui.show()?;
    frame(&w, 640, 480);
    click(&w, 543., 392.);
    assert!(ui.get_tutorial_active());
    frame(&w, 640, 480);
    click(&w, 509., 283.);
    assert!(ui.get_feedback_copied());
    // 用离屏渲染的面板下缘定位操作行，文案换行不会改变点击目标。
    for (step, x, next, page) in [
        (1, 520., 2, 4),
        (2, 415., 4, 4),
        (4, 520., 5, 0),
        (5, 410., 6, 0),
        (6, 520., 7, 0),
        (7, 520., 8, 2),
        (8, 520., 9, 3),
        (9, 520., 0, 3),
    ] {
        assert_eq!(ui.get_tutorial_step(), step);
        let pixels = frame(&w, 640, 480);
        let bottom = (0..480).rev()
            .find(|y| pixels.as_slice()[y * 640 + 319] == slint::Rgb8Pixel::new(25, 33, 33))
            .expect("visible tutorial panel");
        click(&w, x, bottom as f32 - 30.);
        frame(&w, 640, 480);
        assert_eq!((ui.get_tutorial_step(), ui.get_page()), (next, page), "step {step}");
    }
    assert!(!ui.get_tutorial_active());
    assert!(!ui.get_busy());
    ui.hide()?;
    Ok(())
}

#[test]
fn workbench_snapshots_and_navigation() -> Result<()> {
    let w = MinimalSoftwareWindow::new(RepaintBufferType::NewBuffer);
    slint::platform::set_platform(Box::new(Offscreen(w.clone())))?;
    let ui = AppWindow::new()?;
    seed(&ui);
    ui.show()?;
    let output = std::env::var_os("BOUNDARY_UI_CAPTURE").map(PathBuf::from);
    if let Some(path) = &output {
        fs::create_dir_all(path)?;
    }
    let pages = [
        (0, 0, "play"),
        (1, 2, "rooms"),
        (1, 3, "network"),
        (2, 1, "diagnostics"),
        (3, 4, "settings"),
    ];
    for (width, height) in [(1260, 780), (640, 480)] {
        let mut signatures = std::collections::HashSet::new();
        for (lang, suffix) in [(0, "zh"), (1, "en"), (2, "ja")] {
            ui.set_language(lang);
            ui.set_language_mode(lang);
            frame(&w, width, height);
            for (tab, page, name) in pages {
                nav(&w, width, tab);
                if tab == 1 {
                    frame(&w, width, height);
                    online_tab(&w, width, page == 3);
                }
                ensure!(
                    ui.get_page() == page,
                    "Navigation failed: {name}, {width}, {suffix}"
                );
                let pixels = frame(&w, width, height);
                let bytes = pixels.as_bytes();
                let body = &bytes[(width as usize * 104 + 18) * 3..];
                let bright = body
                    .chunks_exact(3)
                    .filter(|p| p[0] > 120 && p[1] > 120 && p[2] > 120)
                    .count();
                ensure!(
                    bright > 1000,
                    "Page body is blank: {name}, {width}, {suffix}"
                );
                ensure!(
                    signatures.insert(crc32fast::hash(body)),
                    "Duplicate page or translation: {name}, {suffix}"
                );
                if let Some(dir) = &output {
                    image::save_buffer(
                        dir.join(format!("{name}-{suffix}-{width}.png")),
                        bytes,
                        width,
                        height,
                        image::ColorType::Rgb8,
                    )?;
                }
            }
        }
        if let Some(dir) = &output {
            ui.set_language(0);
            ui.set_language_mode(0);
            ui.set_page(0);
            ui.set_tutorial_step(1);
            ui.set_tutorial_active(true);
            let pixels = frame(&w, width, height);
            image::save_buffer(dir.join(format!("setup-zh-{width}.png")), pixels.as_bytes(), width, height, image::ColorType::Rgb8)?;
            ui.set_tutorial_active(false);
        }
    }
    ui.hide()?;
    Ok(())
}
