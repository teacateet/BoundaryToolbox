//! Slint 应用控制器、UI 模型和后台任务调度。

use crate::core;
use crate::vnt_platform;
use crate::{AppWindow, DriveRow, PortRow, ProxyRow, ServerRow, VntPeerRow, VntServerRow};

use std::cell::RefCell;
use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::net::Ipv4Addr;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::{Arc, RwLock};
use std::thread;
use std::time::Duration;

use anyhow::{Result, bail};
use crossbeam_channel::{Receiver, Sender, unbounded};
use slint::{
    CloseRequestResponse, ComponentHandle, Model, ModelRc, SharedString, Timer, TimerMode, VecModel,
};

use crate::core::{
    APP_VERSION, InstallCancelToken, InstallProgress, InstallerCore, LaunchMode, MONITORED_PORTS,
    PortConflict, PortStatusRow as CorePortStatusRow, SystemCheckReport,
    format_port_conflicts,
};
use crate::vnt_platform::{VntEvent, VntLaunchOptions, VntSession};

mod actions;
#[cfg(test)]
mod ui_smoke;
mod background;
mod close_guard;
mod controller;
mod diagnostics;
mod dialogs;
mod drive_scan;
mod fixed_room;
mod font;
mod i18n;
mod logging;
mod messages;
mod prefs;
mod proxy_list;
mod server_list;
mod servers;
mod system_checks;
mod target;
mod tutorial;
mod update;
mod updates;
mod vnt_controller;
mod vnt_rows;
mod window;

use background::spawn_port_thread;
const UPSTREAM_SELF_UPDATE: bool = false;
use diagnostics::{
    format_process_detection_message, runtime_snapshot_has_any, summarize_runtime_processes,
};
use dialogs::estimate_dialog_text_lines;
use prefs::{AppPrefs, VntPrefs};
use proxy_list::{
    AUTO_SELECT_PROXY_URL, GithubProxyOption, fastest_github_proxy_url, initial_github_proxy_rows,
    is_auto_select_github_proxy_url, proxy_options_to_rows,
};
use server_list::{RemoteServer, fetch_servers, server_placeholder_row, server_to_row};
use update::{
    UpdateCheckResult, check_latest_release, download_release_asset,
    schedule_self_replace_and_restart, update_dialog_text, update_status_text,
};
use vnt_rows::{
    apply_vnt_idle_to_ui, localized_vnt_idle_snapshot, vnt_peer_to_row, vnt_placeholder_rows,
    vnt_server_placeholder_rows, vnt_server_to_row,
};

pub(crate) fn run() -> Result<()> {
    logging::install_log_filter();
    let app = AppWindow::new()?;
    window::apply_adaptive_window_geometry(&app);
    let controller = AppController::new(app)?;
    AppController::bind_callbacks(&controller);
    controller.borrow().initialize_tutorial_state();
    AppController::start_background_timers(&controller);
    let mut args = std::env::args_os().skip(1);
    if args.next().as_deref() == Some(std::ffi::OsStr::new("--game-dir")) {
        let path = args.next().filter(|p| !p.is_empty()).ok_or_else(|| anyhow::anyhow!("--game-dir 需要游戏目录"))?;
        let mut c = controller.borrow_mut();
        let target = c.core.normalize_selected_path(&path)?;
        let ready = c.tr("已就绪", "Ready", "準備完了");
        c.set_current_target(Some(target), ready);
        c.start_system_check();
    } else {
        controller.borrow_mut().initialize();
    }
    let ui = controller.borrow().ui.clone_strong();
    let stop_background = controller.borrow().stop_background.clone();
    ui.run()?;
    stop_background.store(true, Ordering::Relaxed);
    controller.borrow_mut().shutdown();
    Ok(())
}

/// 工作线程发送回 Slint 主线程的消息。
#[derive(Debug, Clone)]
enum AppMessage {
    Log(String),
    PortRows(Vec<CorePortStatusRow>),
    ServerRows(Vec<RemoteServer>),
    ServerRowsFailed(String),
    FixedRoomFinished {
        target: PathBuf,
        port: u16,
        action: fixed_room::RoomAction,
        result: std::result::Result<fixed_room::RoomOutcome, String>,
    },
    GithubProxyRows {
        rows: Vec<GithubProxyOption>,
        fetched_count: usize,
        update_time: Option<String>,
    },
    GithubProxyRowsFailed(String),
    UpdateCheckFinished {
        result: UpdateCheckResult,
        automatic: bool,
    },
    UpdateCheckFailed {
        error: String,
        automatic: bool,
    },
    UpdateRestartScheduled {
        tag: String,
    },
    UpdateDownloadFailed(String),
    SystemCheckFinished(SystemCheckReport),
    VntEvent(VntEvent),
    InstallProgress(InstallProgress),
    ActionFinished {
        title: String,
        status: String,
        dialog: String,
        process_status: Option<String>,
        target: Option<PathBuf>,
    },
    ActionFailed {
        title: String,
        status: String,
        error: String,
    },
    ScanFinished {
        result: Option<PathBuf>,
        dialog: String,
    },
}

/// 自定义应用内弹窗背后等待执行的动作。
enum PendingDialogAction {
    None,
    LaunchWithConflicts {
        target: PathBuf,
        mode: LaunchMode,
        conflicts: Vec<PortConflict>,
    },
    ManualPathInput,
    DownloadUpdate {
        result: UpdateCheckResult,
    },
    CloseApplication,
}

/// 供 Slint 回调共享的可变应用状态。
///
/// `AppController` 是 UI 层的唯一状态聚合点：它持有 Slint 窗口、列表模型、
/// 用户偏好和后台消息通道。真正会改文件、下载、杀进程的操作必须继续放在
/// `InstallerCore`，这里仅做调度和 UI 状态刷新。
struct AppController {
    ui: AppWindow,
    core: Arc<InstallerCore>,
    // 后台线程只允许通过 tx 发 AppMessage；UI 线程在 drain_messages 中消费 rx。
    tx: Sender<AppMessage>,
    rx: Receiver<AppMessage>,
    // 长驻后台任务通过 stop_background 退出，active_page 用于避免非当前页持续刷新。
    stop_background: Arc<AtomicBool>,
    active_page: Arc<AtomicI32>,
    port_target: Arc<RwLock<Option<PathBuf>>>,
    session_log_file: File,
    current_target: Option<PathBuf>,
    active_fixed_room: Option<(PathBuf, u16)>,
    fixed_room_start: Option<Arc<core::fixed_host::StartAttempt>>,
    // Slint 列表模型在启动时创建一次，后续原地更新，避免 ListView 绑定被替换。
    drive_model: Rc<VecModel<DriveRow>>,
    port_model: Rc<VecModel<PortRow>>,
    server_model: Rc<VecModel<ServerRow>>,
    github_proxy_model: Rc<VecModel<ProxyRow>>,
    vnt_server_option_model: Rc<VecModel<SharedString>>,
    vnt_server_model: Rc<VecModel<VntServerRow>>,
    vnt_peer_model: Rc<VecModel<VntPeerRow>>,
    vnt_session: Option<VntSession>,
    vnt_host_selection_pending: bool,
    vnt_restart_after_stop: Option<VntLaunchOptions>,
    system_report: Option<SystemCheckReport>,
    app_prefs: AppPrefs,
    is_admin: bool,
    install_cancel: Option<InstallCancelToken>,
    auto_select_github_proxy_after_refresh: bool,
    pending_dialog_action: PendingDialogAction,
}
