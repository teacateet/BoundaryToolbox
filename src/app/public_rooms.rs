use super::*;
use crate::steam_directory::{PREFIX, Room};
use std::{
    io::{BufRead, BufReader, Write},
    process::{ChildStdin, Stdio},
    time::Instant,
};
use uuid::Uuid;

#[derive(Default)]
pub(super) struct DirectoryState {
    pub(super) preparing: bool,
    // 关闭写端会通知 SDK 子进程撤下公开大厅；主进程意外退出也会触发 EOF。
    host: Option<(Uuid, ChildStdin)>,
    fetching: bool,
    last_refresh: Option<Instant>,
}
impl DirectoryState {
    pub(super) fn target_changed(&mut self) {
        self.last_refresh = None;
    }
    pub(super) fn advertising(&self) -> bool {
        self.host.is_some()
    }
}

fn command(target: &std::path::Path, op: &str) -> anyhow::Result<std::process::Command> {
    let mut cmd = core::util::hidden_command(std::env::current_exe()?);
    cmd.args(["--brt-steam-rooms", op])
        .current_dir(target)
        .env("SteamAppId", "1364020")
        .env("SteamGameId", "1364020")
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    Ok(cmd)
}

impl AppController {
    pub(super) fn clear_public_listing(&mut self) {
        let was_public = self.public_rooms.preparing || self.public_rooms.host.is_some();
        self.public_rooms.host.take();
        self.public_rooms.preparing = false;
        self.ui.set_public_room_active(false);
        if was_public {
            self.ui.set_public_host_status(
                self.tr(
                    "已撤下公开房间。",
                    "Public listing withdrawn.",
                    "公開を解除しました。",
                )
                .into(),
            );
        }
    }

    pub(super) fn toggle_public_room(&mut self) {
        if self.public_rooms.preparing || self.public_rooms.advertising() {
            self.clear_public_listing();
            // 同时撤销旧邀请；下一次私下分享会生成新凭据。
            self.disconnect_friend_room();
            return;
        }
        if self.ui.get_busy()
            || self.ui.get_friend_room_working()
            || self.open_room_port().is_none()
        {
            return;
        }
        self.public_rooms.preparing = true;
        self.ui.set_public_room_active(true);
        self.ui.set_public_host_status(
            self.tr("正在公开房间…", "Publishing room…", "ルームを公開中…")
                .into(),
        );
        self.share_friend_room();
        if self.public_rooms.preparing && !self.ui.get_friend_room_working() {
            self.clear_public_listing();
            self.ui.set_public_host_status(
                self.tr(
                    "未公开，请先确认好友连接就绪。",
                    "Not published. Check the friend connection.",
                    "未公開です。フレンド接続を確認してください。",
                )
                .into(),
            );
        }
    }

    pub(super) fn start_public_listing(&mut self, invite: friend_invite::Invite) {
        self.public_rooms.preparing = false;
        self.ui.set_friend_room_notice("".into());
        let id = Uuid::new_v4();
        let run = (|| -> anyhow::Result<ChildStdin> {
            anyhow::ensure!(
                matches!(invite, friend_invite::Invite::Friend { .. }),
                "只能公开好友房"
            );
            let (target, port) = self
                .active_fixed_room
                .as_ref()
                .ok_or_else(|| anyhow::anyhow!("房间已经关闭"))?;
            anyhow::ensure!(
                core::fixed_host::owned_room_listening(target, *port)?,
                "房间已经关闭"
            );
            let mut child = command(target, "host")?.stdin(Stdio::piped()).spawn()?;
            let mut keep = child.stdin.take().unwrap();
            if writeln!(keep, "{invite}").is_err() {
                drop(keep);
                let _ = child.wait();
                anyhow::bail!("无法启动 Steam 房间目录");
            }
            let out = child.stdout.take().unwrap();
            let tx = self.tx.clone();
            thread::spawn(move || {
                let mut ready = false;
                for line in BufReader::new(out).lines().map_while(Result::ok) {
                    if line == format!("{PREFIX}READY") {
                        ready = true;
                        let _ = tx.send(AppMessage::PublicListing(id, Ok(())));
                    }
                }
                let reason = child
                    .wait_with_output()
                    .ok()
                    .map(|out| safe_error(&out.stderr))
                    .filter(|m| !m.is_empty())
                    .unwrap_or_else(|| {
                        if ready {
                            "公开连接已结束".into()
                        } else {
                            "Steam 未能公开房间".into()
                        }
                    });
                let _ = tx.send(AppMessage::PublicListing(id, Err(reason)));
            });
            Ok(keep)
        })();
        match run {
            Ok(keep) => self.public_rooms.host = Some((id, keep)),
            Err(e) => {
                self.clear_public_listing();
                self.ui
                    .set_public_host_status(format!("公开失败：{e}").into());
            }
        }
    }

    pub(super) fn public_listing_result(&mut self, id: Uuid, result: Result<(), String>) {
        if self.public_rooms.host.as_ref().map(|p| p.0) != Some(id) {
            return;
        }
        match result {
            Ok(()) => {
                self.ui.set_public_host_status(
                    self.tr(
                        "房间已公开，其他 BRT 玩家可以看到你的 Steam 昵称并加入。",
                        "Public. Other BRT players can see your Steam name and join.",
                        "公開しました。ほかの BRT プレイヤーが Steam 名を確認して参加できます。",
                    )
                    .into(),
                );
                self.refresh_public_rooms();
            }
            Err(e) => {
                self.disconnect_friend_room();
                self.ui
                    .set_public_host_status(format!("公开连接已结束：{e}").into());
            }
        }
    }

    pub(super) fn tick_public_rooms(&mut self) {
        if self.public_rooms.advertising() && !self.ui.get_busy() && self.open_room_port().is_none()
        {
            self.clear_public_listing();
            self.disconnect_friend_room();
        }
        if self.ui.get_page() == 2
            && self
                .public_rooms
                .last_refresh
                .is_none_or(|t| t.elapsed() >= Duration::from_secs(20))
        {
            self.refresh_public_rooms();
        }
    }
    pub(super) fn refresh_public_rooms(&mut self) {
        if self.public_rooms.fetching {
            return;
        }
        self.public_rooms.last_refresh = Some(Instant::now());
        let Some(target) = self.current_target.clone() else {
            self.ui.set_public_rows(Default::default());
            self.ui.set_public_status(
                self.tr(
                    "选择 Steam 游戏目录后可以查看公开房间。",
                    "Select your Steam game folder to browse public rooms.",
                    "Steam ゲームフォルダーを選ぶと公開ルームが表示されます。",
                )
                .into(),
            );
            return;
        };
        self.public_rooms.fetching = true;
        self.ui.set_public_loading(true);
        let tx = self.tx.clone();
        thread::spawn(move || {
            let result = (|| -> anyhow::Result<Vec<Room>> {
                let out = command(&target, "list")?.stdin(Stdio::null()).output()?;
                anyhow::ensure!(out.status.success(), "{}", safe_error(&out.stderr));
                let raw = std::str::from_utf8(&out.stdout)?
                    .lines()
                    .find_map(|line| line.strip_prefix(PREFIX))
                    .ok_or_else(|| anyhow::anyhow!("未收到 Steam 大厅列表"))?;
                Ok(serde_json::from_str(raw)?)
            })();
            let _ = tx.send(AppMessage::PublicRows(
                target,
                result.map_err(|e| e.to_string()),
            ));
        });
    }
    pub(super) fn public_rows_result(
        &mut self,
        target: PathBuf,
        result: Result<Vec<Room>, String>,
    ) {
        self.public_rooms.fetching = false;
        self.ui.set_public_loading(false);
        if self.current_target.as_ref() != Some(&target) {
            self.ui.set_public_rows(Default::default());
            self.public_rooms.last_refresh = None;
            return;
        }
        match result {
            Ok(rooms) => {
                let rows = rooms
                    .into_iter()
                    .filter(|r| public_invite(&r.invite))
                    .map(|r| ServerRow {
                        name: r.host.into(),
                        address: r.invite.into(),
                        joinable: true,
                        active: true,
                        ..Default::default()
                    })
                    .collect::<Vec<_>>();
                self.ui.set_public_status(
                    if rows.is_empty() {
                        self.tr(
                            "当前没有公开房间。",
                            "No public rooms right now.",
                            "現在公開ルームはありません。",
                        )
                    } else {
                        self.tr(
                            "选择房主即可加入。",
                            "Choose a host to join.",
                            "ホストを選んで参加できます。",
                        )
                    }
                    .into(),
                );
                self.ui
                    .set_public_rows(slint::ModelRc::new(VecModel::from(rows)));
            }
            Err(e) => {
                self.ui.set_public_rows(Default::default());
                self.ui
                    .set_public_status(format!("无法刷新公开房间：{e}").into());
            }
        }
    }
}
fn safe_error(raw: &[u8]) -> String {
    String::from_utf8_lossy(raw)
        .lines()
        .find(|s| s.starts_with("Error:"))
        .unwrap_or("Steam 大厅不可用，请确认 Steam 在线后重试。")
        .trim_start_matches("Error: ")
        .chars()
        .take(180)
        .collect()
}
