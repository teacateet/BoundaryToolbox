//! 用所选安装中的社区启动器管理独立 PvP 房间。

use std::collections::{HashMap, HashSet};
use std::fs::{self, File, OpenOptions};
use std::io::{ErrorKind, Read, Seek, SeekFrom, Write};
use std::os::windows::fs::MetadataExt;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::atomic::{AtomicU8, Ordering};
use std::thread;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, bail, ensure};
use netstat2::{AddressFamilyFlags, ProtocolFlags, ProtocolSocketInfo, get_sockets_info};
use serde::{Deserialize, Serialize};
use sysinfo::{Pid, ProcessRefreshKind, ProcessesToUpdate, System, UpdateKind};
use uuid::Uuid;

use super::{GAME_EXE, pathing::validate_win64_path, process::kill_pids, util::hidden_command};
use super::runtime_ops::local_pve::auto_room;

const WRAPPER: &str = "ServerLauncher/ProjectReboundServerLauncher.exe";
const STATE: &str = "orbital-host.json";
const PVP_MODE: &str =
    "[SERVER] Mode: /Game/Online/GameMode/PBGameMode_Rush_BP.PBGameMode_Rush_BP_C";
const DEADLINE: Duration = Duration::from_secs(90);
pub(crate) mod player_count;

#[derive(Default)]
pub(crate) struct StartAttempt(AtomicU8);

impl StartAttempt {
    // 启动完成与取消争用同一次决定。复用已有房间时不会取得其进程所有权。
    pub(crate) fn cancel(&self) -> bool {
        self.0.compare_exchange(0, 1, Ordering::AcqRel, Ordering::Acquire).is_ok()
    }
    fn cancelled(&self) -> bool { self.0.load(Ordering::Acquire) == 1 }
    fn accept_ready(&self) -> bool {
        self.0.compare_exchange(0, 2, Ordering::AcqRel, Ordering::Acquire).is_ok()
    }
}

#[derive(Serialize, Deserialize)]
struct Owner {
    pid: u32,
    token: String,
}

pub(crate) fn valid_room_port(port: u16) -> bool {
    port >= 1024 && ![6969, 8000, 9000, 9001].contains(&port)
}

fn room_path(target: &Path, port: u16) -> Result<PathBuf> {
    validate_win64_path(target)?;
    ensure!(
        valid_room_port(port),
        "请选择 1024 至 65535 之间空闲的游戏端口，6969、8000、9000、9001 供档案服务使用"
    );
    let legacy = target.join(format!("BoundaryOrbital-PvP-{port}"));
    Ok(if legacy.try_exists()? { legacy } else { target.join(format!("BRT-PvP-{port}")) })
}

fn ordinary(path: &Path, dir: bool) -> Result<()> {
    let meta = fs::symlink_metadata(path)?;
    ensure!(
        meta.file_attributes() & 0x400 == 0 && if dir { meta.is_dir() } else { meta.is_file() },
        "房间路径类型异常：{}",
        path.display()
    );
    Ok(())
}

fn room_lock(room: &Path) -> Result<File> {
    ordinary(room, true)?;
    let file = room.join("orbital.lock");
    let handle = match File::create_new(&file) {
        Ok(file) => file,
        Err(e) if e.kind() == ErrorKind::AlreadyExists => {
            ordinary(&file, false)?;
            OpenOptions::new().read(true).write(true).open(&file)?
        }
        Err(e) => return Err(e.into()),
    };
    handle.try_lock().context("另一个工具箱正在操作这个房间")?;
    Ok(handle)
}

fn saved_owner(room: &Path) -> Result<Option<Owner>> {
    let path = room.join(STATE);
    if !path.try_exists()? {
        return Ok(None);
    }
    ordinary(&path, false)?;
    Ok(Some(serde_json::from_slice(&fs::read(path)?)?))
}

fn process_snapshot(ids: &[Pid]) -> System {
    let mut system = System::new();
    system.refresh_processes_specifics(
        ProcessesToUpdate::Some(ids),
        true,
        ProcessRefreshKind::nothing()
            .with_exe(UpdateKind::Always)
            .with_cwd(UpdateKind::Always)
            .with_cmd(UpdateKind::Always),
    );
    system
}

fn same_file(left: Option<&Path>, right: &Path) -> bool {
    left.and_then(|p| p.canonicalize().ok())
        .zip(right.canonicalize().ok())
        .is_some_and(|(a, b)| a.as_os_str().eq_ignore_ascii_case(b.as_os_str()))
}

fn is_owner(owner: &Owner, target: &Path, room: &Path) -> bool {
    let pid = Pid::from_u32(owner.pid);
    let system = process_snapshot(&[pid]);
    let Some(proc) = system.process(pid) else {
        return false;
    };
    if Uuid::parse_str(&owner.token).is_err() {
        return false;
    }
    let token = format!("-serverid={}", owner.token);
    same_file(proc.exe(), &target.join(WRAPPER))
        && same_file(proc.cwd(), room)
        && matches!(proc.cmd(), [_, cli, arg] if cli == "-cli" && arg == token.as_str())
}

fn udp_owners(port: u16) -> Result<HashSet<u32>> {
    let mut pids = HashSet::new();
    for socket in get_sockets_info(
        AddressFamilyFlags::IPV4 | AddressFamilyFlags::IPV6,
        ProtocolFlags::UDP,
    )? {
        if matches!(socket.protocol_socket_info, ProtocolSocketInfo::Udp(ref udp) if udp.local_port == port)
        {
            // 无进程归属的套接字也应当作已占用。
            if socket.associated_pids.is_empty() {
                pids.insert(0);
            }
            pids.extend(socket.associated_pids);
        }
    }
    Ok(pids)
}

fn game_has_port(target: &Path, wrapper_pid: u32, port: u16) -> Result<bool> {
    let owners = udp_owners(port)?;
    if owners.len() != 1 {
        return Ok(false);
    }
    let Some(id) = owners.into_iter().next().filter(|id| *id != 0) else {
        return Ok(false);
    };
    let pid = Pid::from_u32(id);
    let sys = process_snapshot(&[pid]);
    Ok(sys.process(pid).is_some_and(|p| {
        p.parent() == Some(Pid::from_u32(wrapper_pid)) && same_file(p.exe(), &target.join(GAME_EXE))
    }))
}

fn configure(room: &Path, port: u16) -> Result<()> {
    let path = room.join("serverconfig.json");
    let config = serde_json::json!({"map":"Warehouse", "mode":"pvp", "serverName":"BRT", "serverRegion":"LOCAL", "offline":true, "port":port, "externalPort":port});
    match File::create_new(&path) {
        Ok(mut file) => file.write_all(&serde_json::to_vec_pretty(&config)?)?,
        Err(e) if e.kind() == ErrorKind::AlreadyExists => {
            ordinary(&path, false)?;
            let value: serde_json::Value = serde_json::from_slice(&fs::read(&path)?)?;
            for field in ["map", "mode", "offline", "port", "externalPort"] {
                ensure!(
                    value[field] == config[field],
                    "房间配置 {field} 已被修改，请检查 {}",
                    path.display()
                );
            }
        }
        Err(e) => return Err(e.into()),
    }
    Ok(())
}

fn log_ends(room: &Path) -> Result<HashMap<PathBuf, u64>> {
    let mut files = HashMap::new();
    let dir = room.join("logs");
    if !dir.try_exists()? {
        return Ok(files);
    }
    ordinary(&dir, true)?;
    for entry in fs::read_dir(dir)? {
        let entry = entry?;
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if name.starts_with("log-") && name.ends_with(".txt") && entry.file_type()?.is_file() {
            files.insert(entry.path(), entry.metadata()?.len());
        }
    }
    Ok(files)
}

fn startup_log(room: &Path, baseline: &HashMap<PathBuf, u64>, map: &str, mode_line: &str) -> Result<bool> {
    for (path, size) in log_ends(room)? {
        let offset = baseline.get(&path).copied().unwrap_or(0);
        if size == offset {
            continue;
        }
        let mut file = File::open(&path)?;
        file.seek(SeekFrom::Start(if size < offset { 0 } else { offset }))?;
        let mut bytes = Vec::new();
        file.take(512 * 1024).read_to_end(&mut bytes)?;
        let log = String::from_utf8_lossy(&bytes);
        ensure!(
            !log.contains("[Launcher] Server exited unexpectedly.")
                && !log.contains("[Launcher] CRITICAL:"),
            "游戏在开放房间前退出，详见 {}",
            path.display()
        );
        let map_line = format!("[SERVER] Map loaded: {map}");
        if log.lines().any(|line| line.trim_end() == map_line)
            && log.lines().any(|line| line.trim_end() == mode_line)
            && log.lines().any(|line| line.trim_end() == "[SERVER] Server is now listening.")
        {
            return Ok(true);
        }
    }
    Ok(false)
}

pub(crate) fn start(target: &Path, port: u16) -> Result<()> {
    start_room(target, port, false, &StartAttempt::default()).map(|_| ())
}

pub(crate) fn start_pve(target: &Path, port: u16) -> Result<()> {
    start_room(target, port, true, &StartAttempt::default()).map(|_| ())
}

fn coop_path(target: &Path, port: u16) -> PathBuf {
    target.join(format!("BRT-Coop-{port}"))
}

/// 分享或加入自己的房间前，核对本次安装的专服仍拥有游戏端口。
pub(crate) fn owned_room_listening(target: &Path, port: u16) -> Result<bool> {
    for dir in [room_path(target, port)?, coop_path(target, port)] {
        if let Some(owner) = saved_owner(&dir)?
            && is_owner(&owner, target, &dir) && game_has_port(target, owner.pid, port)? {
            return Ok(true);
        }
    }
    Ok(false)
}

fn write_room_value(path: &Path, value: &serde_json::Value) -> Result<()> {
    let tmp = path.with_extension(format!("{}.tmp", Uuid::new_v4()));
    let mut file = File::create_new(&tmp)?;
    let write = file.write_all(&serde_json::to_vec_pretty(value)?);
    drop(file);
    if let Err(error) = write.and_then(|_| fs::rename(&tmp, path)) {
        let _ = fs::remove_file(tmp);
        return Err(error.into());
    }
    Ok(())
}

fn coop_config(target: &Path, room: &Path, port: u16) -> Result<(String, String, u64)> {
    let rotation = room.join("rotation.json");
    let turn: u64 = if rotation.try_exists()? {
        ordinary(&rotation, false)?;
        serde_json::from_slice(&fs::read(&rotation)?)?
    } else { 0 };
    let files = super::process::launch_files(target);
    let (difficulty, map) = auto_room::coop_choice(&files, turn)?;
    let config = auto_room::config(difficulty, &map, port);
    let path = room.join("serverconfig.json");
    if path.try_exists()? {
        ordinary(&path, false)?;
        let saved: serde_json::Value = serde_json::from_slice(&fs::read(&path)?)?;
        let previous = auto_room::coop_choice(&files, turn.saturating_sub(1))?;
        ensure!(saved == config || saved == auto_room::config(previous.0, &previous.1, port),
            "好友房间配置已由其他程序修改，请检查 {}", path.display());
    }
    write_room_value(&path, &config)?;
    let suffix = match difficulty { "easy" => "Easy", "normal" => "Normal", "hard" => "Hard", _ => bail!("不支持的 PvE 难度") };
    let bp = format!("BP_PBGameMode_Rush_PVE_{suffix}");
    Ok((map, format!("[SERVER] Mode: /Game/Online/GameMode/{bp}.{bp}_C"), turn))
}

pub(crate) fn start_room(target: &Path, port: u16, coop: bool, attempt: &StartAttempt) -> Result<bool> {
    if attempt.cancelled() { return Ok(false); }
    let room = room_path(target, port)?;
    let room = if coop { coop_path(target, port) } else { room };
    ensure!(
        target.join(WRAPPER).is_file(),
        "所选安装缺少社区服务器启动器"
    );
    match fs::create_dir(&room) {
        Ok(()) => (),
        Err(e) if e.kind() == ErrorKind::AlreadyExists => (),
        Err(e) => return Err(e).context("创建固定服房间目录失败"),
    }
    let _lock = room_lock(&room)?;
    if attempt.cancelled() { return Ok(false); }
    if let Some(owner) = saved_owner(&room)? {
        if is_owner(&owner, target, &room) {
            ensure!(
                game_has_port(target, owner.pid, port)?,
                "此房间进程仍在切图，请稍后重试"
            );
            return Ok(attempt.accept_ready());
        }
        fs::remove_file(room.join(STATE))?;
    }
    ensure!(
        udp_owners(port)?.is_empty(),
        "UDP {port} 正被其他程序使用，请选择其他端口"
    );
    let (map, mode, turn) = if coop {
        coop_config(target, &room, port)?
    } else {
        configure(&room, port)?;
        ("Warehouse".to_owned(), PVP_MODE.to_owned(), 0)
    };
    let baseline = log_ends(&room)?;
    if attempt.cancelled() { return Ok(false); }
    let token = Uuid::new_v4().simple().to_string();
    // 游戏按启动工作目录查找相对资源，房间必须与原 ServerLauncher 同级。
    let mut child = hidden_command(target.join(WRAPPER))
        .current_dir(&room)
        .arg("-cli")
        .arg(format!("-serverid={token}"))
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .context("启动好友房间失败")?;
    let owner = Owner {
        pid: child.id(),
        token,
    };
    let state = room.join(STATE);
    let result = (|| -> Result<bool> {
        let mut proof = File::create_new(&state)?;
        proof.write_all(&serde_json::to_vec(&owner)?)?;
        proof.sync_all()?;
        let started = Instant::now();
        while started.elapsed() < DEADLINE {
            if attempt.cancelled() { return Ok(false); }
            if let Some(status) = child.try_wait()? {
                bail!("固定服启动器已退出，退出码 {status}");
            }
            if startup_log(&room, &baseline, &map, &mode)? && game_has_port(target, owner.pid, port)? {
                if !attempt.accept_ready() { return Ok(false); }
                if coop {
                    write_room_value(&room.join("rotation.json"), &serde_json::json!(turn + 1))?;
                }
                return Ok(true);
            }
            thread::sleep(Duration::from_millis(500));
        }
        bail!(
            "等待 UDP {port} 开放超时，详见 {}",
            room.join("logs").display()
        );
    })();
    if !matches!(result, Ok(true)) {
        if child.try_wait()?.is_none() {
            kill_pids(&[child.id()])?;
            child.wait()?;
        }
        if state.try_exists()? {
            fs::remove_file(state)?;
        }
    }
    result
}

pub(crate) fn stop(target: &Path, port: u16) -> Result<bool> {
    let room = room_path(target, port)?;
    let pvp = stop_room(target, &room)?;
    let coop = stop_room(target, &coop_path(target, port))?;
    Ok(pvp || coop)
}

fn stop_room(target: &Path, room: &Path) -> Result<bool> {
    if !room.try_exists()? {
        return Ok(false);
    }
    let _lock = room_lock(&room)?;
    let Some(owner) = saved_owner(&room)? else {
        return Ok(false);
    };
    let ours = is_owner(&owner, target, &room);
    if ours {
        kill_pids(&[owner.pid])?;
    }
    fs::remove_file(room.join(STATE))?;
    Ok(ours)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cancellation_never_adopts_or_stops_an_existing_room() {
        let cancelled = StartAttempt::default();
        assert!(cancelled.cancel());
        assert!(!cancelled.accept_ready() && !cancelled.cancel());
        let ready = StartAttempt::default();
        assert!(ready.accept_ready());
        assert!(!ready.cancel());
    }

    #[test]
    #[ignore = "使用真实社区启动器验证建房取消、清理和同端口重试"]
    fn installed_cancel_and_retry_same_port() -> Result<()> {
        let target = PathBuf::from(std::env::var_os("BOUNDARY_QA_WIN64").context("Missing BOUNDARY_QA_WIN64")?);
        let port: u16 = std::env::var("BOUNDARY_QA_PORT")?.parse()?;
        let dir = room_path(&target, port)?;
        ensure!(!dir.try_exists()? && udp_owners(port)?.is_empty(), "Test room must be unused");
        let attempt = std::sync::Arc::new(StartAttempt::default());
        let worker = {
            let token = attempt.clone();
            let target = target.clone();
            thread::spawn(move || start_room(&target, port, false, &token))
        };
        let until = Instant::now() + Duration::from_secs(8);
        while !dir.join(STATE).is_file() && Instant::now() < until && !worker.is_finished() {
            thread::sleep(Duration::from_millis(15));
        }
        let spawned = dir.join(STATE).is_file();
        let cancelled = attempt.cancel();
        let result = worker.join().expect("room worker panicked")?;
        ensure!(spawned && cancelled && !result, "Did not cancel a spawned room");
        ensure!(!dir.join(STATE).exists() && udp_owners(port)?.is_empty(), "Cancellation left ownership or listener");
        start(&target, port)?;
        let listening = saved_owner(&dir)?.is_some_and(|owner| game_has_port(&target, owner.pid, port).unwrap_or(false));
        let stopped = stop(&target, port)?;
        ensure!(listening && stopped && udp_owners(port)?.is_empty(), "Retry or stop failed");
        Ok(())
    }

    #[test]
    fn readiness_requires_fresh_matching_native_messages() -> Result<()> {
        let dir = std::env::temp_dir().join(Uuid::new_v4().to_string());
        fs::create_dir_all(dir.join("logs"))?;
        let file = dir.join("logs/log-1.txt");
        let good = format!(
            "[SERVER] Map loaded: Warehouse\n{PVP_MODE}\n[SERVER] Server is now listening.\n"
        );
        fs::write(&file, &good)?;
        let baseline = log_ends(&dir)?;
        assert!(!startup_log(&dir, &baseline, "Warehouse", PVP_MODE)?);
        OpenOptions::new()
            .append(true)
            .open(&file)?
            .write_all(good.as_bytes())?;
        assert!(startup_log(&dir, &baseline, "Warehouse", PVP_MODE)?);
        assert!(!startup_log(&dir, &baseline, "MiniFarm", PVP_MODE)?);
        OpenOptions::new()
            .append(true)
            .open(&file)?
            .write_all(b"[Launcher] Server exited unexpectedly.")?;
        assert!(startup_log(&dir, &baseline, "Warehouse", PVP_MODE).is_err());
        fs::remove_dir_all(dir)?;
        Ok(())
    }

    #[test]
    #[ignore = "使用已安装的社区服务器验证好友 PvE 开房和轮换"]
    fn installed_coop_start_reuse_rotate_and_stop() -> Result<()> {
        let target = PathBuf::from(std::env::var_os("BOUNDARY_QA_WIN64").context("Missing BOUNDARY_QA_WIN64")?);
        let port: u16 = std::env::var("BOUNDARY_QA_PORT")?.parse()?;
        let dir = coop_path(&target, port);
        ensure!(!dir.try_exists()? && udp_owners(port)?.is_empty(), "需要未使用的测试房间");
        let original = target.join("ServerLauncher/serverconfig.json");
        let original_bytes = fs::read(&original)?;
        for expected in ["Warehouse", "DataCenter"] {
            start_pve(&target, port)?;
            let check = (|| -> Result<()> {
                let owner = saved_owner(&dir)?.context("缺少房主记录")?;
                ensure!(game_has_port(&target, owner.pid, port)?, "对局未监听好友房端口");
                start_pve(&target, port)?;
                ensure!(saved_owner(&dir)?.context("重复开房丢失原记录")?.pid == owner.pid, "重复开房改变了进程");
                let actual: serde_json::Value = serde_json::from_slice(&fs::read(dir.join("serverconfig.json"))?)?;
                ensure!(actual["map"] == expected && actual["mode"] == "pve", "地图轮换异常");
                ensure!(original_bytes == fs::read(&original)?, "原版开房设置发生变化");
                Ok(())
            })();
            let stopped = stop(&target, port)?;
            check?;
            ensure!(stopped && udp_owners(port)?.is_empty(), "停止后未释放端口");
        }
        Ok(())
    }

    #[test]
    #[ignore = "launches the installed game server on a dedicated QA port"]
    fn installed_pvp_start_reuse_and_stop() -> Result<()> {
        let target = PathBuf::from(
            std::env::var_os("BOUNDARY_QA_WIN64").context("Missing BOUNDARY_QA_WIN64")?,
        );
        let port = std::env::var("BOUNDARY_QA_PORT")?.parse()?;
        let room = room_path(&target, port)?;
        ensure!(
            !room.join(STATE).try_exists()? && udp_owners(port)?.is_empty(),
            "Test room already in use"
        );
        start(&target, port)?;
        let check = (|| -> Result<()> {
            ensure!(owned_room_listening(&target, port)?, "Open room cannot be shared or joined");
            let owner = saved_owner(&room)?.context("Missing ownership record")?;
            ensure!(
                is_owner(&owner, &target, &room) && game_has_port(&target, owner.pid, port)?,
                "Listening game is not owned by this room"
            );
            start(&target, port)?;
            ensure!(
                saved_owner(&room)?.unwrap().pid == owner.pid,
                "Second start changed the server PID"
            );
            Ok(())
        })();
        let stopped = stop(&target, port)?;
        check?;
        ensure!(!owned_room_listening(&target, port)?, "Closed room still accepts invitations");
        ensure!(stopped && !stop(&target, port)?, "Stop was not idempotent");
        ensure!(udp_owners(port)?.is_empty(), "Game still owns the UDP port");
        Ok(())
    }
}
