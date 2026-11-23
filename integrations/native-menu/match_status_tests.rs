use super::*;
use MatchStep::*;

#[test]
fn match_steps_repaint_launch_page_but_not_lan() -> Result<()> {
    use slint::platform::software_renderer::{MinimalSoftwareWindow, RepaintBufferType};
    use slint::platform::{Platform, WindowAdapter};
    struct Screen(Rc<MinimalSoftwareWindow>);
    impl Platform for Screen {
        fn create_window_adapter(&self) -> Result<Rc<dyn WindowAdapter>, slint::PlatformError> {
            Ok(self.0.clone())
        }
    }
    let screen = MinimalSoftwareWindow::new(RepaintBufferType::NewBuffer);
    slint::platform::set_platform(Box::new(Screen(screen.clone())))?;
    let ui = AppWindow::new()?;
    ui.set_page(0);
    ui.set_language(0);
    ui.set_has_target(true);
    ui.set_target_text("fixture".into());
    ui.set_launch_pve_server(true);
    ui.show()?;
    let epoch = LAUNCH_EPOCH.load(Ordering::Relaxed);
    for (width, height) in [(640, 480), (1260, 780)] {
        let mut previous = None;
        screen.set_size(slint::PhysicalSize::new(width, height));
        for stage in [Prepare, Ticket, Ack, RoundStarted, LoadoutWaiting, LoadoutSent] {
            show_match_step(&ui, epoch, "fixture", stage);
            assert_eq!(ui.get_status_text().as_str(), stage.label(0));
            slint::platform::update_timers_and_animations();
            screen.request_redraw();
            let mut px = slint::SharedPixelBuffer::<slint::Rgb8Pixel>::new(width, height);
            assert!(screen.draw_if_needed(|r| { r.render(px.make_mut_slice(), width as usize); }));
            let image = px.as_bytes().to_vec();
            if width == 1260 && matches!(stage,LoadoutWaiting|LoadoutSent)
                && let Some(folder) = std::env::var_os("BRT_QA_LOADOUT_IMAGES") {
                let name = if stage == LoadoutSent {"sent.png"} else {"waiting.png"};
                image::save_buffer(Path::new(&folder).join(name), &image, width, height, image::ColorType::Rgb8)?;
            }
            if let Some(old) = previous {
                assert!(old != image, "stage did not repaint");
            }
            previous = Some(image);
        }
    }
    ui.set_launch_pve_server(false);
    ui.set_status_text("LAN".into());
    show_match_step(&ui, epoch, "fixture", Prepare);
    assert_eq!(ui.get_status_text().as_str(), "LAN");
    ui.hide()?;
    Ok(())
}

#[test]
fn only_the_current_rooms_sent_loadout_changes_the_match_display() {
    let run = uuid::Uuid::new_v4().to_string();
    let id = uuid::Uuid::new_v4();
    let other = uuid::Uuid::new_v4();
    let logged = |choice: uuid::Uuid,event: &str,life:u64| format!("[PVE-SPAWN] {}",serde_json::json!({"choice":choice.to_string(),"run":run,"event":event,"life":life}));
    assert!(spawn_event(&logged(id,"menu-read",0), &run).is_none());
    assert!(spawn_event(&logged(id,"loadout-returned",0), &run).is_none());
    assert!(spawn_event(&logged(id,"loadout-returned",1), "old-run").is_none());
    let mut v = MatchView{choice_id:Some(id),last:Some(Ack),..Default::default()};
    let ready = spawn_event(&logged(id,"room-registered",0), &run).unwrap();
    let stale = spawn_event(&logged(other,"loadout-returned",2), &run).unwrap();
    assert_eq!(v.archive_status(None,&[ready,stale]),None);
    assert!(v.room_started(Some(id)));
    assert_eq!(v.archive_status(Some(id),&[]),Some(LoadoutWaiting));
    let sent=spawn_event(&logged(id,"loadout-returned",1), &run).unwrap();
    assert_eq!(v.archive_status(Some(id),&[sent]),Some(LoadoutSent));
    assert_eq!(v.archive_status(Some(id),&[ready]),None);
    v.accept(1400,Hall,false);
    assert_eq!(v.archive_status(Some(id),&[sent]),None);
    v.accept(1500,Prepare,false);
    assert_eq!(v.archive_sent,None);
}

#[test]
fn display_tracks_ticket_and_ack_not_presence_or_commit() {
    let row = r#"[PVE-ROOM] {"event":"local-feedback","stage":"ticket-received","at":1040,"run":"a","choice":1}"#;
    assert_eq!(menu_step(row, "a"), Some((1040, Ticket, None)));
    assert_eq!(menu_step(row, "b"), None);
    assert_eq!(
        menu_step("[PARTY] Presence InMatching => InMatch", "a"),
        None
    );
    assert_eq!(bridge_step("2026-11-22 15:58:06 join-requested"), None);
    assert_eq!(bridge_step("2026-11-22 15:58:06 join_ack").unwrap().1, Ack);
    let mut v = MatchView::default();
    assert!(!v.accept(1000, Ack, true));
    for (ms, s, b) in [
        (1010, Prepare, false),
        (1040, Ticket, false),
        (1000, Ack, true),
    ] {
        assert!(v.accept(ms, s, b));
    }
    assert!(!v.accept(1000, Closed, true));
    assert!(Ack.label(0).contains("进房未核验"));
    let first = uuid::Uuid::new_v4();
    v.choice_id = Some(first);
    assert!(!v.room_started(Some(uuid::Uuid::new_v4())));
    assert!(v.room_started(Some(first)));
    assert!(!v.room_started(Some(first)));
    assert!(v.accept(2010, Prepare, false));
    assert!(v.accept(2020, Ticket, false));
    assert!(!v.accept(2000, Ack, true));
    assert!(v.accept(3000, Failed, true));
    assert!(v.accept(4000, Ack, true));
    assert!(v.accept(5010, Prepare, false));
    assert!(v.accept(5500, Cancel, false));
    assert!(!v.accept(6000, Ack, true));
}

#[test]
fn reader_waits_for_complete_lines_and_lease_expiry() {
    let dir = std::env::temp_dir().join(uuid::Uuid::new_v4().to_string());
    fs::create_dir(&dir).unwrap();
    let file = dir.join("fixture");
    let id = uuid::Uuid::new_v4().to_string();
    fs::write(&file,serde_json::json!({"v":1,"mode":"pve","offline":true,"autoPool":true,"expiresAt":2000,"runId":id}).to_string()).unwrap();
    assert_eq!(live_pool(&file, 1900), Some((id, None)));
    assert_eq!(live_pool(&file, 2000), None);
    let mut tail = MatchTail::default();
    fs::write(&file, "old\npartial").unwrap();
    let mut rows = vec![];
    tail.poll(&file, |s| rows.push(s.to_owned()));
    fs::write(&file, "old\npartial line\n").unwrap();
    tail.poll(&file, |s| rows.push(s.to_owned()));
    fs::write(&file, "new\n").unwrap();
    tail.poll(&file, |s| rows.push(s.to_owned()));
    assert_eq!(rows, ["old", "partial line", "new"]);
    fs::remove_file(file).unwrap();
    fs::remove_dir(dir).unwrap();
}
