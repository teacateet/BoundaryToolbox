//! 原生菜单的本机房间租约。只在经过托管服务核验的应用旁写入，不写游戏配置。

use std::fs::{self, File, OpenOptions};
use std::io::{ErrorKind, Read, Write};
use std::net::{IpAddr, Ipv4Addr};
use std::os::windows::fs::{MetadataExt, OpenOptionsExt};
use std::os::windows::io::{AsRawHandle, FromRawHandle, OwnedHandle};
use std::path::{Path, PathBuf};
use std::process::Child;
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use anyhow::{Context, Result, bail, ensure};
use netstat2::{AddressFamilyFlags, ProtocolFlags, ProtocolSocketInfo, get_sockets_info};
use serde::Deserialize;
use serde_json::json;
use sysinfo::{Pid, ProcessRefreshKind, ProcessesToUpdate, System, UpdateKind};
use uuid::Uuid;
use windows::Win32::Foundation::{HANDLE, WAIT_FAILED, WAIT_OBJECT_0, WAIT_TIMEOUT};
use windows::Win32::Storage::FileSystem::{
    FILE_ATTRIBUTE_REPARSE_POINT, FILE_FLAG_BACKUP_SEMANTICS, FILE_FLAG_DELETE_ON_CLOSE,
    FILE_FLAG_OPEN_REPARSE_POINT, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, FILE_SHARE_WRITE,
};
use windows::Win32::System::Threading::{OpenProcess, PROCESS_SYNCHRONIZE, WaitForMultipleObjects};

use super::{LaunchFiles, kill_pids, local_profile, local_pve, path_match_key};

#[path = "auto_pool.rs"]
mod auto_pool;
#[path = "client_join.rs"]
mod client_join;
pub(super) use auto_pool::launch_pool;

const RENEW: Duration = Duration::from_secs(4);
const TTL_MS: u64 = 12_000; // 菜单 reader 上限为 15 秒。

struct ProcessPin {
    id: u32,
    handle: OwnedHandle,
}

impl ProcessPin {
    fn open(id: u32) -> Result<Self> {
        // 只申请等待权限。持有内核句柄期间不用按可复用的 PID 认领后来者。
        let handle = unsafe { OpenProcess(PROCESS_SYNCHRONIZE, false, id)? };
        Ok(Self {
            id,
            handle: unsafe { OwnedHandle::from_raw_handle(handle.0) },
        })
    }

    fn raw(&self) -> HANDLE {
        HANDLE(self.handle.as_raw_handle())
    }
}

fn require_running<const N: usize>(pins: [(&str, HANDLE); N], timeout: Duration) -> Result<()> {
    let handles = pins.map(|(_, handle)| handle);
    let result = unsafe { WaitForMultipleObjects(&handles, false, timeout.as_millis() as u32) };
    if result == WAIT_TIMEOUT {
        return Ok(());
    }
    if result == WAIT_FAILED {
        return Err(std::io::Error::last_os_error()).context("Windows 等待本机进程失败");
    }
    if let Some((role, _)) = result
        .0
        .checked_sub(WAIT_OBJECT_0.0)
        .and_then(|index| pins.get(index as usize))
    {
        bail!("{role}已退出");
    }
    bail!("Windows 等待本机进程返回异常状态 {:#x}", result.0)
}

fn snapshot(id: u32) -> System {
    let mut sys = System::new();
    sys.refresh_processes_specifics(
        ProcessesToUpdate::Some(&[Pid::from_u32(id)]),
        true,
        ProcessRefreshKind::nothing()
            .with_exe(UpdateKind::Always)
            .with_cwd(UpdateKind::Always),
    );
    sys
}

struct Deployment {
    process: ProcessPin,
    lease: PathBuf,
}

impl Deployment {
    fn verified(home: &Path, files: &LaunchFiles) -> Result<Self> {
        let current = || local_profile::running_managed_pid(home, files);
        let id = current()?.context("本机人机菜单需要所选安装的已验证托管档案服务，未发布房间")?;
        let process = ProcessPin::open(id).context("打开托管档案服务进程失败")?;
        let lease = local_profile::running_pve_room_lease(home, files)?
            .context("无法确定已验证托管应用的房间租约路径")?;
        require_running([("托管档案服务", process.raw())], Duration::ZERO)?;
        ensure!(current()? == Some(id), "档案服务在核验应用目录期间已改变");
        Ok(Self { process, lease })
    }
}

#[derive(Clone, Deserialize, PartialEq, Eq)]
struct Selection {
    mode: String,
    offline: bool,
    port: u16,
    map: Option<String>,
    difficulty: Option<String>,
}

impl Selection {
    fn read(files: &LaunchFiles) -> Result<Self> {
        let path = files.launcher_exe.with_file_name("serverconfig.json");
        Self::read_from(&path)
    }

    fn read_from(path: &Path) -> Result<Self> {
        let mut bytes = Vec::new();
        OpenOptions::new()
            .read(true)
            .share_mode(FILE_SHARE_READ.0)
            .open(&path)?
            .take(1024 * 1024 + 1)
            .read_to_end(&mut bytes)?;
        ensure!(bytes.len() <= 1024 * 1024, "房间配置过大");
        let value: Self =
            serde_json::from_slice(bytes.strip_prefix(b"\xef\xbb\xbf").unwrap_or(&bytes))
                .with_context(|| format!("无法核对本机人机配置：{}", path.display()))?;
        ensure!(
            value.mode == "pve" && value.offline,
            "菜单只发布配置明确为 pve、offline=true 的本机房间"
        );
        ensure!(
            value.port >= 1024 && ![6969, 8000, 9000, 9001].contains(&value.port),
            "不是原生菜单可用的本机游戏端口"
        );
        Ok(value)
    }

    fn map(&self) -> &str {
        self.map.as_deref().unwrap_or("Warehouse")
    }
    fn difficulty(&self) -> &str {
        self.difficulty.as_deref().unwrap_or("normal")
    }
    fn class(&self) -> Result<String> {
        let diff = match self.difficulty() {
            "easy" => "Easy",
            "normal" => "Normal",
            "hard" => "Hard",
            _ => bail!("不支持的本机 PvE 难度"),
        };
        let bp = format!("BP_PBGameMode_Rush_PVE_{diff}");
        Ok(format!("/Game/Online/GameMode/{bp}.{bp}_C"))
    }
}

struct Room {
    wrapper: ProcessPin,
    game: ProcessPin,
}

impl Room {
    fn pin(files: &LaunchFiles, id: u32) -> Result<Self> {
        Self::pin_at(
            files,
            id,
            files.launcher_exe.parent().context("启动器缺少父目录")?,
        )
    }

    fn pin_at(files: &LaunchFiles, id: u32, dir: &Path) -> Result<Self> {
        Self::pin_launcher(files, id, dir, &files.launcher_exe)
    }

    fn pin_launcher(files: &LaunchFiles, id: u32, dir: &Path, launcher: &Path) -> Result<Self> {
        let game = ProcessPin::open(id).context("打开本机对局进程失败")?;
        let sys = snapshot(id);
        let p = sys.process(Pid::from_u32(id)).context("就绪对局已退出")?;
        ensure!(
            p.exe()
                .is_some_and(|exe| path_match_key(exe) == path_match_key(&files.game_exe)),
            "就绪对局不属于所选安装"
        );
        let parent = p.parent().context("就绪对局没有启动器")?.as_u32();
        let wrapper = ProcessPin::open(parent).context("打开本机对局启动器进程失败")?;
        let sys = snapshot(parent);
        let p = sys
            .process(Pid::from_u32(parent))
            .context("就绪启动器已退出")?;
        ensure!(
            p.parent() == Some(Pid::from_u32(std::process::id()))
                && p.exe()
                    .is_some_and(|exe| path_match_key(exe) == path_match_key(launcher))
                && p.cwd()
                    .is_some_and(|cwd| path_match_key(cwd) == path_match_key(dir)),
            "就绪进程不属于本次本机房间"
        );
        require_running(
            [("本机对局启动器", wrapper.raw()), ("本机对局", game.raw())],
            Duration::ZERO,
        )?;
        Ok(Self { wrapper, game })
    }

    fn socket_owner(port: u16) -> Result<Option<u32>> {
        let mut reachable = false;
        let mut owner = None;
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
            if row.associated_pids.is_empty() {
                return Ok(None);
            }
            for pid in row.associated_pids {
                if pid == 0 || owner.replace(pid).is_some_and(|old| old != pid) {
                    return Ok(None);
                }
            }
            reachable |= matches!(udp.local_addr, IpAddr::V4(ip) if ip.is_unspecified() || ip == Ipv4Addr::LOCALHOST);
        }
        Ok(owner.filter(|_| reachable))
    }
}

fn ordinary(meta: &fs::Metadata) -> bool {
    meta.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT.0 == 0
}

struct Slot {
    file: PathBuf,
    _root: File,
    _lock: File,
    run: String,
    expected: Option<Vec<u8>>,
    published: bool,
}

impl Slot {
    fn claim(home: &Path, lease: &Path) -> Result<Self> {
        let root = home.canonicalize()?.join("profile-runtime");
        let name = lease
            .file_name()
            .and_then(|s| s.to_str())
            .context("托管应用租约缺少文件名")?;
        let id = name
            .strip_prefix("pve-ready-")
            .and_then(|s| s.strip_suffix(".json"))
            .context("托管租约名称无效")?;
        ensure!(
            id.len() == 64
                && id
                    .bytes()
                    .all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))
                && lease.is_absolute()
                && lease
                    .parent()
                    .context("租约没有父目录")?
                    .canonicalize()?
                    .as_os_str()
                    .eq_ignore_ascii_case(root.as_os_str()),
            "不是本工具箱核验的托管租约路径"
        );
        let pin = OpenOptions::new()
            .access_mode(FILE_READ_ATTRIBUTES.0)
            .share_mode((FILE_SHARE_READ | FILE_SHARE_WRITE).0)
            .custom_flags((FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT).0)
            .open(&root)?;
        ensure!(ordinary(&pin.metadata()?), "托管根目录已替换");
        let file = root.join(name);
        let lock = OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .share_mode(0)
            .custom_flags((FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DELETE_ON_CLOSE).0)
            .open(file.with_extension("lock"))
            .context("该托管应用已由其他房间占用或不可写")?;
        ensure!(
            lock.metadata()?.is_file() && ordinary(&lock.metadata()?),
            "租约锁类型无效"
        );
        let mut slot = Self {
            file,
            _root: pin,
            _lock: lock,
            run: Uuid::new_v4().to_string(),
            expected: None,
            published: false,
        };
        slot.expected = slot.contents()?;
        if let Some(old) = &slot.expected {
            let old: serde_json::Value = serde_json::from_slice(old)?;
            let now = now_ms()?;
            ensure!(
                old.is_null()
                    || (old["v"] == 1 && old["expiresAt"].as_u64().is_some_and(|t| t <= now)),
                "该托管应用还有未过期的房间回执，请稍后重试"
            );
        }
        Ok(slot)
    }

    fn contents(&self) -> Result<Option<Vec<u8>>> {
        let f = match OpenOptions::new()
            .read(true)
            .custom_flags(FILE_FLAG_OPEN_REPARSE_POINT.0)
            .open(&self.file)
        {
            Ok(f) => f,
            Err(e) if e.kind() == ErrorKind::NotFound => return Ok(None),
            Err(e) => return Err(e.into()),
        };
        let meta = f.metadata()?;
        ensure!(
            meta.is_file() && ordinary(&meta) && meta.len() <= 4096,
            "租约不是普通的小型文件"
        );
        let mut bytes = Vec::new();
        f.take(4097).read_to_end(&mut bytes)?;
        ensure!(bytes.len() <= 4096, "租约文件过大");
        Ok(Some(bytes))
    }

    fn replace(&mut self, bytes: Vec<u8>) -> Result<()> {
        ensure!(
            self.contents()? == self.expected,
            "租约已换代，旧房间不再写入"
        );
        let tmp = self.file.with_extension(format!("{}.tmp", Uuid::new_v4()));
        let mut out = File::create_new(&tmp)?;
        let result = (|| -> Result<()> {
            out.write_all(&bytes)?;
            drop(out);
            fs::rename(&tmp, &self.file)?;
            Ok(())
        })();
        if result.is_err() {
            let _ = fs::remove_file(tmp);
        }
        result?;
        self.expected = Some(bytes);
        self.published = true;
        Ok(())
    }

    fn publish(&mut self, cfg: &Selection) -> Result<()> {
        let value = json!({
            "v": 1, "runId": self.run, "expiresAt": now_ms()? + TTL_MS, "mode": "pve",
            "offline": true, "difficulty": cfg.difficulty(), "map": cfg.map(), "gameClass": cfg.class()?, "port": cfg.port,
        });
        self.replace(serde_json::to_vec(&value)?)
    }
}

impl Slot {
    fn withdraw(&mut self) {
        // 独占锁仍在手；若文件被另一个所有者替换，不撤销后来者。
        if self.published && self.contents().ok() == Some(self.expected.clone()) {
            let _ = fs::remove_file(&self.file);
        }
    }
}

impl Drop for Slot {
    fn drop(&mut self) {
        self.withdraw();
    }
}

fn now_ms() -> Result<u64> {
    Ok(SystemTime::now()
        .duration_since(UNIX_EPOCH)?
        .as_millis()
        .try_into()?)
}

struct Publisher {
    slot: Slot,
    profile: Deployment,
    room: Room,
    selected: Selection,
    files: LaunchFiles,
}

const JOIN_LOG_LIMIT: u64 = 16 * 1024;

struct JoinLog {
    path: PathBuf,
    next_failure: Instant,
}

impl JoinLog {
    fn record(&mut self, stage: &'static str) {
        if stage.ends_with("-failed") {
            if Instant::now() < self.next_failure {
                return;
            }
            self.next_failure = Instant::now() + Duration::from_secs(5);
        }
        let line = format!(
            "{} {stage}\n",
            chrono::Local::now().format("%Y-%m-%d %H:%M:%S")
        );
        let _ = (|| -> std::io::Result<()> {
            let mut out = OpenOptions::new()
                .write(true)
                .append(true)
                .create(true)
                .share_mode(FILE_SHARE_READ.0)
                .custom_flags(FILE_FLAG_OPEN_REPARSE_POINT.0)
                .open(&self.path)?;
            let meta = out.metadata()?;
            if !meta.is_file() || !ordinary(&meta) {
                return Ok(());
            }
            if meta.len() + line.len() as u64 > JOIN_LOG_LIMIT {
                out.set_len(0)?;
            }
            out.write_all(line.as_bytes())
        })();
    }
}

impl Publisher {
    fn pins(&self) -> [(&'static str, HANDLE); 3] {
        [
            ("托管档案服务", self.profile.process.raw()),
            ("本机对局启动器", self.room.wrapper.raw()),
            ("本机对局", self.room.game.raw()),
        ]
    }

    fn refresh(&mut self, verified: Option<u32>) -> Result<()> {
        ensure!(verified == Some(self.profile.process.id), "档案部署已切换");
        ensure!(
            self.selected == Selection::read(&self.files)?,
            "房间配置已改变"
        );
        require_running(self.pins(), Duration::ZERO)?;
        ensure!(
            Room::socket_owner(self.selected.port)? == Some(self.room.game.id),
            "本机对局 UDP 归属已改变"
        );
        self.slot.publish(&self.selected)
    }

    fn open(
        mut self,
        verify: impl Fn() -> Result<Option<u32>> + Send + 'static,
        lobby: impl FnOnce() -> Result<()>,
        stopped: impl FnOnce(String) + Send + 'static,
    ) -> Result<()> {
        self.refresh(verify()?)?;
        let (go, gate) = mpsc::channel();
        let worker = thread::Builder::new()
            .name("pve-menu-lease".into())
            .spawn(move || {
                if gate.recv().is_err() {
                    return;
                }
                let reason = loop {
                    if let Err(error) = require_running(self.pins(), RENEW) {
                        break format!("{error:#}");
                    }
                    if let Err(e) = verify().and_then(|id| self.refresh(id)) {
                        break format!("{e:#}");
                    }
                };
                drop(self);
                stopped(reason);
            })?;
        if let Err(error) = lobby() {
            drop(go);
            let _ = worker.join();
            return Err(error);
        }
        go.send(())?;
        Ok(())
    }
}

pub(super) fn launch(
    home: &Path,
    files: &LaunchFiles,
    plan: &local_pve::Plan,
    lobby: impl FnOnce() -> Result<()>,
    stopped: impl FnOnce(String) + Send + 'static,
) -> Result<local_pve::ReadyRoom> {
    let profile = Deployment::verified(home, files)?;
    let owner_home = home.to_owned();
    let owner_files = files.clone();
    launch_verified(
        home,
        files,
        plan,
        profile,
        move || local_profile::running_managed_pid(&owner_home, &owner_files),
        lobby,
        stopped,
    )
}

fn launch_verified(
    home: &Path,
    files: &LaunchFiles,
    plan: &local_pve::Plan,
    profile: Deployment,
    verify: impl Fn() -> Result<Option<u32>> + Send + 'static,
    lobby: impl FnOnce() -> Result<()>,
    stopped: impl FnOnce(String) + Send + 'static,
) -> Result<local_pve::ReadyRoom> {
    let selected = Selection::read(files)?;
    let slot = Slot::claim(home, &profile.lease)?;
    // 在原就绪检查仍持有本次 Child 并负责失败清理时固定句柄，再与其日志 PID 对照。
    let mut pinned = None;
    let ready = plan.launch(files, || {
        let id = Room::socket_owner(selected.port)?.context("已就绪游戏端口的归属发生改变")?;
        let room = Room::pin(files, id)?;
        let cleanup = ProcessPin::open(room.wrapper.id).context("打开本次对局启动器进程失败")?;
        pinned = Some((room, cleanup));
        Ok(())
    })?;
    let (room, wrapper) = pinned.context("本次就绪回调没有固定房间进程")?;
    ensure!(
        room.game.id == ready.pid
            && ready.endpoint.port() == selected.port
            && ready.map == selected.map(),
        "本次就绪日志、游戏进程和所选配置不符，未发布或接管房间"
    );
    let publisher = Publisher {
        slot,
        profile,
        room,
        selected,
        files: files.clone(),
    };
    if let Err(error) = publisher.open(verify, lobby, stopped) {
        if require_running([("本机对局启动器", wrapper.raw())], Duration::ZERO).is_ok() {
            kill_pids(&[wrapper.id])
                .with_context(|| format!("发布或大厅启动失败（{error:#}），清理本次启动器失败"))?;
        }
        return Err(error);
    }
    Ok(ready)
}

#[cfg(test)]
#[path = "lease_tests.rs"]
mod tests;
