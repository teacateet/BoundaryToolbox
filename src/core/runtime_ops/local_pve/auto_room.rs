use super::{
    Binding, DIFFICULTIES, LaunchFiles, Plan, Result, menu_maps, read_map_table, udp_binding,
};
use anyhow::{Context, ensure};
use std::fs::{self, File};
use std::io::{ErrorKind, Write};
use std::os::windows::fs::MetadataExt;
use std::path::Path;

// 使用零售启动器识别的地图短名，大厅的 warehouse 行使用小写。
fn rotation(data: &[u8], mode: &str, turn: u64) -> Result<(&'static str, String, usize)> {
    let (index, &(difficulty, _)) = DIFFICULTIES
        .iter()
        .enumerate()
        .find(|(_, (_, id))| *id == mode)
        .context("原版大厅没有这档人机难度")?;
    let maps = menu_maps(data, mode)?;
    ensure!(
        maps.iter().all(|map| matches!(
            map.as_str(),
            "Warehouse" | "DataCenter" | "MiniFarm" | "CircularX" | "OSS"
        )),
        "人机表包含启动器未识别的地图"
    );
    let slot = (turn % maps.len() as u64) as usize;
    Ok((difficulty, maps[slot].clone(), index * 16 + slot))
}

pub(in crate::core) fn coop_choice(
    files: &LaunchFiles,
    turn: u64,
) -> Result<(&'static str, String)> {
    let data = read_map_table(files)?;
    let mut choices = Vec::new();
    for (_, mode) in DIFFICULTIES {
        for index in 0..menu_maps(&data, mode)?.len() {
            choices.push(rotation(&data, mode, index as u64)?);
        }
    }
    let (difficulty, map, _) = choices.swap_remove((turn % choices.len() as u64) as usize);
    Ok((difficulty, map))
}

pub(in crate::core::runtime_ops) fn selected_slot(files: &LaunchFiles, mode: &str, selected: &str) -> Result<u64> {
    menu_maps(&read_map_table(files)?, mode)?.iter().position(|m| m == selected)
        .map(|i| i as u64).context("下一局的地图与模式不兼容")
}

pub(in crate::core) fn party_difficulty(files: &LaunchFiles, mode: &str, selected: &str) -> Result<&'static str> {
    let data = read_map_table(files)?;
    let index = menu_maps(&data, mode)?.iter().position(|m| m == selected)
        .context("组队选择的地图与模式不兼容")?;
    Ok(rotation(&data, mode, index as u64)?.0)
}

pub(in crate::core) fn config(difficulty: &str, map: &str, port: u16) -> serde_json::Value {
    serde_json::json!({"mode":"pve", "difficulty":difficulty, "map":map, "offline":true,
        "port":port, "externalPort":port, "serverName":"BRT", "serverRegion":"LOCAL"})
}

fn room_dir(path: &Path) -> Result<()> {
    match fs::create_dir(path) {
        Ok(()) => (),
        Err(e) if e.kind() == ErrorKind::AlreadyExists => (),
        Err(e) => return Err(e.into()),
    }
    let meta = fs::symlink_metadata(path)?;
    ensure!(
        meta.is_dir() && meta.file_attributes() & 0x400 == 0,
        "自动房间目录无效"
    );
    Ok(())
}

pub(in crate::core::runtime_ops) fn stage(
    files: &LaunchFiles,
    mode: &str,
    turn: u64,
) -> Result<(LaunchFiles, Plan)> {
    let (difficulty, map, slot) = rotation(&read_map_table(files)?, mode, turn)?;
    let target = files.game_exe.parent().context("缺少游戏目录")?;
    for offset in 0..8 {
        let port = 19770 + slot as u16 + offset * 64;
        if udp_binding(port, None)? != Binding::Free {
            continue;
        }
        let dir = target.join(format!("BRT-Auto-{difficulty}-{map}-{port}"));
        room_dir(&dir)?;
        let path = dir.join("serverconfig.json");
        let expected = config(difficulty, &map, port);
        match File::create_new(&path) {
            Ok(mut f) => f.write_all(&serde_json::to_vec_pretty(&expected)?)?,
            Err(e) if e.kind() == ErrorKind::AlreadyExists => {
                let info = fs::symlink_metadata(&path)?;
                ensure!(
                    info.is_file() && info.file_attributes() & 0x400 == 0 && info.len() <= 8192,
                    "自动房间配置无效：{}",
                    path.display()
                );
                let saved: serde_json::Value = serde_json::from_slice(&fs::read(&path)?)?;
                ensure!(
                    saved == expected,
                    "自动房间配置与本次选择不同：{}",
                    path.display()
                );
            }
            Err(e) => return Err(e.into()),
        }
        let plan = Plan::read_dir(&dir)?;
        return Ok((files.clone(), plan));
    }
    anyhow::bail!("本机自动人机的游戏端口均被占用")
}

pub(in crate::core::runtime_ops) fn stage_managed(files: &LaunchFiles, mode: &str, turn: u64) -> Result<(LaunchFiles, Plan)> {
    let (files, mut plan) = stage(files, mode, turn)?;
    plan.use_local_archive_host(&files)?;
    Ok((files, plan))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn all_native_rows_rotate_in_order_for_each_difficulty() -> Result<()> {
        let mut rows = serde_json::Map::new();
        for (mode, maps) in [
            ("Rush_PVE_Easy", &["warehouse", "DataCenter"][..]),
            ("Rush_PVE_Normal", &["MiniFarm", "CircularX", "OSS"][..]),
            ("Rush_PVE_Hard", &["OSS", "CircularX"][..]),
        ] {
            rows.insert(mode.into(), json!({"MapsForMatch":maps.iter().map(|m|json!({"RowName":m})).collect::<Vec<_>>()}));
        }
        let data = serde_json::to_vec(&json!([{"Name":"DT_GameModeDefinition","Rows":rows}]))?;
        for (mode, expected) in [
            (
                "Rush_PVE_Easy",
                &["Warehouse", "DataCenter", "Warehouse"][..],
            ),
            (
                "Rush_PVE_Normal",
                &["MiniFarm", "CircularX", "OSS", "MiniFarm"][..],
            ),
            ("Rush_PVE_Hard", &["OSS", "CircularX", "OSS"][..]),
        ] {
            for (turn, map) in expected.iter().enumerate() {
                assert_eq!(rotation(&data, mode, turn as u64)?.1, *map);
            }
        }
        assert!(rotation(&data, "Rush_PVP", 0).is_err());
        Ok(())
    }
}
