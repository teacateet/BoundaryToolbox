use slint::ComponentHandle;
use slint::platform::software_renderer::{MinimalSoftwareWindow, RepaintBufferType};
use slint::platform::{Platform, PointerEventButton, WindowAdapter, WindowEvent};
use std::rc::Rc;

// 只挂载真实页面和选房弹窗，不创建 AppController 或联机后台。
slint::slint! {
    import { VntPage } from "../../ui/pages/vnt_page.slint";
    import { VntHostDialog } from "../../ui/dialogs/vnt_host_dialog.slint";
    import { VntPeerRow } from "../../ui/models.slint";
    export component PeerFixture inherits Window {
        width: 1000px;
        height: 700px;
        in property <bool> dialog;
        in property <bool> busy;
        in property <bool> online: true;
        in-out property <string> page-port <=> page.vnt_game_port;
        in-out property <string> dialog-port <=> picker.vnt_game_port;
        out property <string> joined;
        out property <string> picked;
        property <[VntPeerRow]> peers: [{name: "Host", address: "10.26.0.8", detail: "", online: root.online}];
        page := VntPage {
            vnt_running: true;
            vnt_busy: root.busy;
            vnt_peer_rows: root.peers;
            vnt_join_peer_clicked(ip) => { root.joined = ip + ":" + self.vnt_game_port; }
        }
        picker := VntHostDialog {
            visible: root.dialog;
            busy: root.busy;
            peers: root.peers;
            host_selected(ip) => { root.picked = ip + ":" + self.vnt_game_port; }
        }
    }
}

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

fn type_port(window: &MinimalSoftwareWindow, port: &str) {
    for digit in port.chars() {
        let text: slint::SharedString = digit.to_string().into();
        window.dispatch_event(WindowEvent::KeyPressed { text: text.clone() });
        window.dispatch_event(WindowEvent::KeyReleased { text });
    }
}

#[test]
fn vnt_room_real_page_and_picker_accept_guest_port_offscreen() {
    let window = MinimalSoftwareWindow::new(RepaintBufferType::NewBuffer);
    slint::platform::set_platform(Box::new(Offscreen(window.clone()))).unwrap();
    let ui = PeerFixture::new().unwrap();
    ui.show().unwrap();
    window.set_size(slint::PhysicalSize::new(1000, 700));
    let mut pixels = slint::SharedPixelBuffer::<slint::Rgb8Pixel>::new(1000, 700);
    let mut paint = || {
        window.request_redraw();
        assert!(window.draw_if_needed(|renderer| {
            renderer.render(pixels.make_mut_slice(), 1000);
        }));
    };
    paint();
    assert_eq!(ui.get_page_port(), "7777");
    assert_eq!(ui.get_dialog_port(), "7777");
    ui.set_online(false);
    click(&window, 936., 415.);
    assert!(ui.get_joined().is_empty());
    ui.set_online(true);
    click(&window, 936., 415.);
    assert_eq!(ui.get_joined(), "10.26.0.8:7777");

    ui.set_page_port("".into());
    click(&window, 930., 350.);
    type_port(&window, "18777");
    assert_eq!(ui.get_page_port(), "18777");
    click(&window, 936., 415.);
    assert_eq!(ui.get_joined(), "10.26.0.8:18777");

    ui.set_dialog(true);
    ui.set_dialog_port("".into());
    paint();
    click(&window, 812., 294.);
    type_port(&window, "18777");
    assert_eq!(ui.get_dialog_port(), "18777");
    ui.set_busy(true);
    click(&window, 700., 358.);
    assert!(ui.get_picked().is_empty());
    ui.set_busy(false);
    click(&window, 700., 358.);
    assert_eq!(ui.get_picked(), "10.26.0.8:18777");
    ui.hide().unwrap();
}
