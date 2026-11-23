//! VNT 状态快照到 Slint 行模型的转换。

use crate::vnt_platform::{self, VntPeer, VntServer};
use crate::{AppWindow, VntPeerRow, VntServerRow};

/// 将默认未连接 VNT 状态应用到 Slint 属性。
pub(crate) fn apply_vnt_idle_to_ui(ui: &AppWindow, language: i32) {
    let snapshot = localized_vnt_idle_snapshot(language);
    ui.set_vnt_busy(snapshot.busy);
    ui.set_vnt_running(snapshot.running);
    ui.set_vnt_status_text(snapshot.status.into());
    ui.set_vnt_detail_text(snapshot.detail.into());
    ui.set_vnt_ip_text(snapshot.virtual_ip.into());
    ui.set_vnt_server_status_text(snapshot.server.into());
    ui.set_vnt_nat_text(snapshot.nat.into());
    ui.set_vnt_peer_summary_text(snapshot.peer_summary.into());
}

/// 在 VNT 运行前展示的默认服务器行。
pub(crate) fn vnt_server_placeholder_rows(language: i32) -> Vec<VntServerRow> {
    localized_vnt_idle_snapshot(language)
        .servers
        .into_iter()
        .map(vnt_server_to_row)
        .collect()
}

/// 在 VNT 运行前展示的默认节点行。
pub(crate) fn vnt_placeholder_rows(language: i32) -> Vec<VntPeerRow> {
    localized_vnt_idle_snapshot(language)
        .peers
        .into_iter()
        .map(vnt_peer_to_row)
        .collect()
}

/// 运行前的空闲快照由 UI 语言决定；运行中的 VNT 原生状态仍按核心返回展示。
pub(crate) fn localized_vnt_idle_snapshot(language: i32) -> vnt_platform::VntSnapshot {
    let mut snapshot = vnt_platform::idle_snapshot();
    snapshot.status =
        crate::app::i18n::tr(language, "未连接", "Disconnected", "未接続").to_string();
    snapshot.detail = crate::app::i18n::tr(
        language,
        "填写房间号后启动联机平台",
        "Enter a room code before starting",
        "ルーム番号を入力してから起動してください",
    )
    .to_string();
    snapshot.peer_summary =
        crate::app::i18n::tr(language, "0 个用户", "0 users", "0 ユーザー").to_string();
    if let Some(server) = snapshot.servers.first_mut() {
        server.name = crate::app::i18n::tr(
            language,
            "暂无服务器",
            "No servers yet",
            "サーバーはありません",
        )
        .to_string();
        server.address = crate::app::i18n::tr(
            language,
            "启动联机后会显示 VNT 服务器",
            "VNT servers appear after starting",
            "起動後に VNT サーバーが表示されます",
        )
        .to_string();
        server.detail = crate::app::i18n::tr(
            language,
            "VNT 原生核心未运行",
            "VNT native core is not running",
            "VNT ネイティブコアは未起動です",
        )
        .to_string();
    }
    if let Some(peer) = snapshot.peers.first_mut() {
        peer.name = crate::app::i18n::tr(
            language,
            "暂无在线用户",
            "No online users yet",
            "オンラインユーザーはありません",
        )
        .to_string();
        peer.detail = crate::app::i18n::tr(
            language,
            "启动联机后会显示同房间用户",
            "Users in the same room appear after starting",
            "起動後に同じルームのユーザーが表示されます",
        )
        .to_string();
    }
    snapshot
}

/// 将 VNT 节点快照映射为 Slint 行。
pub(crate) fn vnt_peer_to_row(peer: VntPeer) -> VntPeerRow {
    VntPeerRow {
        name: peer.name.into(),
        address: peer.address.into(),
        detail: peer.detail.into(),
        online: peer.online,
    }
}

/// 将 VNT 服务器快照映射为 Slint 行。
pub(crate) fn vnt_server_to_row(server: VntServer) -> VntServerRow {
    VntServerRow {
        name: server.name.into(),
        address: server.address.into(),
        detail: server.detail.into(),
        online: server.online,
    }
}
