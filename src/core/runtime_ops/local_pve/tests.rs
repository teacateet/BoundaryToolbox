use super::*;
use std::cell::Cell;
use std::fs;
use std::io::Write;
use std::net::UdpSocket;
use uuid::Uuid;

struct Install(PathBuf);

impl Install {
    fn new() -> Self {
        let dir = std::env::temp_dir().join(format!("brt-pve-{}", Uuid::new_v4()));
        fs::create_dir(&dir).unwrap();
        Self(dir)
    }

    fn plan(&self, bytes: &[u8]) -> Result<Plan> {
        fs::write(self.0.join("serverconfig.json"), bytes)?;
        Plan::read(&self.0.join("wrapper.exe"))
    }

    fn configured(&self, port: u16) -> Plan {
        self.plan(format!(r#"{{"mode":"pve","port":{port},"externalPort":32712,"map":"CircularX","difficulty":"hard"}}"#).as_bytes()).unwrap()
    }

    fn log(&self) -> PathBuf {
        let dir = self.0.join("logs");
        fs::create_dir_all(&dir).unwrap();
        dir.join("log-20261101_010203.txt")
    }
}

impl Drop for Install {
    fn drop(&mut self) {
        let _ = fs::remove_file(self.0.join("serverconfig.json"));
        let logs = self.0.join("logs");
        if let Ok(entries) = fs::read_dir(&logs) {
            for entry in entries.flatten() {
                let _ = fs::remove_file(entry.path());
            }
            let _ = fs::remove_dir(logs);
        }
        let _ = fs::remove_dir(&self.0);
    }
}

fn lines(plan: &Plan, pid: u32) -> Vec<String> {
    vec![
        format!("[Launcher] Server launched. PID = {pid}"),
        format!("[SERVER] Map loaded: {}", plan.map),
        format!("[SERVER] Mode: {}", plan.mode_class),
        format!("[SERVER] Port: {}", plan.endpoint.port()),
        "[SERVER] Server is now listening.".into(),
    ]
}

#[test]
fn menu_map_uses_selected_difficulty_and_rejects_invalid_rows() {
    let table = br#"[{"Name":"DT_GameModeDefinition","Rows":{
        "Rush_PVE_Easy":{"MapsForMatch":[{"RowName":"warehouse"}]},
        "Rush_PVE_Normal":{"MapsForMatch":[{"RowName":"CircularX"},{"RowName":"OSS"}]},
        "Rush_PVE_Hard":{"MapsForMatch":[{}]}}}]"#;
    assert_eq!(menu_maps(table, "Rush_PVE_Normal").unwrap(), ["CircularX", "OSS"]);
    assert_eq!(menu_maps(table, "Rush_PVE_Easy").unwrap(), ["Warehouse"]);
    assert!(menu_maps(table, "Rush_PVE_Hard").is_err());
    assert!(menu_maps(b"[]", "Rush_PVE_Normal").is_err());
}

#[test]
fn pve_opens_only_lobby_and_explicit_join_keeps_remote_endpoint() {
    use super::super::{LOCAL_LOGIC_SERVER_URL, LaunchMode, game_client_command};
    let endpoint = "192.0.2.11:39242".parse().unwrap();
    let logic = format!("-LogicServerURL={LOCAL_LOGIC_SERVER_URL}");
    for (mode, expected) in [
        (LaunchMode::Pve, vec![logic.clone(), "-BrtLocalPveUi".to_owned()]),
        (LaunchMode::Pvp, vec![logic.clone()]),
        (
            LaunchMode::Join(endpoint),
            vec![logic, format!("-match={endpoint}")],
        ),
    ] {
        let dir = Path::new("D:/fixture/Win64");
        let exe = dir.join("fixture.exe");
        let cmd = game_client_command(dir, &exe, mode);
        assert_eq!(cmd.get_current_dir(), Some(dir));
        assert_eq!(cmd.get_program(), exe.as_os_str());
        assert_eq!(
            cmd.get_args()
                .map(|a| a.to_string_lossy())
                .collect::<Vec<_>>(),
            expected
        );
    }
}

#[test]
fn selected_config_is_locked_read_only_and_never_uses_advertised_port() {
    let a = Install::new();
    let b = Install::new();
    let one = a.configured(39241);
    let other = b.configured(39242);
    assert_eq!(
        one.endpoint,
        "127.0.0.1:39241".parse::<SocketAddrV4>().unwrap()
    );
    assert_eq!(other.endpoint.port(), 39242);
    assert_eq!(other.map, "CircularX");
    assert!(other.mode_class.ends_with("Hard_C"));
    let path = a.0.join("serverconfig.json");
    let original = fs::read(&path).unwrap();
    assert!(OpenOptions::new().write(true).open(&path).is_err());
    drop(one);
    assert_eq!(fs::read(path).unwrap(), original);
}

#[test]
fn missing_broken_or_non_pve_config_never_creates_defaults() {
    let tmp = Install::new();
    assert!(Plan::read(&tmp.0.join("wrapper.exe")).is_err());
    assert!(!tmp.0.join("serverconfig.json").exists());
    for input in [
        "{",
        "{}",
        r#"{"mode":"pvp","port":39241}"#,
        r#"{"mode":"PVE","port":39241}"#,
        r#"{"mode":"pve","externalPort":39242}"#,
        r#"{"mode":"pve","port":0}"#,
        r#"{"mode":"pve","port":"39241"}"#,
        r#"{"mode":"pve","port":65536}"#,
        r#"{"mode":"pve","port":9000}"#,
        r#"{"mode":"pve","port":39241,"difficulty":"unknown"}"#,
    ] {
        assert!(tmp.plan(input.as_bytes()).is_err(), "{input}");
        assert_eq!(
            fs::read(tmp.0.join("serverconfig.json")).unwrap(),
            input.as_bytes()
        );
    }
    let plan = tmp
        .plan(b"\xef\xbb\xbf{\"mode\":\"pve\",\"port\":39241}")
        .unwrap();
    assert_eq!(plan.map, "Warehouse");
    assert!(plan.mode_class.ends_with("Normal_C"));
}

#[test]
fn verified_endpoint_opens_a_single_lobby_only_after_listen_and_owner() {
    let tmp = Install::new();
    let plan = tmp.configured(39241);
    let logs = StartupLogs::baseline(&tmp.0).unwrap();
    let count = Cell::new(0);
    fs::write(tmp.log(), lines(&plan, 27).join("\r\n") + "\r\n").unwrap();
    let (pid, _) = wait_then_open(
        &plan,
        &logs,
        Duration::from_secs(2),
        || Ok(true),
        |id| {
            assert_eq!(id, 27);
            assert_eq!(count.get(), 0);
            Ok(true)
        },
        || {
            count.set(count.get() + 1);
            Ok(())
        },
    )
    .unwrap();
    assert_eq!(pid, 27);
    assert_eq!(count.get(), 1);
}

#[test]
fn no_lobby_for_partial_log_dead_wrapper_wrong_room_conflict_or_timeout() {
    let tmp = Install::new();
    let plan = tmp.configured(39241);
    let logs = StartupLogs::baseline(&tmp.0).unwrap();
    let correct = lines(&plan, 27);
    let mut cases = Vec::new();
    for missing in 0..correct.len() {
        let mut log = correct.clone();
        log.remove(missing);
        cases.push((log, true, true));
    }
    for bad in [
        "[SERVER] Port: 32712",
        "[SERVER] Map loaded: Warehouse",
        "[SERVER] Mode: /Game/Online/GameMode/PBGameMode_Rush_BP.PBGameMode_Rush_BP_C",
        "[Launcher] Server launched. PID = 28",
        "[Launcher] Server exited unexpectedly.",
        "[Launcher] ERROR: UDP Port 39241 is already in use!",
    ] {
        let mut log = correct.clone();
        log.push(bad.into());
        cases.push((log, true, true));
    }
    cases.push((
        vec![
            "[SERVER] External port: 39241".into(),
            "[SERVER] Server is now listening.".into(),
        ],
        true,
        true,
    ));
    cases.push((correct.clone(), false, true));
    cases.push((correct.clone(), true, false));
    for (log, live, owned) in cases {
        fs::write(tmp.log(), log.join("\n") + "\n").unwrap();
        assert!(
            wait_then_open(
                &plan,
                &logs,
                Duration::from_millis(30),
                || Ok(live),
                |_| Ok(owned),
                || panic!("unverified lobby")
            )
            .is_err()
        );
    }
    fs::remove_file(tmp.log()).unwrap();
    assert!(
        wait_then_open(
            &plan,
            &logs,
            Duration::from_millis(5),
            || Ok(true),
            |_| Ok(true),
            || panic!("timed-out lobby")
        )
        .unwrap_err()
        .to_string()
        .contains("超时")
    );
    fs::write(tmp.log(), correct.join("\n") + "\n").unwrap();
    let live = Cell::new(true);
    assert!(
        wait_then_open(
            &plan,
            &logs,
            Duration::from_millis(500),
            || Ok(live.replace(false)),
            |_| Ok(true),
            || panic!("wrapper exited before lobby")
        )
        .is_err()
    );
}

#[test]
fn kernel_udp_ownership_rejects_an_existing_room_without_closing_it() {
    let socket = UdpSocket::bind((Ipv4Addr::LOCALHOST, 0)).unwrap();
    let port = socket.local_addr().unwrap().port();
    let pid = std::process::id();
    let tmp = Install::new();
    let plan = tmp.configured(port);
    assert_eq!(udp_binding(port, None).unwrap(), Binding::Other);
    assert_eq!(udp_binding(port, Some(pid)).unwrap(), Binding::Ours);
    assert!(
        plan.check_available(&tmp.0.join("wrapper.exe"))
            .unwrap_err()
            .to_string()
            .contains("已被占用")
    );
    let mut sys = System::new();
    let id = Pid::from_u32(pid);
    sys.refresh_processes_specifics(
        ProcessesToUpdate::Some(&[id]),
        true,
        ProcessRefreshKind::nothing(),
    );
    let parent = sys.process(id).unwrap().parent().unwrap().as_u32();
    let exe = std::env::current_exe().unwrap();
    assert!(owns_room(&exe, parent, pid, port).unwrap());
    assert!(!owns_room(&exe, pid, pid, port).unwrap());
    assert!(!owns_room(&tmp.0.join("other.exe"), parent, pid, port).unwrap());
    socket.send_to(b"x", socket.local_addr().unwrap()).unwrap();
    socket
        .set_read_timeout(Some(Duration::from_secs(1)))
        .unwrap();
    let mut byte = [0; 1];
    assert_eq!(socket.recv(&mut byte).unwrap(), 1);
    assert_eq!(&byte, b"x");
}

#[test]
fn stale_partial_and_separate_files_cannot_complete_a_room() -> Result<()> {
    let tmp = Install::new();
    let plan = tmp.configured(39241);
    let file = tmp.log();
    let markers = lines(&plan, 42);
    fs::write(&file, markers.join("\n") + "\nhistorical partial")?;
    let logs = StartupLogs::baseline(&tmp.0)?;
    assert!(logs.ready_log(&plan)?.is_none());
    let mut append = OpenOptions::new().append(true).open(&file)?;
    writeln!(append, "{}", markers[0])?; // 启动前的半行不能给本次填入 PID。
    writeln!(append, "{}", markers[1..].join("\n"))?;
    fs::write(
        file.with_file_name("log-other.txt"),
        format!("{}\n", markers[0]),
    )?;
    assert!(logs.ready_log(&plan)?.is_none());
    write!(append, "{}", markers[0])?;
    assert!(logs.ready_log(&plan)?.is_none());
    writeln!(append)?;
    let (pid, mut round) = logs.ready_log(&plan)?.unwrap();
    assert_eq!(pid, 42);
    fs::write(file.with_file_name("log-other.txt"), "[SERVER_LIFECYCLE] match_started\n")?;
    writeln!(append, "[HEARTBEAT] PlayerCount = 2")?;
    write!(append, "[SERVER_LIFECYCLE] match_sta")?;
    assert!(!round.started());
    writeln!(append, "rted\r")?;
    assert!(round.started());
    let (_, mut ending_round) = logs.ready_log(&plan)?.unwrap();
    writeln!(append, "[SERVER_LIFECYCLE] match_end_detected reason=host-shutdown")?;
    assert!(!ending_round.started());
    assert!(logs.ready_log(&plan).is_err());
    writeln!(append, "[Launcher] Server launched. PID = 43")?;
    writeln!(append, "[SERVER_LIFECYCLE] match_started")?;
    assert!(!round.started());
    Ok(())
}

#[test]
fn hidden_fixture_log_and_kernel_owner_gate_the_lobby() -> Result<()> {
    let tmp = Install::new();
    let log = tmp.log();
    let logs = StartupLogs::baseline(&tmp.0)?;
    let exe = PathBuf::from(std::env::var_os("SystemRoot").context("missing SystemRoot")?)
        .join(r"System32\WindowsPowerShell\v1.0\powershell.exe");
    // 夹具自行申请临时端口，不经 stdout，延迟写入真实启动器格式的就绪记录。
    let script = r#"
$ErrorActionPreference = 'Stop'
$udp = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
$port = $udp.Client.LocalEndPoint.Port
[IO.File]::WriteAllLines($env:BRT_FIXTURE_LOG, [string[]]@(
    "[Launcher] Server launched. PID = $PID",
    '[SERVER] Map loaded: CircularX',
    '[SERVER] Mode: /Game/Online/GameMode/BP_PBGameMode_Rush_PVE_Hard.BP_PBGameMode_Rush_PVE_Hard_C',
    "[SERVER] Port: $port"))
Start-Sleep -Milliseconds 200
[IO.File]::AppendAllText($env:BRT_FIXTURE_LOG, "[SERVER] Server is now listening.`r`n")
Start-Sleep -Seconds 20
"#;
    let mut child = hidden_command(&exe)
        .args([
            "-NoLogo",
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            script,
        ])
        .env("BRT_FIXTURE_LOG", &log)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()?;
    let result = (|| -> Result<()> {
        let until = Instant::now() + Duration::from_secs(10);
        let port = loop {
            if let Ok(text) = fs::read_to_string(&log)
                && let Some(port) = text.lines().find_map(|s| s.strip_prefix("[SERVER] Port: "))
            {
                break port.parse()?;
            }
            ensure!(
                child.try_wait()?.is_none() && Instant::now() < until,
                "fixture port not published"
            );
            thread::sleep(Duration::from_millis(20));
        };
        let plan = tmp.configured(port);
        let opened = Cell::new(0);
        wait_then_open(
            &plan,
            &logs,
            Duration::from_secs(5),
            || Ok(child.try_wait()?.is_none()),
            |pid| owns_room(&exe, std::process::id(), pid, port),
            || {
                opened.set(opened.get() + 1);
                Ok(())
            },
        )?;
        ensure!(opened.get() == 1, "lobby callback count");
        Ok(())
    })();
    let _ = child.kill();
    child.wait()?;
    result
}
