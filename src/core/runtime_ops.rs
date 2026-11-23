//! InstallerCore 启动、进程和端口运行时操作。

use std::fs::{self, OpenOptions};
use std::net::SocketAddrV4;
use std::path::Path;
use std::process::{Command, Stdio};
use std::sync::Mutex;
use std::thread;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, bail};
use encoding_rs::GBK;
use sysinfo::System;

use super::cleanup::clean_engine_ini;
use super::pathing::validate_win64_path;
use super::process::{
    collect_port_conflicts, format_port_conflicts, kill_pids, launch_files, path_match_key,
    path_text_match_key, runtime_image_pids, runtime_process_pids, summarize_runtime_processes,
    taskkill_images, taskkill_pids,
};
use super::util::hidden_command;
use super::*;

pub(in crate::core) mod local_pve;
pub(crate) use local_pve::{DifficultyChoice, MapChoice, pve_settings};
#[path = "runtime_ops/local_pve/lease.rs"]
mod local_pve_lease;

const RUNTIME_SHUTDOWN_IMAGES: &[&str] = &["ProjectReboundServerLauncher.exe", GAME_EXE];
static PROFILE_START: Mutex<()> = Mutex::new(());

impl InstallerCore {
    /// 按启动模式解析所需文件，并集中报告缺失路径。
    pub fn validate_launch_files(
        &self,
        target_win64: &Path,
        mode: LaunchMode,
    ) -> Result<LaunchFiles> {
        let files = launch_files(target_win64);
        let required = match mode {
            LaunchMode::Pvp | LaunchMode::Join(_) => vec![&files.server_dir, &files.node_exe, &files.game_exe],
            LaunchMode::Pve => vec![
                &files.server_dir,
                &files.node_exe,
                &files.launcher_exe,
                &files.game_exe,
            ],
        };
        let missing: Vec<String> = required
            .into_iter()
            .filter(|path| !path.exists())
            .map(|path| path.display().to_string())
            .collect();
        if !missing.is_empty() {
            bail!(
                "缺少 {} 启动所需文件：\n- {}",
                mode.display_name(),
                missing.join("\n- ")
            );
        }
        if mode.uses_login_server() {
            let login_missing: Vec<String> = [
                files.server_dir.join("index.js"),
                files.server_dir.join("package.json"),
                files.server_dir.join("node_modules").join("body-parser"),
                files.server_dir.join("node_modules").join("express"),
                files.server_dir.join("node_modules").join("protobufjs"),
            ]
            .into_iter()
            .filter(|path| !path.exists())
            .map(|path| path.display().to_string())
            .collect();
            if !login_missing.is_empty() {
                bail!(
                    "登录服务器依赖缺失，请重新安装后再启动：\n- {}",
                    login_missing.join("\n- ")
                );
            }
        }
        Ok(files)
    }

    /// 仅收集属于所选目标目录的 Boundary 运行时进程。
    ///
    /// 匹配范围刻意限定为可执行路径或命令行包含目标目录，避免误杀其它目录下
    /// 运行 index.js 的无关 node.exe 服务。
    pub fn collect_runtime_processes(&self, target_win64: &Path) -> Result<RuntimeSnapshot> {
        let files = launch_files(target_win64);
        let game_exe = path_match_key(&files.game_exe);
        let launcher_exe = path_match_key(&files.launcher_exe);
        let node_exe = path_match_key(&files.node_exe);
        let target_dir = path_match_key(target_win64);
        let game_name = GAME_EXE.to_ascii_lowercase();

        let mut system = System::new_all();
        system.refresh_all();
        let mut snapshot = RuntimeSnapshot::default();
        for process in system.processes().values() {
            let exe_lower = process
                .exe()
                .map(|path| path_text_match_key(&path.to_string_lossy()))
                .unwrap_or_default();
            let name_lower = process.name().to_string_lossy().to_lowercase();
            let cmd = process
                .cmd()
                .iter()
                .map(|part| part.to_string_lossy().into_owned())
                .collect::<Vec<_>>()
                .join(" ");
            let cmd_lower = path_text_match_key(&cmd);
            let item = RuntimeProcess {
                pid: process.pid().as_u32(),
                name: process.name().to_string_lossy().into_owned(),
                exe: process
                    .exe()
                    .map(|path| path.display().to_string())
                    .unwrap_or_default(),
                cmd,
            };
            let launched_from_target =
                exe_lower.contains(&target_dir) || cmd_lower.contains(&target_dir);

            // 优先使用精确可执行路径；只有命令行能证明进程属于当前安装时，
            // 才退回到进程名匹配。
            if exe_lower == game_exe
                || (name_lower == game_name
                    && (launched_from_target || exe_lower.is_empty() || cmd_lower.is_empty()))
            {
                snapshot.game.push(item);
            } else if exe_lower == launcher_exe
                || (name_lower == "projectreboundserverlauncher.exe" && launched_from_target)
            {
                snapshot.wrapper.push(item);
            } else if exe_lower == node_exe || (name_lower == "node.exe" && launched_from_target) {
                snapshot.server.push(item);
            }
        }
        Ok(snapshot)
    }

    /// 返回当前被占用的必要端口。
    pub fn collect_port_conflicts(&self) -> Result<Vec<PortConflict>> {
        collect_port_conflicts()
    }

    pub fn launch_port_conflicts(&self, target: &Path, mode: LaunchMode) -> Result<Vec<PortConflict>> {
        if mode == LaunchMode::Pve {
            ensure_steam_game_idle()?;
        }
        let mut conflicts = self.collect_port_conflicts()?;
        // PvE 房间端口上面的检查只报错，不交给可终止进程的通用对话框。
        conflicts.retain(|conflict| mode.binds_local_port(&conflict.protocol, conflict.port)
            && (mode != LaunchMode::Pve || conflict.port != 7777));
        if conflicts.iter().any(|conflict| conflict.port == 8000) {
            let files = launch_files(target);
            if let Some(pid) = local_profile::running_managed_pid(&self.installer_home, &files)? {
                conflicts.retain(|conflict| conflict.pid != pid);
            }
        }
        if mode == LaunchMode::Pve && !conflicts.is_empty() {
            bail!("本机 PvE 所需的档案端口已被占用，原进程保持运行：\n{}", format_port_conflicts(&conflicts));
        }
        Ok(conflicts)
    }

    /// 为 UI 端口列表构造固定顺序的诊断行，并按当前目标目录标记预期进程。
    pub fn port_status_rows_for_target(
        &self,
        target_win64: Option<&Path>,
    ) -> Result<Vec<PortStatusRow>> {
        let mut conflicts = self.collect_port_conflicts()?;
        if let Some(target) = target_win64 {
            self.mark_expected_port_conflicts(target, &mut conflicts);
        }

        let mut rows = Vec::new();
        for (protocol, port) in MONITORED_PORTS.iter().copied() {
            let mut matches = conflicts
                .iter()
                .filter(|conflict| {
                    conflict.protocol.eq_ignore_ascii_case(protocol) && conflict.port == port
                })
                .cloned()
                .collect::<Vec<_>>();
            // 同一端口有多个 PID 时全部展示；异常占用排在前面，避免被目标进程视觉上遮住。
            matches.sort_by_key(|conflict| (conflict.expected, conflict.pid));
            if matches.is_empty() {
                rows.push(PortStatusRow {
                    protocol,
                    port,
                    conflict: None,
                });
            } else {
                for conflict in matches {
                    rows.push(PortStatusRow {
                        protocol,
                        port,
                        conflict: Some(conflict),
                    });
                }
            }
        }
        Ok(rows)
    }

    /// 根据当前目标目录的运行时快照给端口冲突打标；失败时保留端口结果继续展示。
    fn mark_expected_port_conflicts(&self, target_win64: &Path, conflicts: &mut [PortConflict]) {
        let Ok(snapshot) = self.collect_runtime_processes(target_win64) else {
            return;
        };
        let expected_pids = [&snapshot.game, &snapshot.wrapper, &snapshot.server]
            .into_iter()
            .flat_map(|group| group.iter().map(|process| process.pid))
            .collect::<Vec<_>>();
        for conflict in conflicts {
            conflict.expected =
                conflict.expected || (conflict.pid > 0 && expected_pids.contains(&conflict.pid));
        }
    }

    /// 结束占用必要端口的进程。
    pub fn stop_port_conflict_processes(&self, conflicts: &[PortConflict]) -> Result<String> {
        let mut pids = Vec::new();
        let current_pid = std::process::id();
        for conflict in conflicts {
            if conflict.pid > 0 && conflict.pid != current_pid && !pids.contains(&conflict.pid) {
                pids.push(conflict.pid);
            }
        }
        if pids.is_empty() {
            return Ok("未找到可关闭的占用进程。".to_string());
        }
        kill_pids(&pids)?;
        self.log(format!(
            "端口占用清理：已请求结束 PID {}",
            pids.iter()
                .map(u32::to_string)
                .collect::<Vec<_>>()
                .join(",")
        ));
        Ok(format!(
            "已关闭以下端口占用进程：\n{}",
            format_port_conflicts(conflicts)
        ))
    }

    /// 结束游戏、服务包装器和登录服务器进程。
    pub fn stop_runtime_processes(&self, target_win64: &Path) -> Result<String> {
        let initial = self.collect_runtime_processes(target_win64)?;
        let initial_image_pids = runtime_image_pids(RUNTIME_SHUTDOWN_IMAGES);
        if runtime_process_pids(&initial).is_empty() && initial_image_pids.is_empty() {
            self.log("关闭所有进程：未检测到需要关闭的相关进程。");
            return Ok("当前没有需要关闭的相关进程。".to_string());
        }
        self.log(format!(
            "关闭所有进程：初始检测到 {}{}",
            summarize_runtime_processes(&initial),
            image_pid_summary(&initial_image_pids)
        ));

        let mut killed_pids = Vec::new();
        let mut kill_failures = Vec::new();
        let mut empty_rounds = 0;
        for _ in 0..30 {
            let current = self.collect_runtime_processes(target_win64)?;
            let mut pids = runtime_process_pids(&current);
            append_missing_pids(&mut pids, &runtime_image_pids(RUNTIME_SHUTDOWN_IMAGES));
            if pids.is_empty() {
                empty_rounds += 1;
                if empty_rounds >= 4 {
                    self.log("关闭所有进程：已连续确认全部退出。");
                    return Ok(format!(
                        "已关闭相关进程：\n{}{}",
                        summarize_runtime_processes(&initial),
                        killed_pid_summary(&killed_pids)
                    ));
                }
                thread::sleep(Duration::from_millis(500));
                continue;
            }
            empty_rounds = 0;
            for pid in &pids {
                if !killed_pids.contains(pid) {
                    killed_pids.push(*pid);
                }
            }
            let failures = taskkill_pids(&pids)?;
            kill_failures.extend(failures);
            kill_failures.extend(taskkill_images(RUNTIME_SHUTDOWN_IMAGES)?);
            thread::sleep(Duration::from_millis(500));
        }

        let latest = self.collect_runtime_processes(target_win64)?;
        let latest_image_pids = runtime_image_pids(RUNTIME_SHUTDOWN_IMAGES);
        if runtime_process_pids(&latest).is_empty() && latest_image_pids.is_empty() {
            self.log("关闭所有进程：已确认全部退出。");
            return Ok(format!(
                "已关闭相关进程：\n{}{}",
                summarize_runtime_processes(&initial),
                killed_pid_summary(&killed_pids)
            ));
        }
        let failure_text = if kill_failures.is_empty() {
            String::new()
        } else {
            format!("\ntaskkill 失败详情：{}", kill_failures.join("；"))
        };
        bail!(
            "仍有相关进程在运行，可能有外部程序正在自动拉起游戏：{}{}{}",
            summarize_runtime_processes(&latest),
            image_pid_summary(&latest_image_pids),
            failure_text
        )
    }

    /// 安装前尽力关闭进程，并在结束后再次校验。
    pub(super) fn stop_runtime_processes_before_install(&self, target_win64: &Path) -> Result<()> {
        let snapshot = self.collect_runtime_processes(target_win64)?;
        let mut pids = runtime_process_pids(&snapshot);
        append_missing_pids(&mut pids, &runtime_image_pids(RUNTIME_SHUTDOWN_IMAGES));
        if pids.is_empty() {
            self.log("安装前检查：未检测到正在运行的相关进程。");
            return Ok(());
        }

        self.log(format!(
            "安装前关闭相关进程：{}",
            summarize_runtime_processes(&snapshot)
        ));
        let mut kill_failures = taskkill_pids(&pids)?;
        kill_failures.extend(taskkill_images(RUNTIME_SHUTDOWN_IMAGES)?);

        let mut empty_rounds = 0;
        for _ in 0..20 {
            thread::sleep(Duration::from_millis(250));
            let latest = self.collect_runtime_processes(target_win64)?;
            let mut latest_pids = runtime_process_pids(&latest);
            append_missing_pids(
                &mut latest_pids,
                &runtime_image_pids(RUNTIME_SHUTDOWN_IMAGES),
            );
            if latest_pids.is_empty() {
                empty_rounds += 1;
                if empty_rounds >= 3 {
                    if !kill_failures.is_empty() {
                        self.log(format!(
                            "安装前关闭相关进程：taskkill 返回失败但目标进程已退出：{}",
                            kill_failures.join("；")
                        ));
                    }
                    self.log("安装前关闭相关进程：已连续确认全部退出。");
                    return Ok(());
                }
                continue;
            }
            empty_rounds = 0;
            kill_failures.extend(taskkill_pids(&latest_pids)?);
            kill_failures.extend(taskkill_images(RUNTIME_SHUTDOWN_IMAGES)?);
        }

        let latest = self.collect_runtime_processes(target_win64)?;
        let latest_image_pids = runtime_image_pids(RUNTIME_SHUTDOWN_IMAGES);
        let failure_text = if kill_failures.is_empty() {
            String::new()
        } else {
            format!("\ntaskkill 失败详情：{}", kill_failures.join("；"))
        };
        bail!(
            "安装前仍有相关进程未退出，请手动关闭后重试：{}{}{}",
            summarize_runtime_processes(&latest),
            image_pid_summary(&latest_image_pids),
            failure_text
        )
    }

    /// 根据用户选择启动客户端，必要时同时启动 PVE 服务端。
    pub fn launch(&self, target_win64: &Path, mode: LaunchMode) -> Result<String> {
        self.ensure_steam_ready_for_launch()?;
        match mode {
            LaunchMode::Pvp => self.launch_pvp(target_win64),
            LaunchMode::Pve => self.launch_pve(target_win64),
            LaunchMode::Join(endpoint) => self.launch_match_client(target_win64, endpoint),
        }
    }

    fn launch_login_server(&self, files: &LaunchFiles) -> Result<Option<String>> {
        let _guard = PROFILE_START.lock().map_err(|_| anyhow::anyhow!("档案服务启动锁不可用"))?;
        if local_profile::running_managed_pid(&self.installer_home, files)?.is_some() {
            return Ok(None);
        }
        let (mut cmd, notice) = match local_profile::prepare(&self.installer_home, files)? {
            local_profile::ProfilePlan::Managed(profile) => {
                self.log(format!("本机档案服务使用 {}", profile.loadout_dir.display()));
                (profile.command(&self.installer_home)?, None)
            }
            local_profile::ProfilePlan::Unsupported { reason } => {
                let mut cmd = hidden_command(&files.node_exe);
                cmd.current_dir(&files.server_dir)
                    .arg(files.server_dir.join("index.js"))
                    .env_remove("NODE_OPTIONS").env_remove("NODE_PATH");
                let notice = format!("本次使用所选安装提供的档案服务，原版军械兼容补丁尚未适配此版本。{reason}");
                self.log(&notice);
                (cmd, Some(notice))
            }
        };
        let log_path = self.installer_home.join("login-server.log");
        if let Some(parent) = log_path.parent() {
            fs::create_dir_all(parent).with_context(|| {
                format!("创建登录服务器日志目录失败：{}", parent.display())
            })?;
        }
        let stdout = OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&log_path)
            .with_context(|| format!("创建登录服务器日志失败：{}", log_path.display()))?;
        let stderr = stdout
            .try_clone()
            .with_context(|| format!("复制登录服务器日志句柄失败：{}", log_path.display()))?;
        self.log(format!(
            "启动登录服务器：{}，日志={}",
            files.node_exe.display(),
            log_path.display()
        ));
        let mut child = cmd
            .stdout(Stdio::from(stdout))
            .stderr(Stdio::from(stderr))
            .spawn()
            .context("启动登录服务器失败")?;
        let deadline = Instant::now() + Duration::from_secs(12);
        loop {
            if let Some(status) = child.try_wait().context("检查登录服务器进程状态失败")? {
                let output = read_login_server_output(&log_path);
                bail!("档案服务启动后退出：{status}。{}\n{}\n{}", login_server_exit_hint(&output), log_path.display(), output);
            }
            let text = fs::read_to_string(&log_path).unwrap_or_default();
            if text.contains("miau >:3 - 6969") && text.contains(" - 9000") {
                if notice.is_some() || local_profile::running_managed_pid(&self.installer_home, files)? == Some(child.id()) {
                    return Ok(notice);
                }
            }
            if Instant::now() >= deadline {
                let _ = child.kill();
                let _ = child.wait();
                bail!("等待本机档案端口就绪超时，请查看 {}", log_path.display());
            }
            thread::sleep(Duration::from_millis(150));
        }
    }

    /// 只启动客户端流程：登录服务器和游戏，不启动 ProjectRebound 包装器。
    fn launch_pvp(&self, target_win64: &Path) -> Result<String> {
        validate_win64_path(target_win64)?;
        let files = self.validate_launch_files(target_win64, LaunchMode::Pvp)?;
        let cleaned = clean_engine_ini(self.logger.clone())?;

        let notice = self.launch_login_server(&files)?;
        self.log(format!("启动客户端游戏：{}", files.game_exe.display()));
        game_client_command(target_win64, &files.game_exe, LaunchMode::Pvp)
            .spawn()
            .context("启动客户端游戏失败")?;

        let mut notes = vec!["客户端启动完成。".to_string()];
        notes.extend(notice);
        if let Some(path) = cleaned {
            notes.push(format!("并已清理冲突配置：{}", path.display()));
        }
        Ok(notes.join("\n"))
    }

    /// 使用目标对局的游戏端口，登录服务沿用本机地址。
    fn launch_match_client(&self, target_win64: &Path, endpoint: SocketAddrV4) -> Result<String> {
        validate_win64_path(target_win64)?;
        let files = self.validate_launch_files(target_win64, LaunchMode::Join(endpoint))?;
        let cleaned = clean_engine_ini(self.logger.clone())?;

        let notice = self.launch_login_server(&files)?;

        self.log(format!(
            "启动联机客户端：{} -> {}",
            files.game_exe.display(),
            endpoint
        ));
        game_client_command(target_win64, &files.game_exe, LaunchMode::Join(endpoint))
            .spawn()
            .context("启动联机客户端失败")?;

        let mut notes = vec![format!("联机客户端已启动，目标对局地址为 {endpoint}。")];
        notes.extend(notice);
        if let Some(path) = cleaned {
            notes.push(format!("并已清理冲突配置：{}", path.display()));
        }
        Ok(notes.join("\n"))
    }

    /// 启动登录服务器、ProjectRebound 包装器、PVE 服务端和游戏客户端。
    fn launch_pve(&self, target_win64: &Path) -> Result<String> {
        ensure_steam_game_idle()?;
        validate_win64_path(target_win64)?;
        let files = self.validate_launch_files(target_win64, LaunchMode::Pve)?;
        let notice = self.launch_login_server(&files)?;
        let core = self.clone();
        local_pve_lease::launch_pool(&self.installer_home, &files, |pipe| {
            ensure_steam_game_idle()?;
            game_client_command(target_win64, &files.game_exe, LaunchMode::Pve)
                .arg(format!("-pipe={pipe}"))
                .spawn()
                .context("启动原作大厅失败")
        }, move |reason| core.log(format!("本机 PvE 菜单租约已停止续期：{reason}")))?;
        let mut notes = vec!["原作大厅已启动。在游戏内选择人机难度后会开启本机房间。".to_owned()];
        notes.extend(notice);
        Ok(notes.join("\n"))
    }
}

fn ensure_steam_game_idle() -> Result<()> {
    use std::io::ErrorKind;
    use winreg::{RegKey, enums::HKEY_CURRENT_USER};
    let app = RegKey::predef(HKEY_CURRENT_USER)
        .open_subkey(r"Software\Valve\Steam")
        .and_then(|key| key.get_value::<u32, _>("RunningAppID"));
    match app {
        Ok(0) => Ok(()),
        Ok(id) => bail!("Steam 正在运行应用 {id}，请先退出后再启动本机人机"),
        Err(e) if e.kind() == ErrorKind::NotFound => Ok(()),
        Err(e) => Err(e).context("无法核对 Steam 当前游戏"),
    }
}

fn game_client_command(win64: &Path, game: &Path, mode: LaunchMode) -> Command {
    let mut command = Command::new(game);
    local_pve::spawn_room::clear(&mut command);
    command.current_dir(win64).arg(format!("-LogicServerURL={LOCAL_LOGIC_SERVER_URL}"));
    if matches!(mode, LaunchMode::Pve) {
        command.arg("-BrtLocalPveUi");
    }
    if let LaunchMode::Join(endpoint) = mode {
        command.arg(format!("-match={endpoint}"));
    }
    command
}

fn read_login_server_output(log_path: &Path) -> String {
    let text = fs::read(log_path)
        .map(|bytes| decode_process_output(&bytes))
        .unwrap_or_else(|error| format!("读取登录服务器日志失败：{error}"));
    let text = text.trim();
    if text.is_empty() {
        "登录服务器没有输出错误日志。".to_string()
    } else {
        trim_process_output(text)
    }
}

fn login_server_exit_hint(output: &str) -> &'static str {
    let lower = output.to_ascii_lowercase();
    if lower.contains("eaddrinuse") || output.contains("地址已在使用") {
        "原因：登录服务器需要的本地端口被占用。请在诊断页关闭占用 6969/8000/9000 的进程，或取消“仍然启动”。"
    } else if lower.contains("cannot find module") || lower.contains("module_not_found") {
        "原因：登录服务器依赖或源码缺失。请重新安装社区服文件。"
    } else if lower.contains("syntaxerror") {
        "原因：登录服务器源码解析失败。请重新安装社区服文件，确保下载未被杀毒软件拦截。"
    } else {
        "原因：Node 登录服务器进程异常退出。请把下面日志发给维护者。"
    }
}

fn decode_process_output(bytes: &[u8]) -> String {
    if bytes.is_empty() {
        return String::new();
    }
    if let Ok(text) = std::str::from_utf8(bytes) {
        return text.to_string();
    }
    let (decoded, _, _) = GBK.decode(bytes);
    decoded.into_owned()
}

fn trim_process_output(text: &str) -> String {
    const MAX_LEN: usize = 1800;
    if text.chars().count() <= MAX_LEN {
        return text.to_string();
    }
    let mut trimmed = text.chars().take(MAX_LEN).collect::<String>();
    trimmed.push_str("...");
    trimmed
}

fn killed_pid_summary(pids: &[u32]) -> String {
    if pids.is_empty() {
        String::new()
    } else {
        format!(
            "\n已请求结束 PID：{}",
            pids.iter()
                .map(u32::to_string)
                .collect::<Vec<_>>()
                .join(",")
        )
    }
}

fn image_pid_summary(pids: &[u32]) -> String {
    if pids.is_empty() {
        String::new()
    } else {
        format!(
            "；专用镜像 PID {}",
            pids.iter()
                .map(u32::to_string)
                .collect::<Vec<_>>()
                .join(",")
        )
    }
}

fn append_missing_pids(target: &mut Vec<u32>, extra: &[u32]) {
    for pid in extra {
        if *pid > 0 && !target.contains(pid) {
            target.push(*pid);
        }
    }
}
