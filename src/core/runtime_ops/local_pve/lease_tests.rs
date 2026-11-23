use super::super::hidden_command;
use super::*;
use serde_json::Value;
use std::net::UdpSocket;
use std::process::{Child, Stdio};
use std::time::Instant;

const FIXTURE: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/src/core/runtime_ops/local_pve/lease_fixture.cjs"
);
const READER: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/integrations/native-menu/ready-room.cjs"
);

#[test]
fn wait_reports_the_signaled_process_and_preserves_windows_errors() -> Result<()> {
    use windows::Win32::System::Threading::{CreateEventW, ResetEvent, SetEvent};
    let roles = ["托管档案服务", "本机对局启动器", "本机对局"];
    let mut events = Vec::new();
    for _ in roles {
        let h = unsafe { CreateEventW(None, true, false, None)? };
        events.push(unsafe { OwnedHandle::from_raw_handle(h.0) });
    }
    let pins: [(&str, HANDLE); 3] =
        std::array::from_fn(|i| (roles[i], HANDLE(events[i].as_raw_handle())));
    require_running(pins, Duration::ZERO)?;
    for (role, handle) in pins {
        unsafe { SetEvent(handle)? };
        assert_eq!(
            require_running(pins, Duration::ZERO)
                .unwrap_err()
                .to_string(),
            format!("{role}已退出")
        );
        unsafe { ResetEvent(handle)? };
    }
    let error = require_running([("测试", HANDLE::default())], Duration::ZERO).unwrap_err();
    assert_eq!(error.to_string(), "Windows 等待本机进程失败");
    assert_eq!(
        error
            .downcast_ref::<std::io::Error>()
            .unwrap()
            .raw_os_error(),
        Some(6)
    );
    Ok(())
}

struct Temp(PathBuf);

impl Temp {
    fn new() -> Result<Self> {
        let p = std::env::temp_dir().join(format!("brt-lease-{}", Uuid::new_v4()));
        fs::create_dir(&p)?;
        fs::create_dir(p.join("profile-runtime"))?;
        Ok(Self(p))
    }
    fn lease(&self, c: char) -> PathBuf {
        self.0
            .join("profile-runtime")
            .join(format!("pve-ready-{}.json", c.to_string().repeat(64)))
    }
    fn app(&self, c: char) -> PathBuf {
        self.0
            .join("profile-runtime/apps")
            .join(c.to_string().repeat(64))
    }
}
impl Drop for Temp {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

fn cfg(port: u16, map: &str) -> Selection {
    Selection {
        mode: "pve".into(),
        offline: true,
        port,
        map: Some(map.into()),
        difficulty: Some("normal".into()),
    }
}

fn node() -> Result<PathBuf> {
    let out = hidden_command("node")
        .args(["--no-global-search-paths", "-p", "process.execPath"])
        .env_remove("NODE_OPTIONS")
        .env_remove("NODE_PATH")
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .output()?;
    ensure!(out.status.success(), "fixture needs Node");
    Ok(PathBuf::from(std::str::from_utf8(&out.stdout)?.trim()))
}

fn native_reader(dir: &Temp, key: char, mode: &str, elapsed: u64) -> Result<Value> {
    let out = hidden_command(node()?)
        .env_remove("NODE_OPTIONS")
        .env_remove("NODE_PATH")
        .args(["--no-global-search-paths", FIXTURE, "reader", READER])
        .arg(dir.app(key))
        .arg(mode)
        .arg(elapsed.to_string())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .output()?;
    ensure!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    Ok(serde_json::from_slice(&out.stdout)?)
}

#[test]
fn rust_lease_is_read_by_the_real_menu_only_for_the_right_app_choice_and_lifetime() -> Result<()> {
    let tmp = Temp::new()?;
    let path = tmp.lease('a');
    let mut slot = Slot::claim(&tmp.0, &path)?;
    let socket = UdpSocket::bind((Ipv4Addr::LOCALHOST, 0))?;
    let port = socket.local_addr()?.port();
    slot.publish(&cfg(port, "CircularX"))?;
    let first: Value = serde_json::from_slice(&fs::read(&path)?)?;
    assert!(Uuid::parse_str(first["runId"].as_str().unwrap()).is_ok());
    assert!(first["expiresAt"].as_u64().unwrap() - now_ms()? <= 15_000);
    let found = native_reader(&tmp, 'a', "NORMAL BOT", 0)?;
    assert_eq!(found["status"], "found");
    assert_eq!(found["serverPort"], port);
    assert_eq!(found["serverIp"], "127.0.0.1");
    assert_eq!(native_reader(&tmp, 'a', "EASY BOT", 0)?["status"], "queued");
    assert_eq!(
        native_reader(&tmp, 'a', "NORMAL BOT", 15_001)?["status"],
        "queued"
    );
    assert_eq!(native_reader(&tmp, 'b', "NORMAL BOT", 0)?, Value::Null);
    slot.publish(&cfg(port, "Warehouse"))?;
    assert_eq!(
        native_reader(&tmp, 'a', "NORMAL BOT", 0)?["status"],
        "queued"
    );
    drop(slot);
    assert_eq!(native_reader(&tmp, 'a', "NORMAL BOT", 0)?, Value::Null);
    Ok(())
}

#[test]
fn competing_writer_fresh_receipt_and_foreign_replacement_are_not_overwritten() -> Result<()> {
    let a = Temp::new()?;
    let b = Temp::new()?;
    let path = a.lease('a');
    assert!(Slot::claim(&a.0, &b.lease('a')).is_err());
    assert!(Slot::claim(&a.0, &a.lease('A')).is_err());
    let mut one = Slot::claim(&a.0, &path)?;
    assert!(!path.try_exists()?);
    assert!(Slot::claim(&a.0, &path).is_err());
    let first_id = one.run.clone();
    let port = 34123;
    one.publish(&cfg(port, "CircularX"))?;
    let replacement =
        serde_json::to_vec(&json!({"v":1,"runId":"other-run","expiresAt":now_ms()?+12_000}))?;
    fs::write(&path, &replacement)?;
    assert!(one.publish(&cfg(port, "CircularX")).is_err());
    drop(one);
    assert_eq!(fs::read(&path)?, replacement);
    assert!(Slot::claim(&a.0, &path).is_err());
    fs::write(&path, br#"{"v":1,"runId":"other-run","expiresAt":1}"#)?;
    let mut next = Slot::claim(&a.0, &path)?;
    assert_ne!(next.run, first_id);
    assert_eq!(
        serde_json::from_slice::<Value>(&fs::read(&path)?)?["expiresAt"],
        1
    );
    next.publish(&cfg(port, "CircularX"))?;
    drop(next);
    assert!(!path.try_exists()? && !path.with_extension("lock").try_exists()?);
    Ok(())
}

struct Live {
    tmp: Temp,
    wrapper: Child,
    pid: u32,
    port: u16,
    files: LaunchFiles,
}
impl Live {
    fn start() -> Result<Self> {
        let tmp = Temp::new()?;
        let node = node()?;
        let wrapper_exe = tmp.0.join("wrapper.exe");
        if fs::hard_link(&node, &wrapper_exe).is_err() {
            fs::copy(&node, &wrapper_exe)?;
        }
        let child = hidden_command(&wrapper_exe)
            .current_dir(&tmp.0)
            .args(["--no-global-search-paths", FIXTURE, "wrapper"])
            .arg(&node)
            .env_remove("NODE_OPTIONS")
            .env_remove("NODE_PATH")
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()?;
        let files = LaunchFiles {
            launcher_exe: wrapper_exe,
            game_exe: node.clone(),
            node_exe: node,
            server_dir: tmp.0.clone(),
        };
        let mut live = Self {
            tmp,
            wrapper: child,
            pid: 0,
            port: 0,
            files,
        };
        let until = Instant::now() + Duration::from_secs(8);
        loop {
            if let Ok(bytes) = fs::read(live.tmp.0.join("listening.json")) {
                (live.pid, live.port) = serde_json::from_slice(&bytes)?;
                break;
            }
            ensure!(
                Instant::now() < until && live.wrapper.try_wait()?.is_none(),
                "fixture did not bind"
            );
            thread::sleep(Duration::from_millis(20));
        }
        live.config("CircularX")?;
        Ok(live)
    }
    fn config(&self, map: &str) -> Result<()> {
        fs::write(
            self.tmp.0.join("serverconfig.json"),
            serde_json::to_vec(&json!({
                "mode":"pve","offline":true,"map":map,"difficulty":"normal","port":self.port,
                "externalPort":65535,"other":"player-setting",
            }))?,
        )?;
        Ok(())
    }
    fn publisher(&self) -> Result<Publisher> {
        let room = Room::pin(&self.files, self.pid)?;
        assert_eq!(room.wrapper.id, self.wrapper.id());
        let path = self.tmp.lease('a');
        let profile = Deployment {
            process: ProcessPin::open(std::process::id())?,
            lease: path.clone(),
        };
        Ok(Publisher {
            slot: Slot::claim(&self.tmp.0, &path)?,
            profile,
            room,
            selected: Selection::read(&self.files)?,
            files: self.files.clone(),
        })
    }
    fn signal(&self, name: &str) -> Result<()> {
        Ok(fs::write(self.tmp.0.join(name), b"")?)
    }
}

impl Drop for Live {
    fn drop(&mut self) {
        let game = ProcessPin::open(self.pid).ok();
        let _ = self.signal("stop");
        let _ = self.wrapper.wait();
        if let Some(p) = game {
            let _ = require_running([("本机对局", p.raw())], Duration::from_secs(3));
        }
    }
}

#[test]
fn actual_wrapper_game_and_udp_renew_then_revoke_without_touching_config() -> Result<()> {
    let live = Live::start()?;
    let config = live.tmp.0.join("serverconfig.json");
    let bytes = fs::read(&config)?;
    let pid = std::process::id();
    let file = live.tmp.lease('a');
    let (tx, rx) = mpsc::channel();
    live.publisher()?.open(
        move || Ok(Some(pid)),
        || {
            ensure!(
                serde_json::from_slice::<Value>(&fs::read(&file)?)?["port"] == live.port,
                "lobby before publication"
            );
            Ok(())
        },
        move |why| {
            let _ = tx.send(why);
        },
    )?;
    let first: Value = serde_json::from_slice(&fs::read(&file)?)?;
    let until = Instant::now() + RENEW * 2 + Duration::from_secs(2);
    loop {
        let current: Value = serde_json::from_slice(&fs::read(&file)?)?;
        assert_eq!(current["runId"], first["runId"]);
        if current["expiresAt"].as_u64() > first["expiresAt"].as_u64() {
            break;
        }
        ensure!(Instant::now() < until, "room did not renew");
        thread::sleep(Duration::from_millis(40));
    }
    live.signal("release-udp")?;
    assert!(rx.recv_timeout(RENEW * 2)?.contains("UDP"));
    assert!(!file.try_exists()?);
    require_running(
        [("本机对局", ProcessPin::open(live.pid)?.raw())],
        Duration::ZERO,
    )?;
    assert_eq!(fs::read(config)?, bytes);
    Ok(())
}

#[test]
fn changed_config_profile_and_failed_lobby_cannot_keep_a_lease() -> Result<()> {
    let live = Live::start()?;
    let file = live.tmp.lease('a');
    let id = std::process::id();
    assert!(
        live.publisher()?
            .open(
                || Ok(None),
                || panic!("wrong deployment opened lobby"),
                |_| {}
            )
            .is_err()
    );
    assert!(!file.try_exists()?);
    assert!(
        live.publisher()?
            .open(
                move || Ok(Some(id)),
                || bail!("fixture lobby failure"),
                |_| {}
            )
            .is_err()
    );
    assert!(!file.try_exists()?);
    let mut p = live.publisher()?;
    p.refresh(Some(id))?;
    assert!(p.refresh(Some(id.wrapping_add(1))).is_err());
    live.config("Warehouse")?;
    assert!(p.refresh(Some(id)).is_err());
    drop(p);
    assert!(!file.try_exists()?);
    Ok(())
}

#[test]
fn either_owned_process_exiting_revokes_a_published_room() -> Result<()> {
    for (stop, expected) in [
        ("stop-game", "本机对局已退出"),
        ("stop-wrapper", "本机对局启动器已退出"),
    ] {
        let live = Live::start()?;
        let (tx, rx) = mpsc::channel();
        let id = std::process::id();
        live.publisher()?.open(
            move || Ok(Some(id)),
            || Ok(()),
            move |s| {
                let _ = tx.send(s);
            },
        )?;
        live.signal(stop)?;
        assert_eq!(rx.recv_timeout(Duration::from_secs(3))?, expected);
        assert!(!live.tmp.lease('a').try_exists()?);
    }
    Ok(())
}

#[test]
fn cli_readiness_to_native_menu_then_shutdown_on_a_synthetic_install() -> Result<()> {
    let tmp = Temp::new()?;
    let node = node()?;
    let wrapper = tmp.0.join("brt-fixture-cli.exe");
    let source = Path::new(FIXTURE).with_file_name("cli_fixture.rs");
    let build = hidden_command("rustc")
        .arg("--edition=2024")
        .arg(source)
        .arg("-o")
        .arg(&wrapper)
        .current_dir(&tmp.0)
        .stderr(Stdio::piped())
        .output()?;
    ensure!(
        build.status.success(),
        "{}",
        String::from_utf8_lossy(&build.stderr)
    );
    let files = LaunchFiles {
        launcher_exe: wrapper,
        game_exe: node.clone(),
        node_exe: node.clone(),
        server_dir: tmp.0.clone(),
    };
    let port = UdpSocket::bind((Ipv4Addr::LOCALHOST, 0))?
        .local_addr()?
        .port();
    fs::write(tmp.0.join("test-port"), port.to_string())?;
    fs::write(tmp.0.join("test-node"), node.to_str().unwrap())?;
    fs::write(tmp.0.join("test-script"), FIXTURE)?;
    let path = tmp.0.join("serverconfig.json");
    let config = format!(
        r#"{{"mode":"pve","offline":true,"difficulty":"normal","map":"CircularX","port":{port},"externalPort":65535}}"#
    );
    fs::write(&path, &config)?;
    let definitions = files.server_dir.join("game/definitions");
    fs::create_dir_all(&definitions)?;
    fs::write(
        definitions.join("DT_GameModeDefinition.json"),
        r#"[{"Name":"DT_GameModeDefinition","Rows":{"Rush_PVE_Normal":{"MapsForMatch":[{"RowName":"CircularX"}]}}}]"#,
    )?;
    let selected = local_pve::preflight(&files)?;
    let pid = std::process::id();
    let profile = Deployment {
        process: ProcessPin::open(pid)?,
        lease: tmp.lease('a'),
    };
    let (tx, rx) = mpsc::channel();
    let ready = launch_verified(
        &tmp.0,
        &files,
        &selected,
        profile,
        move || Ok(Some(pid)),
        || {
            let found = native_reader(&tmp, 'a', "NORMAL BOT", 0)?;
            ensure!(
                found["status"] == "found" && found["serverPort"] == port,
                "missing native menu endpoint at lobby"
            );
            Ok(())
        },
        move |why| {
            let _ = tx.send(why);
        },
    )?;
    let room = Room::pin(&files, ready.pid)?;
    fs::write(tmp.0.join("stop"), [])?;
    let why = rx.recv_timeout(Duration::from_secs(4))?;
    assert!(
        matches!(why.as_str(), "本机对局已退出" | "本机对局启动器已退出"),
        "{why}"
    );
    assert!(
        require_running(
            [("本机对局启动器", room.wrapper.raw())],
            Duration::from_secs(3)
        )
        .is_err()
    );
    assert!(!tmp.lease('a').try_exists()?);
    assert_eq!(fs::read_to_string(path)?, config);
    Ok(())
}
