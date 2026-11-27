//! AppController 安装、卸载、启动和诊断动作。

use super::*;
use std::io::{Read, Seek, SeekFrom};
use std::os::windows::fs::{MetadataExt, OpenOptionsExt};
use std::sync::atomic::AtomicU64;

static LAUNCH_EPOCH: AtomicU64 = AtomicU64::new(0);

// 仅观察本轮实际托管的本机租约和两个已有日志，不发查询，不参与匹配。
fn ordinary_match_file(path: &Path) -> Option<File> {
    let f = OpenOptions::new()
        .read(true)
        .custom_flags(0x0020_0000)
        .open(path)
        .ok()?;
    let m = f.metadata().ok()?;
    (m.is_file() && m.file_attributes() & 0x400 == 0).then_some(f)
}

fn live_pool(path: &Path, now: i64) -> Option<(String, Option<uuid::Uuid>)> {
    let mut data = Vec::new();
    ordinary_match_file(path)?
        .take(4097)
        .read_to_end(&mut data)
        .ok()?;
    if data.len() > 4096 {
        return None;
    }
    let v: serde_json::Value = serde_json::from_slice(&data).ok()?;
    let ttl = v["expiresAt"].as_i64()?.checked_sub(now)?;
    (v["v"] == 1
        && v["autoPool"] == true
        && v["offline"] == true
        && v["mode"] == "pve"
        && (1..=15000).contains(&ttl))
    .then(|| {
        v["runId"]
            .as_str()
            .and_then(|s| uuid::Uuid::parse_str(s).ok())
    })
    .flatten()
    .map(|id| {
        let started_choice = v["startedChoice"].as_str().and_then(|s| uuid::Uuid::parse_str(s).ok());
        (id.to_string(), started_choice)
    })
}

#[derive(Default)]
struct MatchTail {
    pos: u64,
    last: Vec<u8>,
}
impl MatchTail {
    fn poll(&mut self, path: &Path, mut line: impl FnMut(&str)) {
        let Some(mut f) = ordinary_match_file(path) else {
            return;
        };
        let Ok(meta) = f.metadata() else { return };
        let end = meta.len();
        let mut check = vec![0; self.last.len()];
        if end < self.pos
            || f.seek(SeekFrom::Start(self.pos - self.last.len() as u64))
                .is_err()
            || f.read_exact(&mut check).is_err()
            || check != self.last
        {
            self.pos = 0;
            self.last.clear();
        }
        let start = self.pos.max(end.saturating_sub(64 * 1024));
        if start == end || f.seek(SeekFrom::Start(start)).is_err() {
            return;
        }
        let mut data = Vec::new();
        if f.take(end - start).read_to_end(&mut data).is_err() {
            return;
        }
        let Some(n) = data.iter().rposition(|&b| b == b'\n').map(|n| n + 1) else {
            return;
        };
        for row in String::from_utf8_lossy(&data[..n])
            .lines()
            .skip(usize::from(start != self.pos))
        {
            line(row);
        }
        self.pos = start + n as u64;
        self.last = data[n.saturating_sub(64)..n].to_vec();
    }
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum MatchStep {
    Prepare,
    Ticket,
    Ack,
    RoundStarted,
    LoadoutWaiting,
    LoadoutSent,
    Failed,
    Closed,
    Cancel,
    Hall,
    ProgressFailed,
    TicketFailed,
    End,
}
impl MatchStep {
    fn label(self, lang: i32) -> &'static str {
        use MatchStep::*;
        let (zh, en, ja) = match self {
            Prepare => (
                "本机：房间准备中",
                "Local: preparing room",
                "ローカル：ルーム準備中",
            ),
            Ticket => (
                "本机：客户端已接票，等待交接",
                "Local: client has ticket; awaiting handoff",
                "ローカル：チケット受領、引き渡し待ち",
            ),
            Ack => (
                "本机：已交接，进房未核验",
                "Local: handed off; entry unverified",
                "ローカル：引き渡し済み、入室未確認",
            ),
            RoundStarted => (
                "本机：本场房间已开赛",
                "Local: the match has started",
                "ローカル：このルームの対戦が開始されました",
            ),
            LoadoutWaiting => (
                "本场已开赛，档案尚未发出出生配装",
                "Match started; spawn loadout not sent yet",
                "対戦開始。出撃装備はまだ送信されていません",
            ),
            LoadoutSent => (
                "本场已开赛，档案已发出出生配装",
                "Match started; spawn loadout sent",
                "対戦開始。出撃装備を送信しました",
            ),
            Failed => (
                "本机：交接未确认",
                "Local: handoff unconfirmed",
                "ローカル：引き渡し未確認",
            ),
            Closed => (
                "本机：房间准备或交接已结束",
                "Local: room preparation or handoff ended",
                "ローカル：準備または引き渡し終了",
            ),
            Cancel => (
                "本机：本次匹配已取消",
                "Local: match cancelled",
                "ローカル：マッチをキャンセル",
            ),
            Hall => (
                "本机：已返回大厅",
                "Local: back in lobby",
                "ローカル：ロビーに復帰",
            ),
            ProgressFailed => (
                "本机：建票进度写入失败",
                "Local: ticket progress write failed",
                "ローカル：チケット進捗の送信失敗",
            ),
            TicketFailed => (
                "本机：票据写入失败",
                "Local: ticket write failed",
                "ローカル：チケット送信失敗",
            ),
            End => (
                "本机：匹配状态跟踪已结束",
                "Local: match tracking ended",
                "ローカル：状態追跡終了",
            ),
        };
        i18n::tr(lang, zh, en, ja)
    }
}

fn menu_step(line: &str, run: &str) -> Option<(i64, MatchStep, Option<uuid::Uuid>)> {
    let v: serde_json::Value = serde_json::from_str(line.strip_prefix("[PVE-ROOM] ")?).ok()?;
    if v["run"] != run || v["event"] != "local-feedback" {
        return None;
    }
    Some((
        v["at"].as_i64()?,
        match v["stage"].as_str()? {
            "preparing" => MatchStep::Prepare,
            "ticket-received" => MatchStep::Ticket,
            "write-failed" => MatchStep::ProgressFailed,
            "cancelled" => MatchStep::Cancel,
            "returned" => MatchStep::Hall,
            "ticket-write-failed" => MatchStep::TicketFailed,
            "prepare-ended" => MatchStep::Closed,
            _ => return None,
        },
        v["requestId"].as_str().and_then(|s| uuid::Uuid::parse_str(s).ok()),
    ))
}

fn show_match_step(w: &AppWindow, epoch: u64, target: &str, stage: MatchStep) {
    if LAUNCH_EPOCH.load(Ordering::Relaxed) == epoch
        && w.get_target_text().as_str() == target
        && !w.get_busy()
        && w.get_launch_pve_server()
    {
        w.set_status_text(stage.label(w.get_language()).into());
    }
}

fn spawn_event(line: &str, run: &str) -> Option<(uuid::Uuid, bool)> {
    let row: serde_json::Value = serde_json::from_str(line.strip_prefix("[PVE-SPAWN] ")?).ok()?;
    if row["run"] != run { return None; }
    let sent = match row["event"].as_str()? {
        "room-registered" => false,
        "loadout-returned" if row["life"].as_u64()? > 0 => true,
        _ => return None,
    };
    Some((uuid::Uuid::parse_str(row["choice"].as_str()?).ok()?, sent))
}

fn bridge_step(line: &str) -> Option<(i64, MatchStep)> {
    let (stamp, event) = line.rsplit_once(' ')?;
    let time = chrono::NaiveDateTime::parse_from_str(stamp, "%Y-%m-%d %H:%M:%S")
        .ok()?
        .and_local_timezone(chrono::Local)
        .single()?
        .timestamp_millis();
    Some((
        time,
        match event {
            "join_ack" => MatchStep::Ack,
            "join-failed" => MatchStep::Failed,
            "prepare-failed" | "room-closed" => MatchStep::Closed,
            _ => return None,
        },
    ))
}

#[derive(Default)]
struct MatchView {
    last: Option<MatchStep>,
    start: Option<i64>,
    replacement: bool,
    choice_id: Option<uuid::Uuid>,
    archive_sent: Option<bool>,
}

impl MatchView {
    fn accept(&mut self, ms: i64, stage: MatchStep, bridge: bool) -> bool {
        use MatchStep::*;
        if !bridge && stage == Prepare {
            self.archive_sent = None;
            self.replacement = self.start.is_some();
            self.start = Some(ms);
        }
        if bridge {
            let Some(start) = self.start else {
                return false;
            };
            // 交接日志只有秒、没有选择号：同秒重选时拒绝归属不明的旧回执。
            if ms / 1000 < start / 1000 || (self.replacement && ms / 1000 == start / 1000) {
                return false;
            }
            if !matches!(self.last, Some(Prepare | Ticket | Failed)) {
                return false;
            }
            if stage == Ack && !matches!(self.last, Some(Ticket | Failed)) {
                return false;
            }
        }
        if self.last == Some(stage) {
            return false;
        }
        self.last = Some(stage);
        true
    }
    fn room_started(&mut self, started_choice: Option<uuid::Uuid>) -> bool {
        if self.last == Some(MatchStep::Ack)
            && started_choice.is_some()
            && started_choice == self.choice_id
        {
            self.last = Some(MatchStep::RoundStarted);
            return true;
        }
        false
    }

    fn archive_status(&mut self, started: Option<uuid::Uuid>, events: &[(uuid::Uuid, bool)]) -> Option<MatchStep> {
        for &(choice,sent) in events {
            if self.choice_id == Some(choice) {
                self.archive_sent = Some(self.archive_sent.unwrap_or(false) || sent);
            }
        }
        if started.is_none() || started != self.choice_id ||
            !matches!(self.last, Some(MatchStep::RoundStarted | MatchStep::LoadoutWaiting | MatchStep::LoadoutSent)) {
            return None;
        }
        let step = if self.archive_sent? { MatchStep::LoadoutSent } else { MatchStep::LoadoutWaiting };
        if self.last == Some(step) { return None; }
        self.last = Some(step);
        Some(step)
    }
}

#[cfg(test)]
#[path = "../../integrations/native-menu/match_status_tests.rs"]
mod match_feedback_tests;

fn watch_match(
    ui: slint::Weak<AppWindow>,
    tx: Sender<AppMessage>,
    home: &Path,
    target: &Path,
    epoch: u64,
    mut bridge: MatchTail,
    stop: &AtomicBool,
    lang: i32,
) {
    let files = core::process::launch_files(target);
    let Ok(Some(lease)) = core::local_profile::running_pve_room_lease(home, &files) else {
        return;
    };
    let now = || chrono::Utc::now().timestamp_millis();
    let Some((run, _)) = live_pool(&lease, now()) else {
        return;
    };
    let mut menu = MatchTail::default();
    let (login, join) = (home.join("login-server.log"), home.join("pve-join.log"));
    let mut view = MatchView::default();
    let target_text = target.display().to_string();
    loop {
        if stop.load(Ordering::Relaxed) || LAUNCH_EPOCH.load(Ordering::Relaxed) != epoch {
            break;
        }
        let current = live_pool(&lease, now());
        if current.as_ref().map(|s| &s.0) != Some(&run) {
            if matches!(view.last, Some(MatchStep::Prepare | MatchStep::Ticket | MatchStep::Ack | MatchStep::RoundStarted | MatchStep::LoadoutWaiting | MatchStep::LoadoutSent)) {
                let last = view.last;
                let _ = ui.upgrade_in_event_loop(move |w| {
                    if LAUNCH_EPOCH.load(Ordering::Relaxed) == epoch
                        && last.is_some_and(|s| w.get_status_text().as_str() == s.label(lang))
                    {
                        w.set_status_text(MatchStep::End.label(lang).into());
                    }
                });
            }
            break;
        }
        let mut steps = Vec::new();
        let mut archive = Vec::new();
        menu.poll(&login, |s| {
            if let Some((ms, stage, id)) = menu_step(s, &run) {
                steps.push((ms, stage, id, false));
            } else if let Some(event) = spawn_event(s, &run) {
                archive.push(event);
            }
        });
        bridge.poll(&join, |s| {
            if let Some((ms, stage)) = bridge_step(s) {
                steps.push((ms, stage, None, true));
            }
        });
        steps.sort_by_key(|(ms, _, _, bridge)| (ms / 1000, *bridge, *ms));
        let mut updates = Vec::new();
        for (ms, stage, id, from_bridge) in steps {
            if stage == MatchStep::Prepare {
                view.choice_id = id;
            }
            if view.accept(ms, stage, from_bridge) {
                updates.push(stage);
            }
        }
        let started = current.and_then(|s| s.1);
        let began = view.room_started(started);
        if let Some(stage) = view.archive_status(started, &archive) {
            updates.push(stage);
        } else if began {
            updates.push(MatchStep::RoundStarted);
        }
        for stage in updates {
            let label = stage.label(lang);
            let target = target_text.clone();
            let _ = tx.send(AppMessage::Log(format!("[{}] {label}", core::now_text())));
            let _ = ui.upgrade_in_event_loop(move |w| show_match_step(&w, epoch, &target, stage));
        }
        thread::sleep(Duration::from_millis(300));
    }
}

impl AppController {
    /// 在工作线程中启动安装或更新。
    pub(super) fn start_install(&mut self) {
        if self.ui.get_busy() { return; }

        let target = match self.require_target() {
            Ok(target) => target,
            Err(error) => {
                self.show_error_dialog(
                    self.tr("安装", "Install", "インストール"),
                    &error.to_string(),
                );
                return;
            }
        };
        self.ui.set_busy(true);
        self.ui.set_status_text(
            self.tr("安装中...", "Installing...", "インストール中...")
                .into(),
        );
        self.ui.set_install_progress_cancelable(true);
        self.ui.set_install_progress_dialog_title(
            self.tr("安装进度", "Install Progress", "インストール進捗")
                .into(),
        );
        self.ui.set_install_progress_visible(true);
        self.ui.set_install_progress_value(0.0);
        self.ui.set_install_progress_percent("0%".into());
        self.ui.set_install_progress_title(
            self.tr("准备安装", "Preparing install", "インストール準備中")
                .into(),
        );
        self.set_install_progress_detail_text(&target.display().to_string());
        let cancel_token = InstallCancelToken::new();
        self.install_cancel = Some(cancel_token.clone());
        let core = self.core.clone();
        let tx = self.tx.clone();
        let progress_tx = tx.clone();
        let progress = Arc::new(move |progress: InstallProgress| {
            let _ = progress_tx.send(AppMessage::InstallProgress(progress));
        });
        thread::spawn(move || {
            match core.install_community(&target, progress, cancel_token.clone()) {
                Ok(dialog) => {
                    let _ = tx.send(AppMessage::ActionFinished {
                        title: "安装".to_string(),
                        status: "完成".to_string(),
                        dialog,
                        process_status: None,
                        target: Some(target),
                    });
                }
                Err(error) => {
                    let status = if cancel_token.is_cancelled() || error.to_string() == "安装已取消"
                    {
                        "已取消"
                    } else {
                        "执行失败"
                    };
                    let _ = tx.send(AppMessage::ActionFailed {
                        title: "安装".to_string(),
                        status: status.to_string(),
                        error: if cancel_token.is_cancelled() { error.to_string() } else {
                            format!("{error:#}")
                        },
                    });
                }
            }
        });
    }

    pub(super) fn cancel_install(&mut self) {
        let Some(cancel) = self.install_cancel.as_ref() else {
            return;
        };
        cancel.cancel();
        self.ui.set_status_text(
            self.tr(
                "正在取消安装...",
                "Cancelling install...",
                "インストールをキャンセル中...",
            )
            .into(),
        );
        self.ui
            .set_install_progress_title(self.tr("正在取消", "Cancelling", "キャンセル中").into());
        self.set_install_progress_detail_text(self.tr(
            "正在等待当前步骤停止。",
            "Waiting for the current step to stop.",
            "現在の処理が停止するのを待っています。",
        ));
        self.append_log(&format!("[{}] 用户请求取消安装。", core::now_text()));
    }

    /// 在工作线程中启动卸载。
    pub(super) fn start_uninstall(&mut self) {
        if !self.is_admin {
            self.show_error_dialog(
                self.tr("卸载", "Uninstall", "アンインストール"),
                self.tr(
                    "未使用管理员模式启动",
                    "Not started as administrator",
                    "管理者モードで起動していません",
                ),
            );
            return;
        }

        let target = match self.require_target() {
            Ok(target) => target,
            Err(error) => {
                self.show_error_dialog(
                    self.tr("卸载", "Uninstall", "アンインストール"),
                    &error.to_string(),
                );
                return;
            }
        };
        let language = self.language();
        self.ui.set_busy(true);
        self.ui.set_status_text(
            self.tr("卸载中...", "Uninstalling...", "アンインストール中...")
                .into(),
        );
        self.ui.set_install_progress_visible(false);
        let core = self.core.clone();
        let tx = self.tx.clone();
        thread::spawn(move || match core.uninstall(&target) {
            Ok(dialog) => {
                let _ = tx.send(AppMessage::ActionFinished {
                    title: "卸载".to_string(),
                    status: "完成".to_string(),
                    dialog,
                    process_status: Some(
                        i18n::tr(
                            language,
                            "运行进程：未检测",
                            "Runtime processes: not checked",
                            "実行中プロセス: 未確認",
                        )
                        .to_string(),
                    ),
                    target: Some(target),
                });
            }
            Err(error) => {
                let _ = tx.send(AppMessage::ActionFailed {
                    title: "卸载".to_string(),
                    status: "执行失败".to_string(),
                    error: error.to_string(),
                });
            }
        });
    }

    /// 执行启动前校验，并在需要本地登录服务器时请求确认端口冲突处理。
    pub(super) fn start_launch(&mut self, mode: LaunchMode) {
        if self.ui.get_busy() { return; }
        let title = format!("启动 {}", mode.display_name());
        let display_title = self.localize_action_title(&title);
        if matches!(mode, LaunchMode::Pve) && self.ui.get_vnt_running() && self.ui.get_vnt_no_tun() {
            self.show_error_dialog(
                &display_title,
                &self.tr(
                    "无网卡模式下不能同时启动 PVE 服务端，请切换到网卡模式后再启动。",
                    "The PVE server cannot be launched in no-adapter mode. Switch to adapter mode first.",
                    "アダプターなしモードでは PVE サーバーを同時起動できません。アダプターモードに切り替えてください。",
                ),
            );
            return;
        }
        let target = match self.require_target() {
            Ok(target) => target,
            Err(error) => {
                self.show_error_dialog(&display_title, &error.to_string());
                return;
            }
        };

        let conflicts = if mode.uses_login_server() {
            match self.core.launch_port_conflicts(&target, mode) {
                Ok(conflicts) => conflicts,
                Err(error) => {
                    self.show_error_dialog(&display_title, &error.to_string());
                    return;
                }
            }
        } else {
            Vec::new()
        };
        if mode.uses_login_server() && !conflicts.is_empty() {
            self.show_confirm_dialog_with_tertiary(
                &format!(
                    "{} {}",
                    mode.display_name(),
                    self.tr("端口占用检测", "Port Conflict Check", "ポート使用確認")
                ),
                &format!(
                    "{} {} {}\n{}\n\n{}",
                    self.tr("检测到", "Ports required by", ""),
                    mode.display_name(),
                    self.tr(
                        "启动所需端口已被占用：",
                        "launch are already in use:",
                        " の起動に必要なポートが使用中です:"
                    ),
                    format_port_conflicts(&conflicts),
                    self.tr(
                        "是否关闭这些占用端口的进程后继续启动？",
                        "Stop those processes and continue launching?",
                        "これらのプロセスを停止して起動を続行しますか？"
                    )
                ),
                &format!(
                    "{} {}",
                    self.tr("关闭并启动", "Stop and launch", "停止して起動"),
                    mode.display_name()
                ),
                self.tr("取消", "Cancel", "キャンセル"),
                self.tr("仍然启动", "Launch anyway", "そのまま起動"),
                PendingDialogAction::LaunchWithConflicts {
                    target,
                    mode,
                    conflicts,
                },
            );
            return;
        }
        self.start_launch_with_conflicts(target, mode, conflicts);
    }

    /// 可选关闭已知端口冲突进程后启动指定模式。
    pub(super) fn start_launch_with_conflicts(
        &mut self,
        target: PathBuf,
        mode: LaunchMode,
        conflicts: Vec<PortConflict>,
    ) {
        let epoch = LAUNCH_EPOCH.fetch_add(1, Ordering::Relaxed) + 1;
        let stop = self.stop_background.clone();
        let ui = self.ui.as_weak();
        self.ui.set_busy(true);
        self.ui.set_install_progress_visible(false);
        self.ui.set_status_text(
            format!(
                "{} {}...",
                self.tr("启动中", "Launching", "起動中"),
                mode.display_name()
            )
            .into(),
        );
        let language = self.language();
        let core = self.core.clone();
        let tx = self.tx.clone();
        thread::spawn(move || {
            let mut bridge = MatchTail::default();
            if matches!(mode, LaunchMode::Pve) {
                bridge.poll(&core.installer_home.join("pve-join.log"), |_| {});
            }
            let result = (|| -> Result<String> {
                if !conflicts.is_empty() {
                    core.stop_port_conflict_processes(&conflicts)?;
                    thread::sleep(Duration::from_secs(1));
                    let remaining = core.launch_port_conflicts(&target, mode)?;
                    if !remaining.is_empty() {
                        bail!(
                            "{}\n{}",
                            i18n::tr(
                                language,
                                "端口仍被占用，无法继续启动：",
                                "Ports are still in use; launch cannot continue:",
                                "ポートがまだ使用中のため、起動を続行できません:"
                            ),
                            format_port_conflicts(&remaining)
                        );
                    }
                }
                core.launch(&target, mode)
            })();

            match result {
                Ok(dialog) => {
                    let _ = tx.send(AppMessage::ActionFinished {
                        title: format!("启动 {}", mode.display_name()),
                        status: "完成".to_string(),
                        dialog,
                        process_status: None,
                        target: Some(target.clone()),
                    });
                    if matches!(mode, LaunchMode::Pve) {
                        watch_match(ui, tx, &core.installer_home, &target, epoch, bridge, &stop, language);
                    }
                }
                Err(error) => {
                    let _ = tx.send(AppMessage::ActionFailed {
                        title: format!("启动 {}", mode.display_name()),
                        status: "执行失败".to_string(),
                        error: error.to_string(),
                    });
                }
            }
        });
    }

    pub(super) fn start_join_match(&mut self, address: &str) {
        match crate::core::game_endpoint(address) {
            Some(endpoint) => self.start_launch(LaunchMode::Join(endpoint)),
            None => self.show_error_dialog(
                self.tr("加入对局", "Join match", "対戦に参加"),
                self.tr("请输入 IPv4 地址和可选的游戏端口。", "Enter an IPv4 address with an optional game port.", "IPv4 アドレスと任意のゲームポートを入力してください。"),
            ),
        }
    }

    /// 在工作线程中运行进程和端口诊断。
    pub(super) fn start_detect_processes(&mut self) {
        let target = match self.require_target() {
            Ok(target) => target,
            Err(error) => {
                self.show_error_dialog(
                    self.tr("进程检测", "Process Detection", "プロセス検出"),
                    &error.to_string(),
                );
                return;
            }
        };
        let language = self.language();
        self.ui.set_busy(true);
        self.ui.set_status_text(
            self.tr(
                "进程检测中...",
                "Detecting processes...",
                "プロセス検出中...",
            )
            .into(),
        );
        self.ui.set_install_progress_visible(false);
        let core = self.core.clone();
        let tx = self.tx.clone();
        thread::spawn(move || {
            let result = (|| -> Result<(String, String, Vec<CorePortStatusRow>)> {
                let snapshot = core.collect_runtime_processes(&target)?;
                let rows = core.port_status_rows_for_target(Some(&target))?;
                let conflicts = rows
                    .iter()
                    .filter_map(|row| row.conflict.clone())
                    .collect::<Vec<_>>();
                let has_runtime = runtime_snapshot_has_any(&snapshot);
                let has_conflicts = !conflicts.is_empty();
                let message = format_process_detection_message(&snapshot, &conflicts, language);
                let status = match (has_runtime, has_conflicts) {
                    (true, true) => format!(
                        "{} {} {}",
                        i18n::tr(
                            language,
                            "检测到运行进程和",
                            "Found runtime processes and",
                            "実行中プロセスと"
                        ),
                        conflicts.len(),
                        i18n::tr(language, "个端口占用", "port conflicts", "件のポート使用")
                    ),
                    (true, false) => i18n::tr(
                        language,
                        "检测到相关运行进程",
                        "Related runtime processes found",
                        "関連実行プロセスが見つかりました",
                    )
                    .to_string(),
                    (false, true) => format!(
                        "{} {} {}",
                        i18n::tr(language, "检测到", "Found", ""),
                        conflicts.len(),
                        i18n::tr(language, "个端口占用", "port conflicts", "件のポート使用")
                    ),
                    (false, false) => i18n::tr(
                        language,
                        "未检测到相关进程或端口占用",
                        "No related processes or port conflicts found",
                        "関連プロセスまたはポート使用は見つかりません",
                    )
                    .to_string(),
                };
                Ok((message, status, rows))
            })();

            match result {
                Ok((message, process_status, rows)) => {
                    let _ = tx.send(AppMessage::PortRows(rows));
                    let _ = tx.send(AppMessage::ActionFinished {
                        title: "进程检测".to_string(),
                        status: "完成".to_string(),
                        dialog: message,
                        process_status: Some(process_status),
                        target: Some(target),
                    });
                }
                Err(error) => {
                    let _ = tx.send(AppMessage::ActionFailed {
                        title: "进程检测".to_string(),
                        status: "执行失败".to_string(),
                        error: error.to_string(),
                    });
                }
            }
        });
    }

    /// 在工作线程中只关闭某一个端口行对应的占用进程。
    pub(super) fn start_stop_port_process(&mut self, protocol: String, port: i32, pid: i32) {
        let protocol = protocol.trim().to_uppercase();
        if protocol.is_empty() || port <= 0 || port > u16::MAX as i32 || pid <= 0 {
            self.show_error_dialog(
                self.tr("关闭端口进程", "Stop Port Process", "ポートプロセス停止"),
                self.tr(
                    "端口行没有可关闭的有效进程。",
                    "This port row has no valid process to stop.",
                    "このポート行には停止できる有効なプロセスがありません。",
                ),
            );
            return;
        }

        self.ui.set_busy(true);
        self.ui.set_install_progress_visible(false);
        self.ui.set_status_text(
            format!(
                "{} {}/{} PID {}...",
                self.tr("关闭中", "Stopping", "停止中"),
                protocol,
                port,
                pid
            )
            .into(),
        );
        let language = self.language();
        let core = self.core.clone();
        let tx = self.tx.clone();
        let target = self.current_target.clone();
        thread::spawn(move || {
            let result = (|| -> Result<(String, String, Vec<CorePortStatusRow>)> {
                let current = core.collect_port_conflicts()?;
                let conflict = current
                    .into_iter()
                    .find(|conflict| {
                        conflict.protocol.eq_ignore_ascii_case(&protocol)
                            && i32::from(conflict.port) == port
                            && i32::try_from(conflict.pid).ok() == Some(pid)
                    })
                    .ok_or_else(|| {
                        anyhow::anyhow!(
                            "{}/{} PID {} {}",
                            protocol,
                            port,
                            pid,
                            i18n::tr(
                                language,
                                "的端口占用已经不存在，请等待列表刷新。",
                                "is no longer using this port. Wait for the list to refresh.",
                                "のポート使用は既に存在しません。リストの更新をお待ちください。"
                            )
                        )
                    })?;

                let summary_before = format_port_conflicts(std::slice::from_ref(&conflict));
                core.stop_port_conflict_processes(std::slice::from_ref(&conflict))?;
                thread::sleep(Duration::from_millis(800));

                let remaining = core.collect_port_conflicts()?;
                let still_exists = remaining.iter().any(|item| {
                    item.protocol.eq_ignore_ascii_case(&protocol)
                        && i32::from(item.port) == port
                        && i32::try_from(item.pid).ok() == Some(pid)
                });
                let rows = core.port_status_rows_for_target(target.as_deref())?;
                let status = if still_exists {
                    format!(
                        "{}/{} PID {} {}",
                        protocol,
                        port,
                        pid,
                        i18n::tr(language, "仍在占用", "still in use", "まだ使用中")
                    )
                } else {
                    format!(
                        "{}/{} PID {} {}",
                        protocol,
                        port,
                        pid,
                        i18n::tr(
                            language,
                            "已请求关闭",
                            "stop requested",
                            "停止を要求しました"
                        )
                    )
                };
                let dialog = if still_exists {
                    format!(
                        "{}\n{}",
                        i18n::tr(
                            language,
                            "已请求关闭该端口进程，但仍检测到占用：",
                            "Stop was requested, but the port is still in use:",
                            "停止を要求しましたが、まだポート使用が検出されました:"
                        ),
                        summary_before
                    )
                } else {
                    format!(
                        "{}\n{}",
                        i18n::tr(
                            language,
                            "已关闭该端口对应进程：",
                            "Stopped the process for this port:",
                            "このポートのプロセスを停止しました:"
                        ),
                        summary_before
                    )
                };
                Ok((dialog, status, rows))
            })();

            match result {
                Ok((dialog, process_status, rows)) => {
                    let _ = tx.send(AppMessage::PortRows(rows));
                    let _ = tx.send(AppMessage::ActionFinished {
                        title: "关闭端口进程".to_string(),
                        status: "完成".to_string(),
                        dialog,
                        process_status: Some(process_status),
                        target: None,
                    });
                }
                Err(error) => {
                    let _ = tx.send(AppMessage::ActionFailed {
                        title: "关闭端口进程".to_string(),
                        status: "执行失败".to_string(),
                        error: error.to_string(),
                    });
                }
            }
        });
    }

    /// 在工作线程中关闭运行时进程和端口冲突进程。
    pub(super) fn start_stop_processes(&mut self) {
        let target = match self.require_target() {
            Ok(target) => target,
            Err(error) => {
                self.show_error_dialog(
                    self.tr(
                        "关闭所有进程",
                        "Stop All Processes",
                        "すべてのプロセスを停止",
                    ),
                    &error.to_string(),
                );
                return;
            }
        };
        self.ui.set_busy(true);
        self.ui.set_status_text(
            self.tr(
                "关闭所有进程中...",
                "Stopping all processes...",
                "すべてのプロセスを停止中...",
            )
            .into(),
        );
        self.ui.set_install_progress_visible(false);
        let language = self.language();
        let core = self.core.clone();
        let tx = self.tx.clone();
        thread::spawn(move || {
            let result = (|| -> Result<(String, String)> {
                let snapshot = core.collect_runtime_processes(&target)?;
                let mut conflicts = core.collect_port_conflicts()?;
                let has_runtime = runtime_snapshot_has_any(&snapshot);
                let had_initial_conflicts = !conflicts.is_empty();

                let mut messages = Vec::new();
                // 即使目标目录扫描没有命中，也要进入核心关闭逻辑；那里会按
                // Boundary 专用镜像名兜底处理 wrapper/game 残留进程。
                let runtime_message = core.stop_runtime_processes(&target)?;
                let runtime_was_empty = runtime_message.starts_with("当前没有需要关闭");
                messages.push(runtime_message);
                conflicts = core.collect_port_conflicts()?;
                if !conflicts.is_empty() {
                    messages.push(core.stop_port_conflict_processes(&conflicts)?);
                }

                thread::sleep(Duration::from_millis(800));
                let remaining_conflicts = core.collect_port_conflicts()?;
                let remaining_runtime = core.collect_runtime_processes(&target)?;
                let has_remaining_runtime = runtime_snapshot_has_any(&remaining_runtime);
                let process_status = if runtime_was_empty
                    && !has_runtime
                    && !had_initial_conflicts
                    && remaining_conflicts.is_empty()
                    && !has_remaining_runtime
                {
                    i18n::tr(
                        language,
                        "运行进程/端口占用：未发现",
                        "Runtime/port usage: none found",
                        "実行プロセス/ポート使用: 見つかりません",
                    )
                    .to_string()
                } else if remaining_conflicts.is_empty() && !has_remaining_runtime {
                    i18n::tr(
                        language,
                        "运行进程/端口占用：已关闭",
                        "Runtime/port usage: stopped",
                        "実行プロセス/ポート使用: 停止済み",
                    )
                    .to_string()
                } else if has_remaining_runtime {
                    i18n::tr(
                        language,
                        "仍有相关运行进程",
                        "Related runtime processes remain",
                        "関連実行プロセスが残っています",
                    )
                    .to_string()
                } else {
                    format!(
                        "{} {} {}",
                        i18n::tr(language, "仍有", "Still", ""),
                        remaining_conflicts.len(),
                        i18n::tr(
                            language,
                            "个端口占用",
                            "port conflicts remain",
                            "件のポート使用が残っています"
                        )
                    )
                };
                if has_remaining_runtime {
                    messages.push(format!(
                        "{}\n{}",
                        i18n::tr(
                            language,
                            "仍检测到相关运行进程：",
                            "Related runtime processes are still detected:",
                            "関連実行プロセスがまだ検出されています:"
                        ),
                        summarize_runtime_processes(&remaining_runtime, language)
                    ));
                }
                if !remaining_conflicts.is_empty() {
                    messages.push(format!(
                        "{}\n{}",
                        i18n::tr(
                            language,
                            "仍检测到端口占用：",
                            "Port usage is still detected:",
                            "ポート使用がまだ検出されています:"
                        ),
                        format_port_conflicts(&remaining_conflicts)
                    ));
                }

                Ok((messages.join("\n\n"), process_status))
            })();

            match result {
                Ok((dialog, process_status)) => {
                    let _ = tx.send(AppMessage::ActionFinished {
                        title: "关闭所有进程".to_string(),
                        status: "完成".to_string(),
                        dialog,
                        process_status: Some(process_status),
                        target: Some(target),
                    });
                }
                Err(error) => {
                    let _ = tx.send(AppMessage::ActionFailed {
                        title: "关闭所有进程".to_string(),
                        status: "执行失败".to_string(),
                        error: error.to_string(),
                    });
                }
            }
        });
    }
}
