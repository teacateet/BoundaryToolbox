use super::super::super::hidden_command;
use super::*;
use std::process::Stdio;

const DIR: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/src/core/runtime_ops/local_pve"
);
struct Temp(PathBuf);
impl Drop for Temp {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}
struct Profile(Child);
impl Drop for Profile {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}
fn wait(root: &Path, mut test: impl FnMut() -> bool) -> Result<()> {
    let until = Instant::now() + Duration::from_secs(22);
    while !test() {
        if let Ok(reason) = fs::read_to_string(root.join("failure")) {
            bail!("{reason}");
        }
        ensure!(Instant::now() < until, "等待测试状态超时");
        thread::sleep(Duration::from_millis(35));
    }
    Ok(())
}

fn fixture_stage(
    files: &LaunchFiles,
    mode: &str,
    turn: u64,
) -> Result<(LaunchFiles, local_pve::Plan)> {
    ensure!(turn <= 1, "重复推进了轮换");
    let (files, plan) = local_pve::auto_room::stage(files, mode, turn)?;
    let dir = plan.dir();
    ensure!(Some(dir) != files.launcher_exe.parent(), "没有使用独立 CWD");
    fs::write(dir.join("test-port"), plan.port().to_string())?;
    fs::write(
        dir.join("test-script"),
        Path::new(DIR).join("lease_fixture.cjs").to_str().unwrap(),
    )?;
    fs::write(dir.join("test-node"), files.game_exe.to_str().unwrap())?;
    fs::write(
        dir.join("test-delay"),
        if turn == 0 { "5200" } else { "50" },
    )?;
    fs::write(
        dir.join("test-map"),
        if turn == 0 { "CircularX" } else { "OSS" },
    )?;
    let _ = fs::remove_file(dir.join("listening.json"));
    fs::write(files.server_dir.join("staging"), [])?;
    fs::write(dir.join("test-spawn-check"), [])?;
    Ok((files, plan))
}

#[test]
#[ignore = "启动一个隔离的已安装 MiniFarm 空专服；不启动或控制玩家客户端"]
fn installed_empty_room_receives_scoped_launch() -> Result<()> {
    use crate::core::{GAME_EXE, local_profile::room_attestation, process::launch_files};
    let target = PathBuf::from(std::env::var_os("BOUNDARY_QA_WIN64").context("Missing BOUNDARY_QA_WIN64")?).canonicalize()?;
    ensure!(target.join(GAME_EXE).is_file(),"未选择零售目录");
    let files = launch_files(&target);
    let port = std::net::UdpSocket::bind((Ipv4Addr::LOCALHOST,0))?.local_addr()?.port();
    let name = format!("BRT-Auto-normal-MiniFarm-{port}");
    let dir = target.join(&name);
    fs::create_dir(&dir)?;
    fs::write(dir.join("serverconfig.json"),serde_json::to_vec(&local_pve::auto_room::config("normal","MiniFarm",port))?)?;
    let lease = dir.join("isolated-test-lease.json");
    let key = room_attestation::session_key(&lease)?;
    let tested = (|| -> Result<()> {
        ensure!(room_attestation::read(&lease)? == key,"房间签名无法由同一 Windows 会话复用");
        ensure!(room_attestation::session_key(&lease)? == key,"再次开启大厅更换了已有房间的签名");
        let id = Uuid::new_v4();
        let grant = spawn_room::Grant::issue(id, "76561198185503627".into(), &Uuid::new_v4().to_string(), &key)?;
        let mut plan = local_pve::Plan::read_dir(&dir)?;
        plan.use_local_archive_host(&files)?;
        let mut again = local_pve::Plan::read_dir(&dir)?;
        ensure!(again.use_local_archive_host(&files).is_err(), "同一房间被重复取得");
        let mut active = start_room(&files,plan,&AtomicBool::new(false),Some(grant))?;
        active.check()?;
        use sysinfo::{Pid, ProcessRefreshKind, ProcessesToUpdate, System, UpdateKind};
        let id = Pid::from_u32(active.room.game.id);
        let mut system = System::new();
        system.refresh_processes_specifics(ProcessesToUpdate::Some(&[id]), true,
            ProcessRefreshKind::nothing().with_cmd(UpdateKind::Always));
        let args = system.process(id).context("无法核对专服进程")?.cmd();
        let logic = format!("-LogicServerURL={}", crate::core::LOCAL_LOGIC_SERVER_URL);
        ensure!(args.iter().filter(|s| s.to_string_lossy().starts_with("-LogicServerURL=")).count() == 1 &&
            args.iter().any(|s| s == std::ffi::OsStr::new(&logic)) &&
            args.iter().any(|s| s == "-server") &&
            !args.iter().any(|s| s.to_string_lossy().starts_with("-online=")),
            "本次零售专服没有收到唯一的本机档案地址，或启动了在线房间");
        println!("verified empty dedicated server: map={}, UDP={}, serverPid={}, launcherPid={}, retailArgLoopback=true, sameRoomLock=true, sessionKeyStable=true, phaseStarted={}, playerClient=false",
            active.cfg.map(),port,active.room.game.id,active.room.wrapper.id,active.round.started());
        active.retire()?;
        ensure!(Room::socket_owner(port)?.is_none(),"测试房没有释放自己的 UDP 端口");
        println!("verified dedicated server stopped; UDP released");
        Ok(())
    })();
    room_attestation::forget_test_key(&lease)?;
    tested?;
    let physical = dir.canonicalize()?;
    ensure!(physical.parent() == Some(target.as_path()) && physical == dir &&
        physical.file_name().and_then(|v|v.to_str()) == Some(name.as_str()),
        "测试房目录已改变");
    fs::remove_dir_all(physical)?;
    Ok(())
}

#[test]
fn menu_prepare_cancel_reselect_cold_renew_join_and_release() -> Result<()> {
    let tmp = Temp(std::env::temp_dir().join(format!("brt-pool-{}", Uuid::new_v4())));
    let root = &tmp.0;
    let rt = root.join("profile-runtime");
    fs::create_dir_all(&rt)?;
    let lease = rt.join(format!("pve-ready-{}.json", "a".repeat(64)));
    let table = root.join("game/definitions/DT_GameModeDefinition.json");
    fs::create_dir_all(table.parent().unwrap())?;
    let mut rows = serde_json::Map::new();
    for level in ["Easy", "Normal", "Hard"] {
        rows.insert(format!("Rush_PVE_{level}"),json!({"MatchmakingString":format!("{} BOT",level.to_uppercase()),
            "GameModeName":{"SourceString":level},"GameModeSimpleDescreiption":{"SourceString":level},
            "MapsForMatch":[{"RowName":"CircularX"},{"RowName":"OSS"}]}));
    }
    fs::write(
        &table,
        serde_json::to_vec(&json!([{"Name":"DT_GameModeDefinition","Rows":rows}]))?,
    )?;
    let game = root.join("node.exe");
    let node = hidden_command("node")
        .args(["-p", "process.execPath"])
        .stdout(Stdio::piped())
        .output()?;
    fs::copy(std::str::from_utf8(&node.stdout)?.trim(), &game)?;
    let cli = root.join("test-wrapper.exe");
    let built = hidden_command("rustc")
        .arg("--edition=2024")
        .arg(Path::new(DIR).join("cli_fixture.rs"))
        .arg("-o")
        .arg(&cli)
        .current_dir(root)
        .stderr(Stdio::piped())
        .output()?;
    ensure!(
        built.status.success(),
        "{}",
        String::from_utf8_lossy(&built.stderr)
    );
    let files = LaunchFiles {
        server_dir: root.clone(),
        game_exe: game.clone(),
        launcher_exe: cli,
        node_exe: game.clone(),
    };
    let node_cmd = || {
        let mut c = hidden_command(&game);
        c.current_dir(root)
            .env_remove("NODE_PATH")
            .env_remove("NODE_OPTIONS")
            .stderr(Stdio::inherit());
        c
    };
    let mut profile = Profile(
        node_cmd()
            .arg(Path::new(DIR).join("pool_profile_fixture.cjs"))
            .arg(concat!(
                env!("CARGO_MANIFEST_DIR"),
                "/integrations/native-menu/ready-room.cjs"
            ))
            .arg(&lease)
            .arg(table)
            .spawn()?,
    );
    let node_id = profile.0.id();
    wait(root, || root.join("rpc-port").is_file())?;
    let mut pool = Pool::open(
        root,
        Deployment {
            process: ProcessPin::open(node_id)?,
            lease: lease.clone(),
        },
    )?;
    pool.spawn_owner = || Some("76561198185503627".into());
    pool.spawn_signer = Some([0x73;32]);
    pool.publish(None)?;
    let receipt = || serde_json::from_slice::<Value>(&fs::read(&lease).ok()?).ok();
    let base = receipt().unwrap();
    assert!(base["port"].is_null() && base["joinIpc"].is_null());
    let log = root.join("pve-join.log");
    let output = root.join("commands");
    let lobby = node_cmd()
        .arg(Path::new(DIR).join("join_fixture.cjs"))
        .arg(format!("-pipe={}", pool.pipe))
        .arg(fs::read_to_string(root.join("rpc-port"))?)
        .arg(&output)
        .arg("reconnect")
        .spawn()?;
    let thread = thread::spawn(move || pool.run(lobby, files, || Ok(Some(node_id)), fixture_stage));
    wait(root, || {
        fs::read_to_string(&log)
            .unwrap_or_default()
            .contains("pipe-ready")
    })
    .context("大厅管道")?;
    assert!(!root.join("staging").exists());
    fs::write(root.join("start"), [])?;
    wait(root, || root.join("cancelled").exists()).context("取消第一次开房")?;
    fs::write(root.join("next"), [])?;
    wait(root, || {
        receipt().is_some_and(|v| {
            v["expiresAt"].as_u64() > base["expiresAt"].as_u64() && v["choiceId"].is_null()
        })
    })
    .context("冷启动续租")?;
    wait(root, || {
        fs::read_to_string(&log)
            .unwrap_or_default()
            .contains("join_ack")
    })
    .context("菜单交接")?;
    let ready: Value = serde_json::from_slice(&fs::read(root.join("assigned-1"))?)?;
    assert_eq!(ready["runId"], base["runId"]);
    assert_eq!(ready["difficulty"], "normal");
    assert_eq!(ready["map"], "CircularX");
    assert!(ready["choiceId"].as_str().is_some());
    let port = ready["port"].as_u64().unwrap() as u16;
    assert!(Room::socket_owner(port)?.is_some());
    let first_log = root.join(format!("BRT-Auto-normal-CircularX-{port}/logs/log-fixture.txt"));
    let child_proof: Value = serde_json::from_slice(&fs::read(first_log.parent().unwrap().parent().unwrap().join("test-spawn-child"))?)?;
    assert_eq!(child_proof["room"], format!("pve-{}", ready["choiceId"].as_str().unwrap()));
    assert_eq!(child_proof["verifier"], ready["verifier"]);
    assert_eq!(receipt().unwrap()["spawnChoice"], ready["choiceId"]);
    assert!(receipt().unwrap()["startedChoice"].is_null());
    {
        let mut out = fs::OpenOptions::new().append(true).open(first_log)?;
        writeln!(out, "[SERVER_LIFECYCLE] match_started")?;
    }
    wait(root, || receipt().is_some_and(|v| v["startedChoice"] == ready["choiceId"]))?;
    let commands = fs::read_to_string(&output)?;
    assert_eq!(
        commands
            .lines()
            .filter(|s| s.starts_with("join\t"))
            .collect::<Vec<_>>(),
        vec![format!("join\t{{\"ip\":\"127.0.0.1:{port}\"}}")]
    );
    assert_eq!(
        fs::read_to_string(lease.with_extension("turns"))?,
        "[0,1,0]"
    );
    let journal = fs::read_to_string(log)?;
    assert!(!journal.contains(base["controlIpc"]["nonce"].as_str().unwrap()));
    fs::write(root.join("finish-1"), [])?;
    wait(root, || {
        output.with_extension("disconnected").is_file()
            && receipt().is_some_and(|v| v["port"].is_null() && v["joinIpc"].is_null())
    })?;
    let old_pid = Room::socket_owner(port)?.context("断开大厅 RPC 后房间丢失")?;
    fs::write(output.with_extension("reconnect"), [])?;
    wait(root, || root.join("refused").is_file())?;
    assert_eq!(
        Room::socket_owner(port)?,
        Some(old_pid),
        "非法 prepare 释放了原房间"
    );
    fs::write(root.join("replace"), [])?;
    wait(root, || {
        fs::read_to_string(lease.with_extension("turns")).unwrap_or_default() == "[0,2,0]"
    })?;
    let second: Value = serde_json::from_slice(&fs::read(root.join("assigned-2"))?)?;
    assert_eq!(second["map"], "OSS");
    assert_ne!(second["choiceId"], ready["choiceId"]);
    assert_ne!(second["verifier"], ready["verifier"]);
    assert!(second["startedChoice"].is_null());
    let next_port = second["port"].as_u64().unwrap() as u16;
    assert_eq!(Room::socket_owner(port)?, None);
    let next_pid = Room::socket_owner(next_port)?.context("第二场房间未启动")?;
    profile.0.kill()?;
    wait(root, || !lease.exists())?;
    assert_eq!(
        Room::socket_owner(next_port)?,
        Some(next_pid),
        "档案退出时过早释放房间"
    );
    fs::write(output.with_extension("exit"), [])?;
    assert!(thread.join().unwrap().is_err());
    wait(root, || Room::socket_owner(next_port).ok() == Some(None))?;
    Ok(())
}
