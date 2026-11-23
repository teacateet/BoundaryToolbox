//! 本机快捷启动只验证本次 CLI 启动的房间，不接管既有房间。

use std::collections::BTreeMap;
use std::fs::{self, File, OpenOptions};
use std::io::{ErrorKind, Read, Seek, SeekFrom, Write};
use std::net::{IpAddr, Ipv4Addr, SocketAddrV4};
use std::os::windows::fs::OpenOptionsExt;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::thread;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, bail, ensure};
use netstat2::{AddressFamilyFlags, ProtocolFlags, ProtocolSocketInfo, get_sockets_info};
use serde::Deserialize;
use sysinfo::{Pid, ProcessRefreshKind, ProcessesToUpdate, System, UpdateKind};
use windows::Win32::Storage::FileSystem::FILE_SHARE_READ;

use super::{LaunchFiles, hidden_command, kill_pids, path_match_key};

mod map_choice;
pub(in crate::core) mod auto_room;
pub(in crate::core::runtime_ops) mod spawn_room;
pub(crate) use map_choice::{DifficultyChoice, MapChoice};

const DIFFICULTIES: [(&str, &str); 3] = [
    ("easy", "Rush_PVE_Easy"),
    ("normal", "Rush_PVE_Normal"),
    ("hard", "Rush_PVE_Hard"),
];

pub(crate) fn pve_settings(target: &Path) -> Result<(usize, String)> {
    let plan = Plan::read(&super::launch_files(target).launcher_exe)?;
    Ok((plan.difficulty_index, plan.map))
}

#[derive(Deserialize)]
struct Config {
    mode: String,
    port: u16,
    map: Option<String>,
    difficulty: Option<String>,
}

pub(super) struct Plan {
    dir: PathBuf,
    _config: File,
    managed_host: Option<File>,
    endpoint: SocketAddrV4,
    map: String,
    mode_id: String,
    difficulty_index: usize,
    mode_class: String,
}

pub(super) struct ReadyRoom {
    pub endpoint: SocketAddrV4,
    pub map: String,
    pub pid: u32,
    pub round: RoundLog,
}

impl Plan {
    fn read(launcher: &Path) -> Result<Self> {
        let dir = launcher.parent().context("PvE 启动器目录无效")?;
        Self::read_dir(dir)
    }

    pub(super) fn dir(&self) -> &Path {
        &self.dir
    }

    pub(super) fn port(&self) -> u16 {
        self.endpoint.port()
    }

    pub(in crate::core::runtime_ops) fn read_dir(dir: &Path) -> Result<Self> {
        let path = dir.join("serverconfig.json");
        // 启动器在文件无效时会自行创建默认配置。在它读完前禁止替换或改写本文件。
        let mut file = OpenOptions::new()
            .read(true)
            .share_mode(FILE_SHARE_READ.0)
            .open(&path)
            .with_context(|| format!("无法只读打开 PvE 配置：{}", path.display()))?;
        let mut data = Vec::new();
        file.read_to_end(&mut data)?;
        let config: Config =
            serde_json::from_slice(data.strip_prefix(b"\xef\xbb\xbf").unwrap_or(&data))
                .with_context(|| {
                    format!(
                        "PvE 配置缺少 mode、有效整数 port 或 JSON 无效：{}",
                        path.display()
                    )
                })?;
        ensure!(
            config.mode == "pve",
            "所选配置 mode 不是小写 pve，原房间配置未更改：{}",
            path.display()
        );
        ensure!(
            config.port != 0 && ![6969, 8000, 9000].contains(&config.port),
            "PvE 游戏端口无效或与档案端口冲突：{}",
            config.port
        );
        let map = config.map.unwrap_or_else(|| "Warehouse".into());
        ensure!(
            !map.is_empty() && !map.chars().any(char::is_whitespace),
            "PvE 地图配置无效"
        );
        let difficulty = config.difficulty.as_deref().unwrap_or("normal");
        let difficulty_index = DIFFICULTIES.iter().position(|(key, _)| *key == difficulty)
            .with_context(|| format!("无法核对 PvE 难度：{difficulty}"))?;
        let mode_id = DIFFICULTIES[difficulty_index].1.to_owned();
        let class = format!("BP_PBGameMode_{mode_id}");
        Ok(Self {
            dir: dir.to_owned(),
            _config: file,
            managed_host: None,
            map,
            mode_id,
            difficulty_index,
            endpoint: SocketAddrV4::new(Ipv4Addr::LOCALHOST, config.port),
            mode_class: format!("/Game/Online/GameMode/{class}.{class}_C"),
        })
    }

    // 文件句柄的独占共享模式覆盖准备和整场存续期，同一房间不能被两个工具箱同时启动。
    pub(super) fn use_local_archive_host(&mut self, files: &LaunchFiles) -> Result<()> {
        use std::os::windows::fs::MetadataExt;
        use windows::Win32::Storage::FileSystem::FILE_FLAG_OPEN_REPARSE_POINT;
        let target = files.game_exe.parent().context("缺少游戏目录")?.canonicalize()?;
        let expected = target.join(format!("BRT-Auto-{}-{}-{}", DIFFICULTIES[self.difficulty_index].0, self.map, self.port()));
        ensure!(self.dir.canonicalize()? == expected, "只允许在本次自动 PvE 房间使用本地档案");
        let lock = OpenOptions::new().read(true).write(true).share_mode(0)
            .create(true).truncate(false).custom_flags(FILE_FLAG_OPEN_REPARSE_POINT.0)
            .open(expected.join("brt-host.lock")).context("本机 PvE 房间已被占用，或无法取得房间锁")?;
        let info = lock.metadata()?;
        ensure!(info.is_file() && info.file_attributes() & 0x400 == 0, "无效的本机房间锁");
        self.managed_host = Some(lock);
        Ok(())
    }

    pub(super) fn launcher<'a>(&self, files: &'a LaunchFiles) -> &'a Path {
        if self.managed_host.is_some() { &files.node_exe } else { &files.launcher_exe }
    }

    fn check_available(&self, launcher: &Path) -> Result<()> {
        ensure!(
            udp_binding(self.endpoint.port(), None)? == Binding::Free,
            "本机 UDP {} 已被占用，未接管或关闭原房间",
            self.endpoint.port()
        );
        let mut system = System::new();
        system.refresh_processes_specifics(
            ProcessesToUpdate::All,
            true,
            ProcessRefreshKind::nothing(),
        );
        let name = launcher.file_name().context("PvE 启动器路径无效")?;
        let ids: Vec<_> = system
            .processes()
            .values()
            .filter(|p| p.name().eq_ignore_ascii_case(name))
            .map(|p| p.pid())
            .collect();
        if !ids.is_empty() {
            system.refresh_processes_specifics(
                ProcessesToUpdate::Some(&ids),
                true,
                ProcessRefreshKind::nothing()
                    .with_exe(UpdateKind::Always)
                    .with_cwd(UpdateKind::Always),
            );
            let exe = path_match_key(launcher);
            let cwd = path_match_key(&self.dir);
            ensure!(
                !ids.iter()
                    .filter_map(|id| system.process(*id))
                    .any(|p| p.exe().is_none_or(|v| path_match_key(v) == exe)
                        && p.cwd().is_none_or(|v| path_match_key(v) == cwd)),
                "此安装的 ServerLauncher 已运行或正在启动，原房间保持不变"
            );
        }
        Ok(())
    }

    pub(super) fn launch(
        &self,
        files: &LaunchFiles,
        lobby: impl FnOnce() -> Result<()>,
    ) -> Result<ReadyRoom> {
        self.launch_while(files, |_| (), || Ok(()), |_| Ok(()), lobby)
    }

    pub(super) fn launch_while(
        &self,
        files: &LaunchFiles,
        configure: impl FnOnce(&mut std::process::Command),
        mut check: impl FnMut() -> Result<()>,
        spawned: impl FnOnce(&std::process::Child) -> Result<()>,
        lobby: impl FnOnce() -> Result<()>,
    ) -> Result<ReadyRoom> {
        self.check_available(&files.launcher_exe)?;
        check()?;
        let logs = StartupLogs::baseline(&self.dir)?;
        let mut command = hidden_command(self.launcher(files));
        spawn_room::clear(&mut command);
        configure(&mut command);
        if self.managed_host.is_some() {
            command.env_remove("NODE_PATH").env_remove("NODE_OPTIONS")
                .args(["--no-global-search-paths", "-e", include_str!("../../../integrations/fixed-server/local-pve-host.cjs"), "--"])
                .arg(&self.map).arg(DIFFICULTIES[self.difficulty_index].0)
                .arg(self.port().to_string()).arg(crate::core::LOCAL_LOGIC_SERVER_URL)
                .stdin(Stdio::piped());
        } else {
            command.arg("-cli").stdin(Stdio::null());
        }
        let mut child = command.current_dir(&self.dir)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .context("启动所选 PvE 房间失败")?;
        let wrapper = child.id();
        let result = spawned(&child).and_then(|()| {
            if self.managed_host.is_some() {
                child.stdin.take().context("本次本机房间缺少启动管道")?.write_all(b"start\n")?;
            }
            Ok(())
        }).and_then(|()| wait_then_open(
            self,
            &logs,
            Duration::from_secs(90),
            || { check()?; Ok(child.try_wait()?.is_none()) },
            |pid| owns_room(&files.game_exe, wrapper, pid, self.endpoint.port()),
            lobby,
        ));
        let (pid, round) = match result {
            Ok(ready) => ready,
            Err(error) => {
                // 只向持有 Child 句柄的这次启动树收尾；不按镜像名、端口或日志 PID 终止进程。
                if child.try_wait()?.is_none() {
                    kill_pids(&[child.id()]).with_context(|| {
                        format!("PvE 启动失败（{error:#}），清理本次启动器失败")
                    })?;
                    child.wait()?;
                }
                bail!(
                    "本机 PvE 未完成：{error:#}。详见 {}",
                    self.dir.join("logs").display()
                );
            }
        };
        Ok(ReadyRoom {
            endpoint: self.endpoint,
            map: self.map.clone(),
            pid,
            round,
        })
    }
}

pub(super) fn preflight(files: &LaunchFiles) -> Result<Plan> {
    let plan = Plan::read(&files.launcher_exe)?;
    let data = read_map_table(files)?;
    let names = menu_maps(&data, &plan.mode_id)?;
    if !names.iter().any(|name| plan.map.eq_ignore_ascii_case(name)) {
        return Err(MapChoice::snapshot(&plan, names, &data)?.into());
    }
    plan.check_available(&files.launcher_exe)?;
    Ok(plan)
}

fn read_map_table(files: &LaunchFiles) -> Result<Vec<u8>> {
    let table = files
        .server_dir
        .join("game/definitions/DT_GameModeDefinition.json");
    fs::read(&table).with_context(|| format!("无法读取本机人机模式表：{}", table.display()))
}

fn menu_maps(data: &[u8], mode: &str) -> Result<Vec<String>> {
    let entries: Vec<serde_json::Value> =
        serde_json::from_slice(data.strip_prefix(b"\xef\xbb\xbf").unwrap_or(data))
            .context("本机人机模式表不是有效的 JSON 数组")?;
    let rows = entries
        .iter()
        .find(|v| v["Name"] == "DT_GameModeDefinition")
        .and_then(|v| v["Rows"][mode]["MapsForMatch"].as_array())
        .filter(|rows| !rows.is_empty())
        .with_context(|| format!("本机人机模式表缺少 {mode} 的 MapsForMatch"))?;
    rows
        .iter()
        .map(|v| v["RowName"].as_str().filter(|s| !s.is_empty())
            .map(|row| if row == "warehouse" { "Warehouse" } else { row }.to_owned()))
        .collect::<Option<_>>()
        .context("本机人机模式表的地图行缺少 RowName")
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Binding {
    Free,
    Ours,
    Other,
}

fn udp_binding(port: u16, expected: Option<u32>) -> Result<Binding> {
    let mut any = false;
    let mut loopback = false;
    for row in get_sockets_info(
        AddressFamilyFlags::IPV4 | AddressFamilyFlags::IPV6,
        ProtocolFlags::UDP,
    )? {
        let ProtocolSocketInfo::Udp(udp) = row.protocol_socket_info else {
            continue;
        };
        if udp.local_port != port {
            continue;
        }
        any = true;
        if row.associated_pids.is_empty()
            || !row.associated_pids.iter().all(|pid| Some(*pid) == expected)
        {
            return Ok(Binding::Other);
        }
        loopback |= matches!(udp.local_addr, IpAddr::V4(ip) if ip.is_unspecified() || ip == Ipv4Addr::LOCALHOST);
    }
    Ok(if loopback {
        Binding::Ours
    } else if any {
        Binding::Other
    } else {
        Binding::Free
    })
}

fn owns_room(game: &Path, wrapper: u32, child: u32, port: u16) -> Result<bool> {
    let pid = Pid::from_u32(child);
    let mut system = System::new();
    system.refresh_processes_specifics(
        ProcessesToUpdate::Some(&[pid]),
        true,
        ProcessRefreshKind::nothing().with_exe(UpdateKind::Always),
    );
    let owned = system.process(pid).is_some_and(|p| {
        p.parent() == Some(Pid::from_u32(wrapper))
            && p.exe()
                .is_some_and(|exe| path_match_key(exe) == path_match_key(game))
    });
    Ok(owned && udp_binding(port, Some(child))? == Binding::Ours)
}

// CLI 会将 stdout 重定向到 CONOUT$。只认本次启动后原生日志中新写入的完整行。
struct StartupLogs {
    dir: PathBuf,
    before: BTreeMap<PathBuf, u64>,
}

impl StartupLogs {
    fn sizes(dir: &Path) -> Result<BTreeMap<PathBuf, u64>> {
        let mut files = BTreeMap::new();
        let entries = match fs::read_dir(dir) {
            Ok(entries) => entries,
            Err(e) if e.kind() == ErrorKind::NotFound => return Ok(files),
            Err(e) => return Err(e.into()),
        };
        for entry in entries {
            let entry = entry?;
            if entry
                .file_name()
                .to_str()
                .is_some_and(|s| s.starts_with("log-") && s.ends_with(".txt"))
                && entry.file_type()?.is_file()
            {
                // Windows 目录项的长度可能停留在写入者打开文件之前。
                let path = entry.path();
                let len = File::open(&path)?.seek(SeekFrom::End(0))?;
                files.insert(path, len);
            }
        }
        Ok(files)
    }

    fn baseline(room: &Path) -> Result<Self> {
        let dir = room.join("logs");
        Ok(Self {
            before: Self::sizes(&dir)?,
            dir,
        })
    }

    fn ready_log(&self, plan: &Plan) -> Result<Option<(u32, RoundLog)>> {
        let mut candidate = None;
        for (path, len) in Self::sizes(&self.dir)? {
            let start = self.before.get(&path).copied().unwrap_or(0);
            if len == start {
                continue;
            }
            // 原生启动器在 1 MiB 轮转；截断或异常增长时不拿历史内容作就绪证据。
            ensure!(
                len >= start && len - start <= 2 * 1024 * 1024,
                "启动日志变化异常：{}",
                path.display()
            );
            let mut f = File::open(&path)?;
            let offset = start.saturating_sub(1);
            f.seek(SeekFrom::Start(offset))?;
            let mut bytes = Vec::new();
            (&mut f).take(len - offset).read_to_end(&mut bytes)?;
            let mut lines = bytes.split_inclusive(|&b| b == b'\n');
            if start > 0 {
                lines.next(); // 启动前最后一个字节及其所在半行不能作为新证据。
            }
            let mut state = Startup::default();
            let mut started = false;
            let mut ended = false;
            for line in lines.filter(|line| line.ends_with(b"\n")) {
                let text = String::from_utf8_lossy(line);
                state.observe(text.trim_end(), plan)?;
                if state.ready_pid().is_some() && text.trim_end() == RoundLog::START {
                    started = true;
                }
                ended |= RoundLog::is_match_end(text.trim_end());
            }
            if let Some(pid) = state.ready_pid() {
                ensure!(!ended, "本次 PvE 房间在交接前已结束");
                ensure!(
                    candidate.as_ref().is_none_or(|(old, _)| *old == pid),
                    "多个不同对局同时写入了就绪日志"
                );
                let next = offset + bytes.iter().rposition(|&b| b == b'\n').unwrap() as u64 + 1;
                let round = RoundLog { file: f, next, started, closed: false };
                candidate = Some((pid, round));
            }
        }
        Ok(candidate)
    }
}

// 持有本次启动时核验过的文件句柄；轮转、退出或换图后不再从该文件确认开赛。
#[derive(Debug)]
pub(super) struct RoundLog {
    file: File,
    next: u64,
    started: bool,
    closed: bool,
}

impl RoundLog {
    const START: &'static str = "[SERVER_LIFECYCLE] match_started";
    fn is_match_end(line: &str) -> bool {
        line == "[SERVER_LIFECYCLE] match_end_detected"
            || line.starts_with("[SERVER_LIFECYCLE] match_end_detected reason=")
    }

    pub(super) fn started(&mut self) -> bool {
        if self.closed {
            return false;
        }
        let read = (|| -> std::io::Result<()> {
            let end = self.file.seek(SeekFrom::End(0))?;
            if end == self.next {
                return Ok(());
            }
            if end < self.next || end - self.next > 128 * 1024 {
                self.closed = true;
                return Ok(());
            }
            self.file.seek(SeekFrom::Start(self.next))?;
            let mut bytes = Vec::new();
            (&mut self.file).take(end - self.next).read_to_end(&mut bytes)?;
            for row in bytes.split_inclusive(|&b| b == b'\n').filter(|s| s.ends_with(b"\n")) {
                self.next += row.len() as u64;
                let line = String::from_utf8_lossy(row);
                let line = line.trim_end();
                if line.starts_with("[Launcher] Server launched.")
                    || line.starts_with("[Launcher] Server exited")
                    || line.starts_with("[SERVER] Map loaded:")
                    || Self::is_match_end(line)
                    || line == "[Launcher] Room event limit reached."
                    || line == "[Launcher] PipeReader thread ended."
                {
                    self.closed = true;
                    break;
                }
                if line == Self::START {
                    self.started = true;
                }
            }
            Ok(())
        })();
        read.is_ok() && self.started && !self.closed
    }
}

#[derive(Default)]
struct Startup {
    pid: Option<u32>,
    map: bool,
    mode: bool,
    port: bool,
    listening: bool,
}

impl Startup {
    fn ready_pid(&self) -> Option<u32> {
        self.pid
            .filter(|_| self.map && self.mode && self.port && self.listening)
    }

    fn observe(&mut self, line: &str, plan: &Plan) -> Result<()> {
        if let Some(s) = line.strip_prefix("[Launcher] Server launched. PID = ") {
            let pid = s.parse::<u32>().context("PvE 游戏 PID 无效")?;
            ensure!(
                pid != 0 && self.pid.is_none_or(|old| old == pid),
                "启动期间对局进程已替换"
            );
            self.pid = Some(pid);
        } else if let Some(s) = line.strip_prefix("[SERVER] Map loaded: ") {
            ensure!(s == plan.map, "实际地图 {s} 与配置 {} 不符", plan.map);
            self.map = true;
        } else if let Some(s) = line.strip_prefix("[SERVER] Mode: ") {
            ensure!(s == plan.mode_class, "实际模式 {s} 与所选 PvE 难度不符");
            self.mode = true;
        } else if let Some(s) = line.strip_prefix("[SERVER] Port: ") {
            ensure!(
                s.parse::<u16>().ok() == Some(plan.endpoint.port()),
                "实际游戏端口 {s} 与配置 {} 不符",
                plan.endpoint.port()
            );
            self.port = true;
        } else if line == "[SERVER] Server is now listening." {
            self.listening = true;
        } else if [
            "[Launcher] ERROR:",
            "[Launcher] CRITICAL:",
            "[Launcher] Failed to launch server",
            "[Launcher] Server exited",
            "[Launcher] Room event limit reached.",
        ]
        .iter()
        .any(|prefix| line.starts_with(prefix))
        {
            bail!("PvE 启动器报告失败：{line}");
        }
        Ok(())
    }
}

fn wait_then_open(
    plan: &Plan,
    logs: &StartupLogs,
    timeout: Duration,
    mut alive: impl FnMut() -> Result<bool>,
    mut owns: impl FnMut(u32) -> Result<bool>,
    lobby: impl FnOnce() -> Result<()>,
) -> Result<(u32, RoundLog)> {
    let deadline = Instant::now() + timeout;
    loop {
        ensure!(alive()?, "本次 PvE 启动器已退出");
        let remaining = deadline.saturating_duration_since(Instant::now());
        ensure!(
            !remaining.is_zero(),
            "等待本次 PvE 游戏监听 UDP {} 超时",
            plan.endpoint.port()
        );
        if let Some((pid, round)) = logs.ready_log(plan)?
            && owns(pid)?
            && alive()?
            && Instant::now() < deadline
        {
            lobby()?;
            return Ok((pid, round));
        }
        thread::sleep(remaining.min(Duration::from_millis(250)));
    }
}

#[cfg(test)]
mod tests;
