use super::{hash, linked};
use anyhow::{Context, Result, ensure};
use std::{fs, io, os::windows::ffi::OsStrExt, path::{Path, PathBuf}};
use uuid::Uuid;
use windows::{Win32::Storage::FileSystem::{ReplaceFileW, REPLACE_FILE_FLAGS}, core::PCWSTR};

pub(super) const SHA: &str = "6982f63b4542429157b3a48f99bf2daa38cf7616d3dc47e1381817f221c1a30e";
const RETAIL: &str = "181c49ffb522b3eb01014c84fd9d3a2a5c0b66ae80a6a6addff4bdd6f8125843";
const VERIFIED_LIVE39: &str = "cf5ae98d7a5ba835b3f7f98eaeddad778079be98e3a46c3c546fdf6a4ecc9bb9";
const UPGRADABLE: &[&str] = &[
    "acaaaf17837ac418cf1350c586ca7c06c8d62f04348e0a9dc3e4d6c77d43e455", // 上游 V0.8.4
    "7bbb6bb2a8225fc71d8a8ac5deac0a4e1356e422afb7fcc7d0acc0f7957fc24c", // 上游 Stable
    "c1771cf3fdb95cd409778e8f4c46bea025032be4347f1dca071ac5476a94a61c",
    "fe0f849590ba95ac5263f146ec416152a4c669a980e2b6768694e310bae54e29",
    "e1a3918f88309602f590d24fb60e3f84667a87be278656f91eb3daffc1f2272b",
    "78c55c6cd01bf36b56c8ae6df9fe9920756f7e8d11fc689de1f6cbad75a5b828",
    "6c31aa3de0473e65125b2a54bf2740cc11c7c947cfb5d7ef21343b8a74a83d91",
    "1822116d8ccca904303ac9fda759e4ec90a36a97aca7076c335c6167dab4f2ad",
];

pub(super) fn validated_bundle(runtime: &Path, target: &Path) -> Result<PathBuf> {
    let file = runtime.join("components/gameplay/live39/Payload.dll");
    ensure!(hash(&file).context("未能读取随包玩法组件，请完整解压新版 BRT")? == SHA,
        "随包玩法组件校验失败，请重新完整解压");
    ensure!(hash(&target.join(crate::core::GAME_EXE))? == RETAIL,
        "本包支持 Steam Build 14530908，请核对《边境》安装版本");
    Ok(file)
}

pub(super) fn inspect(target: &Path) -> Result<Option<String>> {
    let dest = target.join("Payload.dll");
    let meta = match fs::symlink_metadata(&dest) {
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(None),
        other => other?,
    };
    ensure!(!linked(&meta) && meta.is_file(), "玩法组件路径无效");
    let h = hash(&dest)?;
    ensure!(h == SHA || h == VERIFIED_LIVE39 || UPGRADABLE.contains(&h.as_str()),
        "现有 Payload.dll 并非受支持的社区版本，已保留原文件");
    Ok(Some(h))
}

// 新装由 publish 创建文件；覆盖已识别旧版时让 Windows 原子替换并保留原件。
pub(super) fn finish(target: &Path, bundled: &Path, before: &Option<String>) -> Result<bool> {
    let dest = target.join("Payload.dll");
    let current = inspect(target)?.context("玩法组件缺失")?;
    if current == SHA || current == VERIFIED_LIVE39 { return Ok(false); }
    ensure!(before.as_deref() == Some(current.as_str()), "安装期间原组件被更改");
    ensure!(crate::core::process::runtime_image_pids(&[crate::core::GAME_EXE, "ProjectReboundServerLauncher.exe"]).is_empty(),
        "请先关闭游戏和专服，再点安装社区环境");
    let backups = target.join(".brt-payload-backups");
    if !backups.try_exists()? { fs::create_dir(&backups)?; }
    ensure!(!linked(&fs::symlink_metadata(&backups)?) && backups.is_dir(), "备份目录无效");
    let id = Uuid::new_v4();
    let dir = backups.join(format!("brt-install-{id}"));
    fs::create_dir(&dir)?;
    let backup = dir.join("Payload.dll");
    let temp = target.join(format!(".brt-install-{id}.tmp"));
    let result = (|| -> Result<()> {
        let mut out = fs::File::create_new(&temp)?;
        io::copy(&mut fs::File::open(bundled)?, &mut out)?;
        out.sync_all()?;
        drop(out);
        ensure!(hash(&temp)? == SHA && hash(&dest)? == current, "安装期间组件校验失败");
        let wide = |p: &Path| p.as_os_str().encode_wide().chain(Some(0)).collect::<Vec<u16>>();
        let (old, new, save) = (wide(&dest), wide(&temp), wide(&backup));
        unsafe { ReplaceFileW(PCWSTR(old.as_ptr()), PCWSTR(new.as_ptr()), PCWSTR(save.as_ptr()), REPLACE_FILE_FLAGS(0), None, None) }
            .context("无法替换玩法组件，请退出游戏后重试")?;
        ensure!(hash(&dest)? == SHA && hash(&backup)? == current, "安装回读失败，旧版备份位于 {}", dir.display());
        Ok(())
    })();
    let _ = fs::remove_file(&temp);
    if result.is_err() { let _ = fs::remove_dir(&dir); } // 仅移除空目录，保留任何备份。
    result?;
    Ok(true)
}
