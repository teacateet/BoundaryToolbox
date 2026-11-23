use std::fs;
use std::net::{IpAddr, Ipv4Addr};
use std::os::windows::fs::MetadataExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::LazyLock;

use anyhow::{Context, Result, ensure};
use netstat2::{AddressFamilyFlags, ProtocolFlags, ProtocolSocketInfo, TcpState, get_sockets_info};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use sysinfo::{Pid, ProcessRefreshKind, ProcessesToUpdate, System, UpdateKind};
use uuid::Uuid;

use super::{LaunchFiles, util::hidden_command};
#[path = "local_profile/room_attestation.rs"]
pub(in crate::core) mod room_attestation;
#[path = "local_profile/legacy_client.rs"]
pub(crate) mod legacy_client;

const KIT: &[(&str, &[u8])] = &[
    ("legacy-client.cjs", include_bytes!("../../integrations/loadout/legacy-client.cjs")),
    ("legacy-client.patch", include_bytes!("../../integrations/loadout/legacy-client.patch")),
    (
        "prepare.cjs",
        include_bytes!("../../integrations/profile-runtime/prepare.cjs"),
    ),
    (
        "release-084.patch",
        include_bytes!("../../integrations/profile-runtime/release-084.patch"),
    ),
    (
        "release-084-code.json.gz",
        include_bytes!("../../integrations/profile-runtime/release-084-code.json.gz"),
    ),
    (
        "release-084-LICENSE.txt",
        include_bytes!("../../integrations/profile-runtime/release-084-LICENSE.txt"),
    ),
    (
        "deploy.cjs",
        include_bytes!("../../integrations/loadout/deploy.cjs"),
    ),
    (
        "projection.cjs",
        include_bytes!("../../integrations/loadout/projection.cjs"),
    ),
    (
        "playlist.patch",
        include_bytes!("../../integrations/native-menu/playlist.patch"),
    ),
    (
        "native-menu.cjs",
        include_bytes!("../../integrations/native-menu/native-menu.cjs"),
    ),
    (
        "session-progress.patch",
        include_bytes!("../../integrations/native-menu/session-progress.patch"),
    ),
    (
        "session-progress.cjs",
        include_bytes!("../../integrations/native-menu/session-progress.cjs"),
    ),
    (
        "ready-room.patch",
        include_bytes!("../../integrations/native-menu/ready-room.patch"),
    ),
    (
        "ready-room.cjs",
        include_bytes!("../../integrations/native-menu/ready-room.cjs"),
    ),
    (
        "region-catalog.patch",
        include_bytes!("../../integrations/native-menu/region-catalog.patch"),
    ),
    (
        "region-qos.patch",
        include_bytes!("../../integrations/native-menu/region-qos.patch"),
    ),
    (
        "local-ticket.patch",
        include_bytes!("../../integrations/native-menu/local-ticket.patch"),
    ),
    (
        "native-equip-appearance.patch",
        include_bytes!("../../integrations/loadout/native-equip-appearance.patch"),
    ),
    (
        "probe-secondary.patch",
        include_bytes!("../../integrations/loadout/probe-secondary.patch"),
    ),
    (
        "retail-weapon-defaults.patch",
        include_bytes!("../../integrations/loadout/retail-weapon-defaults.patch"),
    ),
    (
        "retail-role-weapons.patch",
        include_bytes!("../../integrations/loadout/retail-role-weapons.patch"),
    ),
    (
        "room-spawn.patch",
        include_bytes!("../../integrations/loadout/room-spawn.patch"),
    ),
    (
        "room-spawn.cjs",
        include_bytes!("../../integrations/loadout/room-spawn.cjs"),
    ),
    (
        "spawn-parts.cjs",
        include_bytes!("../../integrations/loadout/spawn-parts.cjs"),
    ),
    (
        "receiver-main.json",
        include_bytes!("../../integrations/loadout/receiver-main.json"),
    ),
    (
        "role-paint-scope.cjs",
        include_bytes!("../../integrations/loadout/role-paint-scope.cjs"),
    ),
    (
        "role-paint-scopes.json",
        include_bytes!("../../integrations/loadout/role-paint-scopes.json"),
    ),
    (
        "retail-equipment-defaults.patch",
        include_bytes!("../../integrations/loadout/retail-equipment-defaults.patch"),
    ),
    (
        "role-read-case.patch",
        include_bytes!("../../integrations/match-ui/role-read-case.patch"),
    ),
    (
        "native-role-write.patch",
        include_bytes!("../../integrations/loadout/native-role-write.patch"),
    ),
    (
        "rest-role-key.patch",
        include_bytes!("../../integrations/profile-runtime/rest-role-key.patch"),
    ),
];
const NODE_FLAG: &str = "--no-global-search-paths";
const REPARSE_POINT: u32 = 0x400;
static KIT_ID: LazyLock<String> = LazyLock::new(|| {
    let mut digest = Sha256::new();
    for (name, bytes) in KIT {
        digest.update(name.as_bytes());
        digest.update([0]);
        digest.update(Sha256::digest(bytes));
    }
    format!("{:x}", digest.finalize())
});

#[derive(Debug)]
pub(crate) enum ProfilePlan {
    Managed(ManagedProfile),
    Unsupported { reason: String },
}

#[derive(Debug)]
pub(crate) struct ManagedProfile {
    pub(crate) app_dir: PathBuf,
    pub(crate) loadout_dir: PathBuf,
    node_exe: PathBuf,
    game_exe: PathBuf,
}

impl ManagedProfile {
    pub(crate) fn command(&self, host_dir: &Path) -> Result<Command> {
        let mut cmd = local_node(&self.node_exe);
        cmd.current_dir(&self.app_dir)
            .arg(self.app_dir.join("index.js"));
        cmd.env("BRT_ARCHIVE_OWNER_HELPER", std::env::current_exe()?)
            .env("BRT_ARCHIVE_OWNER_GAME", &self.game_exe);
        configure_private_rooms(&mut cmd, host_dir)?;
        if let Some(app) = self.app_dir.file_name().and_then(|v| v.to_str()).filter(|id| valid_id(id)) {
            let lease = host_dir.join("profile-runtime").join(format!("pve-ready-{app}.json"));
            if let Ok(key) = room_attestation::session_key(&lease) {
                cmd.env(room_attestation::ENV, room_attestation::hex(&key));
            }
        }
        Ok(cmd)
    }
}

// 只有房主自己登记了本机专服才向档案进程传入房间凭据。游戏和日志不接收此配置。
fn configure_private_rooms(cmd: &mut Command, host_dir: &Path) -> Result<()> {
    let path = host_dir.join("spawn-rooms.local.json");
    let meta = match fs::symlink_metadata(&path) {
        Ok(meta) => meta,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(()),
        Err(error) => return Err(error).context("无法打开本机出生服务的房间配置"),
    };
    ordinary_dir(host_dir)?;
    ensure!(
        meta.is_file() && meta.file_attributes() & REPARSE_POINT == 0 && meta.len() <= 16_384,
        "出生服务的房间配置必须是小于 16 KiB 的普通文件"
    );
    // 拒绝换名、重复密钥与非十六进制，避免把无效配置交给后端或在报错时暴露密钥。
    let rooms: std::collections::BTreeMap<String, String> =
        serde_json::from_slice(&fs::read(&path)?).map_err(|_| anyhow::anyhow!("无法读取出生服务的房间配置格式"))?;
    let mut keys = std::collections::HashSet::new();
    ensure!(!rooms.is_empty() && rooms.len() <= 64 && rooms.iter().all(|(id, key)|
        !id.is_empty() && id.len() <= 80 && id.bytes().all(|c| c.is_ascii_alphanumeric() || b"_-".contains(&c)) &&
        key.len() == 64 && key.bytes().all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c)) && keys.insert(key)
    ), "出生服务的房间名称或密钥无效");
    cmd.env("BRT_SPAWN_ROOMS", serde_json::to_string(&rooms)?);
    Ok(())
}

fn local_node(exe: &Path) -> Command {
    let mut cmd = hidden_command(exe);
    cmd.env_clear();
    for key in ["SystemRoot", "WINDIR", "SystemDrive", "TEMP", "TMP"] {
        if let Some(value) = std::env::var_os(key) {
            cmd.env(key, value);
        }
    }
    cmd.env("NO_PROXY", "*").arg(NODE_FLAG);
    cmd
}

fn ordinary_dir(path: &Path) -> Result<()> {
    let meta = fs::symlink_metadata(path)?;
    ensure!(
        meta.is_dir() && meta.file_attributes() & REPARSE_POINT == 0,
        "运行目录需为普通目录：{}",
        path.display()
    );
    Ok(())
}

fn publish_kit(root: &Path) -> Result<PathBuf> {
    let dir = root.join(format!("kit-{}", *KIT_ID));
    if !dir.try_exists()? {
        let stage = root.join(format!(".kit-{}", Uuid::new_v4()));
        fs::create_dir(&stage)?;
        let written = (|| -> Result<()> {
            for (name, bytes) in KIT {
                fs::write(stage.join(name), bytes)?;
            }
            match fs::rename(&stage, &dir) {
                Ok(()) => Ok(()),
                Err(_) if dir.is_dir() => Ok(()),
                Err(error) => Err(error.into()),
            }
        })();
        // 只回收本进程刚创建的临时文件。
        if stage.exists() {
            for (name, _) in KIT {
                let file = stage.join(name);
                if file.try_exists()? {
                    fs::remove_file(file)?;
                }
            }
            fs::remove_dir(stage)?;
        }
        written?;
    }
    ordinary_dir(&dir)?;
    for (name, bytes) in KIT {
        let file = dir.join(name);
        ensure!(
            fs::symlink_metadata(&file)?.is_file() && fs::read(&file)? == *bytes,
            "工具箱运行组件校验失败：{}",
            file.display()
        );
    }
    Ok(dir)
}

#[derive(Deserialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
enum Prepared {
    Managed {
        #[serde(rename = "appDir")]
        app_dir: PathBuf,
        #[serde(rename = "loadoutDir")]
        loadout_dir: PathBuf,
        #[serde(rename = "sourceEntrySha256")]
        source_entry_sha256: String,
        #[serde(rename = "sourceStoreSha256")]
        source_store_sha256: Option<String>,
    },
    Unsupported {
        reason: String,
    },
}

// Node 的 CommonJS 入口使用普通 Win32 路径。Rust canonicalize 返回的路径保留用于比较。
fn node_path(path: &Path) -> Result<PathBuf> {
    let text = path.to_str().context("Node 运行路径不能编码为 Unicode")?;
    if let Some(unc) = text.strip_prefix(r"\\?\UNC\") {
        return Ok(PathBuf::from(format!(r"\\{unc}")));
    }
    Ok(PathBuf::from(text.strip_prefix(r"\\?\").unwrap_or(text)))
}

#[derive(Serialize, Deserialize)]
struct Receipt {
    app: String,
    entry_sha256: String,
    store_sha256: String,
    location_sha256: String,
    menu_sha256: String,
    source_entry_sha256: String,
    source_store_sha256: Option<String>,
}

fn receipt_file(root: &Path, source: &Path, node: &Path) -> PathBuf {
    let mut key = Sha256::new();
    for text in [
        &*KIT_ID,
        &source.to_string_lossy().to_lowercase(),
        &node.to_string_lossy().to_lowercase(),
    ] {
        key.update(text.as_bytes());
        key.update([0]);
    }
    root.join(format!("verified-{:x}.json", key.finalize()))
}

fn regular_bytes(path: &Path) -> Result<Vec<u8>> {
    let meta = fs::symlink_metadata(path)?;
    ensure!(
        meta.is_file() && meta.file_attributes() & REPARSE_POINT == 0,
        "需要普通应用文件：{}",
        path.display()
    );
    Ok(fs::read(path)?)
}

fn file_sha(path: &Path) -> Result<String> {
    Ok(format!("{:x}", Sha256::digest(regular_bytes(path)?)))
}

// 准备阶段核对完整应用树；运行时重查参与菜单分配的代码、表和协议，不遍历玩家目录。
fn menu_sha(app: &Path) -> Result<String> {
    for dir in [
        "game",
        "game/definitions",
        "game/proto",
        "game/proto/Request",
        "game/proto/Response",
    ] {
        ordinary_dir(&app.join(dir))?;
    }
    let mut hash = Sha256::new();
    for name in [
        "native-menu.cjs",
        "legacy-client.cjs",
        "session-progress.cjs",
        "ready-room.cjs",
        "room-spawn.cjs",
        "spawn-parts.cjs",
        "receiver-main.json",
        "role-paint-scope.cjs",
        "role-paint-scopes.json",
        "game/definitions/DT_GameModeDefinition.json",
        "game/proto/Request/RequestWrapper.proto",
        "game/proto/Response/ResponseWrapper.proto",
        "game/proto/Request/StartMatchmakingRequest.proto",
        "game/proto/Response/StartMatchmakingResponse.proto",
        "game/proto/Response/matchmaking_ext.proto",
        "game/proto/Request/SetPartyPresenceRequest.proto",
        "game/proto/Response/SetPartyPresenceResponse.proto",
        "game/proto/Response/QueryPartyPresenceResponse.proto",
        "game/proto/Response/QueryPlaylistResponse.proto",
        "game/proto/Response/QueryMatchmakingRegionResponse.proto",
    ] {
        let bytes = regular_bytes(&app.join(name))?;
        if let Some((_, embedded)) = KIT.iter().find(|(key, _)| *key == name) {
            ensure!(bytes == *embedded, "原生菜单组件与工具箱不符：{name}");
        }
        hash.update(name.as_bytes());
        hash.update([0]);
        hash.update(Sha256::digest(bytes));
    }
    Ok(format!("{:x}", hash.finalize()))
}

fn source_store_sha(source: &Path) -> Result<Option<String>> {
    let path = source.join("game/loadoutStore.js");
    match fs::symlink_metadata(&path) {
        Ok(_) => Ok(Some(file_sha(&path)?)),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(error) => Err(error.into()),
    }
}

fn local_volume(path: &Path) -> bool {
    use std::path::{Component, Prefix};
    use windows::{Win32::Storage::FileSystem::GetDriveTypeW, core::PCWSTR};
    let Some(Component::Prefix(prefix)) = path.components().next() else {
        return false;
    };
    let (Prefix::Disk(drive) | Prefix::VerbatimDisk(drive)) = prefix.kind() else {
        return false;
    };
    let root = [u16::from(drive), 58, 92, 0];
    // GetDriveTypeW 的 2、3、6 分别表示可移动盘、本地固定盘和 RAM 盘。
    matches!(unsafe { GetDriveTypeW(PCWSTR(root.as_ptr())) }, 2 | 3 | 6)
}

fn store_receipt(
    root: &Path,
    source: &Path,
    node: &Path,
    app: &Path,
    source_entry_sha256: String,
    source_store_sha256: Option<String>,
) -> Result<()> {
    let id = app
        .file_name()
        .and_then(|n| n.to_str())
        .context("应用目录缺少版本摘要")?;
    ensure!(
        valid_id(id) && app.canonicalize()? == root.join("apps").join(id).canonicalize()?,
        "应用发布路径不匹配"
    );
    ensure!(
        file_sha(&source.join("index.js"))? == source_entry_sha256
            && source_store_sha(source)? == source_store_sha256,
        "核验期间所选安装的档案源码发生变化，请重试"
    );
    let payload = serde_json::to_vec(&Receipt {
        app: id.to_owned(),
        entry_sha256: file_sha(&app.join("index.js"))?,
        store_sha256: file_sha(&app.join("game/loadoutStore.js"))?,
        location_sha256: file_sha(&app.join("profile-location.json"))?,
        menu_sha256: menu_sha(app)?,
        source_entry_sha256,
        source_store_sha256,
    })?;
    let dest = receipt_file(root, source, node);
    if fs::read(&dest).ok().as_ref() == Some(&payload) {
        return Ok(());
    }
    let temp = root.join(format!(".verified-{}.tmp", Uuid::new_v4()));
    fs::write(&temp, payload)?;
    if let Err(error) = fs::rename(&temp, &dest) {
        let _ = fs::remove_file(temp);
        return Err(error.into());
    }
    Ok(())
}

fn valid_id(id: &str) -> bool {
    id.len() == 64
        && id
            .bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}

/// 核验所选安装，发布应用副本，记录当前工具箱和安装对应的运行凭据。
pub(crate) fn prepare(installer_home: &Path, files: &LaunchFiles) -> Result<ProfilePlan> {
    let source = files
        .server_dir
        .canonicalize()
        .with_context(|| format!("找不到所选安装的档案服务：{}", files.server_dir.display()))?;
    let node_exe = files
        .node_exe
        .canonicalize()
        .with_context(|| format!("找不到所选安装的 Node：{}", files.node_exe.display()))?;
    let home = installer_home
        .canonicalize()
        .context("工具箱运行目录不存在")?;
    let root = home.join("profile-runtime");
    let storage = resolved_dir(&source.join("data/loadouts")).context("档案目录不可解析")?;
    ensure!(
        local_volume(&storage) && local_volume(&home),
        "本机档案模式需要本地卷，不使用网络共享或映射网络盘"
    );
    ensure!(
        !root.starts_with(&source) && !source.starts_with(&root),
        "工具箱运行目录与原服务目录重叠"
    );
    fs::create_dir_all(&root)?;
    ordinary_dir(&root)?;
    let kit = publish_kit(&root).context("准备档案集成组件失败")?;
    let output = local_node(&node_path(&node_exe)?)
        .current_dir(node_path(&kit)?)
        .arg("prepare.cjs")
        .arg(node_path(&source)?)
        .arg(node_path(&root)?)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .output()
        .context("运行本地档案准备程序失败")?;
    ensure!(
        output.status.success(),
        "准备本地档案服务失败（{}）：{}",
        output.status,
        String::from_utf8_lossy(&output.stderr).trim()
    );
    Ok(
        match serde_json::from_slice(&output.stdout).context("档案准备程序返回了无效结果")?
        {
            Prepared::Managed {
                app_dir,
                loadout_dir,
                source_entry_sha256,
                source_store_sha256,
            } => {
                ensure!(
                    app_dir.is_absolute() && loadout_dir.is_absolute(),
                    "档案服务需返回绝对路径"
                );
                ensure!(
                    resolved_dir(&loadout_dir).is_some_and(|dir| same_path(&dir, &storage)),
                    "核验期间档案位置发生变化，请重试"
                );
                store_receipt(
                    &root,
                    &source,
                    &node_exe,
                    &app_dir,
                    source_entry_sha256,
                    source_store_sha256,
                )?;
                ProfilePlan::Managed(ManagedProfile {
                    app_dir,
                    loadout_dir,
                    node_exe: node_path(&node_exe)?,
                    game_exe: files.game_exe.clone(),
                })
            }
            Prepared::Unsupported { reason } => ProfilePlan::Unsupported { reason },
        },
    )
}

fn listener(port: u16) -> Result<Option<u32>> {
    let sockets = get_sockets_info(
        AddressFamilyFlags::IPV4 | AddressFamilyFlags::IPV6,
        ProtocolFlags::TCP,
    )?;
    Ok(loopback_owner(sockets.into_iter().filter_map(
        |s| match s.protocol_socket_info {
            ProtocolSocketInfo::Tcp(tcp)
                if tcp.state == TcpState::Listen && tcp.local_port == port =>
            {
                Some((tcp.local_addr, s.associated_pids))
            }
            _ => None,
        },
    )))
}

fn loopback_owner(rows: impl Iterator<Item = (IpAddr, Vec<u32>)>) -> Option<u32> {
    let mut owner = None;
    for (address, pids) in rows {
        if address.is_unspecified() {
            return None;
        }
        if address != IpAddr::V4(Ipv4Addr::LOCALHOST) {
            continue;
        }
        let [pid] = pids.as_slice() else { return None };
        if *pid == 0 || owner.replace(*pid).is_some() {
            return None;
        }
    }
    owner
}

fn resolved_dir(dir: &Path) -> Option<PathBuf> {
    match dir.canonicalize() {
        Ok(path) if path.is_dir() => Some(path),
        Err(error)
            if error.kind() == std::io::ErrorKind::NotFound && dir.symlink_metadata().is_err() =>
        {
            Some(resolved_dir(dir.parent()?)?.join(dir.file_name()?))
        }
        _ => None,
    }
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct Location {
    source: PathBuf,
    loadout_dir: PathBuf,
}

fn same_path(left: &Path, right: &Path) -> bool {
    left.as_os_str().eq_ignore_ascii_case(right.as_os_str())
}

fn owned_app(
    home: &Path,
    source: &Path,
    node: &Path,
    process: &sysinfo::Process,
) -> Option<PathBuf> {
    let node = node.canonicalize().ok()?;
    if !same_path(&process.exe()?.canonicalize().ok()?, &node) {
        return None;
    }
    let [_, option, script] = process.cmd() else {
        return None;
    };
    let script = Path::new(script);
    if option != NODE_FLAG || !script.is_absolute() {
        return None;
    }
    let source = source.canonicalize().ok()?;
    let root = home.canonicalize().ok()?.join("profile-runtime");
    if !local_volume(&root) {
        return None;
    }
    ordinary_dir(&root).ok()?;
    let proof: Receipt =
        serde_json::from_slice(&regular_bytes(&receipt_file(&root, &source, &node)).ok()?).ok()?;
    if !valid_id(&proof.app) {
        return None;
    }
    let apps = root.join("apps");
    let app = apps.join(&proof.app);
    ordinary_dir(&apps).ok()?;
    ordinary_dir(&app).ok()?;
    let entry = app.join("index.js");
    if !same_path(&script.canonicalize().ok()?, &entry)
        || !same_path(&process.cwd()?.canonicalize().ok()?, &app)
    {
        return None;
    }
    if file_sha(&entry).ok()? != proof.entry_sha256
        || file_sha(&app.join("game/loadoutStore.js")).ok()? != proof.store_sha256
        || menu_sha(&app).ok()? != proof.menu_sha256
        || file_sha(&source.join("index.js")).ok()? != proof.source_entry_sha256
        || source_store_sha(&source).ok()? != proof.source_store_sha256
    {
        return None;
    }
    let bytes = regular_bytes(&app.join("profile-location.json")).ok()?;
    if format!("{:x}", Sha256::digest(&bytes)) != proof.location_sha256 {
        return None;
    }
    let location: Location = serde_json::from_slice(&bytes).ok()?;
    let storage = resolved_dir(&source.join("data/loadouts"))?;
    if !location.source.is_absolute()
        || !location.loadout_dir.is_absolute()
        || !local_volume(&storage)
        || !same_path(&location.source.canonicalize().ok()?, &source)
        || !same_path(&resolved_dir(&location.loadout_dir)?, &storage)
    {
        return None;
    }
    Some(app)
}

fn running_app_at(
    home: &Path,
    source: &Path,
    node: &Path,
    port: u16,
) -> Result<Option<(u32, PathBuf)>> {
    let Some(owner) = listener(port)? else {
        return Ok(None);
    };
    let pid = Pid::from_u32(owner);
    let mut sys = System::new();
    sys.refresh_processes_specifics(
        ProcessesToUpdate::Some(&[pid]),
        true,
        ProcessRefreshKind::nothing()
            .with_cmd(UpdateKind::Always)
            .with_exe(UpdateKind::Always)
            .with_cwd(UpdateKind::Always),
    );
    if let Some(app) = sys
        .process(pid)
        .and_then(|p| owned_app(home, source, node, p))
        && listener(port)? == Some(owner)
    {
        return Ok(Some((owner, app)));
    }
    Ok(None)
}

pub(super) fn running_managed_pid_at(
    home: &Path,
    files: &LaunchFiles,
    port: u16,
) -> Result<Option<u32>> {
    Ok(running_app_at(home, &files.server_dir, &files.node_exe, port)?.map(|(pid, _)| pid))
}

fn running_lease_at(home: &Path, source: &Path, node: &Path, port: u16) -> Result<Option<PathBuf>> {
    let Some((_, app)) = running_app_at(home, source, node, port)? else {
        return Ok(None);
    };
    // app 已核对为该目标运行凭据的普通 apps/<SHA-256> 目录，绝不选目录中最新的一份。
    let id = app
        .file_name()
        .and_then(|n| n.to_str())
        .context("托管应用摘要不存在")?;
    let root = app
        .parent()
        .and_then(Path::parent)
        .context("托管应用目录不完整")?;
    let lease = root.join(format!("pve-ready-{id}.json"));
    match fs::symlink_metadata(&lease) {
        Ok(m) => ensure!(
            m.is_file() && m.file_attributes() & REPARSE_POINT == 0,
            "房间记录不是普通文件"
        ),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => (),
        Err(e) => return Err(e.into()),
    }
    Ok(Some(node_path(&lease)?))
}

/// 只读解析指定安装此刻实际托管进程的 lease 路径。不会准备应用、创建文件或选用其他目标。
pub(crate) fn running_pve_room_lease(home: &Path, files: &LaunchFiles) -> Result<Option<PathBuf>> {
    running_lease_at(home, &files.server_dir, &files.node_exe, 8000)
}

/// 仅返回占用 HTTP 回环端口、且通过当前发布凭据与进程身份检查的 PID。
pub(crate) fn running_managed_pid(
    installer_home: &Path,
    files: &LaunchFiles,
) -> Result<Option<u32>> {
    running_managed_pid_at(installer_home, files, 8000)
}

#[cfg(test)]
mod tests {
    use super::{local_volume, loopback_owner};
    use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

    #[test]
    fn host_room_registration_only_reaches_managed_profile() {
        let dir = std::env::temp_dir().join(format!("brt-room-{}", uuid::Uuid::new_v4()));
        std::fs::create_dir(&dir).unwrap();
        let mut command = super::local_node(std::path::Path::new("node.exe"));
        let key = std::ffi::OsStr::new("BRT_SPAWN_ROOMS");
        super::configure_private_rooms(&mut command, &dir).unwrap();
        assert!(!command.get_envs().any(|(name, _)| name == key));
        let private_file = dir.join("spawn-rooms.local.json");
        let secret = "ab".repeat(32);
        std::fs::write(&private_file, serde_json::json!({"room_a": &secret, "room_b": &secret}).to_string()).unwrap();
        let error = super::configure_private_rooms(&mut command, &dir).unwrap_err().to_string();
        assert!(!error.contains(&secret));
        std::fs::write(&private_file, serde_json::json!({"room_a": &secret}).to_string()).unwrap();
        super::configure_private_rooms(&mut command, &dir).unwrap();
        assert!(command.get_envs().any(|(name, value)| name == key && value.is_some()));
        std::fs::remove_file(&private_file).unwrap();
        std::fs::remove_dir(&dir).unwrap();
    }

    // 由 verify.cjs 对正在运行的隔离档案进程调用；不使用正式端口或档案。
    #[test]
    #[ignore = "requires a live loopback profile fixture"]
    fn live_fixture_room_path() {
        let (home, source, node, port): (
            std::path::PathBuf,
            std::path::PathBuf,
            std::path::PathBuf,
            u16,
        ) = serde_json::from_str(&std::env::var("PROFILE_LEASE_FIXTURE").unwrap()).unwrap();
        assert_ne!(port, 0);
        assert!(![6969, 8000, 9000, 9001].contains(&port));
        let lease = super::running_lease_at(&home, &source, &node, port).unwrap();
        println!("fixture-lease={}", serde_json::to_string(&lease).unwrap());
    }

    #[test]
    fn accept_local_volume_and_refuse_network_names_without_opening_them() {
        assert!(local_volume(
            &std::env::current_dir().unwrap().canonicalize().unwrap()
        ));
        for name in [
            r"\\fixture.invalid\share\data",
            r"\\?\UNC\fixture.invalid\share\data",
            "data",
        ] {
            assert!(!local_volume(std::path::Path::new(name)));
        }
    }

    #[test]
    fn accept_only_a_single_exact_loopback_owner() {
        let local = IpAddr::V4(Ipv4Addr::LOCALHOST);
        assert_eq!(loopback_owner([(local, vec![42])].into_iter()), Some(42));
        for rows in [
            vec![],
            vec![(local, vec![])],
            vec![(local, vec![0])],
            vec![(local, vec![42, 43])],
            vec![(local, vec![42]), (local, vec![43])],
            vec![(IpAddr::V6(Ipv6Addr::LOCALHOST), vec![42])],
            vec![(IpAddr::V4(Ipv4Addr::UNSPECIFIED), vec![42])],
            vec![
                (local, vec![42]),
                (IpAddr::V6(Ipv6Addr::UNSPECIFIED), vec![43]),
            ],
        ] {
            assert_eq!(loopback_owner(rows.into_iter()), None);
        }
    }
}
