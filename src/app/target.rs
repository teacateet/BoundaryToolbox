//! AppController — target directory detection (Steam auto-detect only).

use super::*;

impl AppController {
    /// Refresh target from Steam auto-detection.
    pub(super) fn refresh_target_from_detection(&mut self, initial: bool) {
        match self.core.detect_steam_game_win64() {
            Ok((path, message)) => {
                self.ui.set_detected_text(message.clone().into());
                let ready = self.tr("已就绪", "Ready", "準備完了");
                self.set_current_target(Some(path), ready);
                if !initial {
                    self.append_log(&format!("[{}] {}", core::now_text(), message));
                }
            }
            Err(error) => {
                let text = format!(
                    "{}{}",
                    self.tr(
                        "自动识别失败：",
                        "Auto detection failed: ",
                        "自動検出失敗: "
                    ),
                    error
                );
                self.ui.set_detected_text(text.clone().into());
                let status = self.tr(
                    "可手动输入路径",
                    "Enter a path manually",
                    "手動でパスを入力してください",
                );
                self.ui.set_status_text(status.into());
                self.set_current_target(None, status);
                if !initial {
                    self.append_log(&format!("[{}] 自动识别失败：{}", core::now_text(), error));
                }
            }
        }
    }

    /// User typed a manual path — try to resolve it.
    pub(super) fn on_manual_path_changed(&mut self, text: String) {
        self.ui.set_manual_path(text.clone().into());
        let raw = text.trim().to_string();
        if raw.is_empty() {
            // Fall back to auto-detect.
            self.refresh_target_from_detection(false);
            return;
        }
        match self.core.normalize_selected_path(Path::new(&raw)) {
            Ok(path) => {
                let ready = self.tr("已就绪", "Ready", "準備完了");
                self.set_current_target(Some(path.clone()), ready);
                self.append_log(&format!(
                    "[{}] 手动路径已解析：{}",
                    core::now_text(),
                    path.display()
                ));
            }
            Err(error) => {
                self.ui.set_status_text(error.to_string().into());
                self.set_current_target(None, &error.to_string());
                self.append_log(&format!("[{}] 手动路径无效：{}", core::now_text(), error));
            }
        }
    }

    /// Open folder-browse dialog.
    pub(super) fn browse_path(&mut self) {
        if self.ui.get_busy() {
            return;
        }
        let initial = if !self.ui.get_manual_path().is_empty() {
            self.ui.get_manual_path().to_string()
        } else {
            self.current_target
                .as_ref()
                .map(|path| path.display().to_string())
                .unwrap_or_default()
        };
        self.show_path_dialog(&initial);
    }

    /// Save current target and sync to Slint.
    pub(super) fn set_current_target(&mut self, path: Option<PathBuf>, status: &str) {
        if self.current_target != path {
            if self.public_rooms.preparing || self.public_rooms.advertising() {
                self.disconnect_friend_room();
                self.clear_public_listing();
            }
            self.ui.set_public_rows(Default::default());
            self.public_rooms.target_changed();
        }
        self.ui.set_room_client_players(-1);
        self.ui.set_room_host_players(-1);
        self.current_target = path;
        if let Ok(mut target) = self.port_target.write() {
            *target = self.current_target.clone();
        }
        self.ui.set_target_text(
            self.current_target
                .as_ref()
                .map(|path| path.display().to_string())
                .unwrap_or_else(|| {
                    self.tr(
                        "未解析到有效的安装目录",
                        "No valid install path resolved",
                        "有効なインストール先が見つかりません",
                    )
                    .to_string()
                })
                .into(),
        );
        self.ui.set_status_text(status.into());
        if self.current_target.is_none() {
            self.ui.set_process_status_text(
                self.tr(
                    "运行进程：未检测",
                    "Runtime processes: not checked",
                    "実行中プロセス: 未確認",
                )
                .into(),
            );
        }
        self.sync_has_target();
    }


    /// Enable/disable UI based on whether target is available.
    pub(super) fn sync_has_target(&self) {
        self.ui.set_has_target(self.current_target.is_some());
    }

    /// Return validated target, or a user-readable error.
    pub(super) fn require_target(&mut self) -> Result<PathBuf> {
        let path = match self.current_target.as_deref() {
            Some(selected) => self.core.normalize_selected_path(selected)?,
            None => self.core.detect_steam_game_win64()?.0,
        };
        let ready = self.tr("已就绪", "Ready", "準備完了");
        self.set_current_target(Some(path.clone()), ready);
        Ok(path)
    }
}
