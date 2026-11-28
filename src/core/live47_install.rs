use super::{hash, linked};
use anyhow::{Context, Result, ensure};
use std::{fs, io, os::windows::ffi::OsStrExt, path::{Path, PathBuf}};
use uuid::Uuid;
use windows::{Win32::Storage::FileSystem::{ReplaceFileW, REPLACE_FILE_FLAGS}, core::PCWSTR};

pub(super) const SHA: &str = "882b0d50e80de5adcd6009153c7924d044bae9359aa0129b1d9cc5cf7ffd7967";
const RETAIL: &str = "181c49ffb522b3eb01014c84fd9d3a2a5c0b66ae80a6a6addff4bdd6f8125843";
const UPGRADABLE: &[&str] = &[
    "99358bf3bf67d46095406ab72e0007b27d55529e6962f9b8eca37aa77d02222f", // 19.20.42 玩家包
    "f8758ce2bd068b1b4c239103e2617f3b2de4a26ea53a5cb31e4ccb7f9dca81cd", // live47 原始组件
    "aac377cf254d20793b165ba66213cc06caaf0dae300e22688b0c265fce9a3506", // 19.20.41 玩家包
    "df065ee7b9092c2a5bc2bfb886fd38ec9f36a0792592f5ae14c9e4ecf7c26b7d", // live46 原始玩家组件
    "38b24830120df106b636e7ce02163efdc1bd60ae9ab6134936ce515409685ac3", // live45 原始玩家组件
    "40780fd95da980868e15ba8f6af898206ea67f6d66561c3f29a7097ef7673104", // 19.20.40 玩家包
    "232147d406128a23c6fd4b7fc5092dbb69d0494a5d303834d185fc6ae9823fb1", // live44-radar 原始玩家构建，安装时统一为随包文件
    "dbacaf4a67fd953c90864df4b0205edb4c58b52784a9b028bc930a54017f6711", // 19.20.39 玩家包
    "4e295446c880834e62b295db98775cd03bb7be067cc17ae5b475812ee06dffd0", // live43 原始发布构建
    "d679ad4f972b8a974a3dafb2e23d72076b14bbfa25448e203f1f0e92a3899c05", // 19.20.38 玩家包
    "b4358e1e02d43e1e5c92643924f712f42eb30fdc3ced02d9d77eedc8e800fdac", // live42 原始发布构建
    "b6195bf1a68e6ffe005888baf22dd12f91c4c8938e69de48702160dcad4b2bc0", // 19.20.37 玩家包
    "3a354fb37987eac6b90020d19869fe075ec507d5c725612202f9d3989dc8a3e5", // live41 原始发布构建
    "0c18c5bfc9d923be2bf6162e68ddc70f014eb0a79e4eec7a37f91bef9b860941", // 19.20.36 玩家包
    "8cd64aa31e0ab2b200dd04893567a8a4641d98b8742bf0f454f549a310c056c0", // live40 原始发布构建
    "6982f63b4542429157b3a48f99bf2daa38cf7616d3dc47e1381817f221c1a30e", // 19.20.35 玩家包
    "cf5ae98d7a5ba835b3f7f98eaeddad778079be98e3a46c3c546fdf6a4ecc9bb9", // live39 原始发布构建
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
    let file = runtime.join("components/gameplay/live47/Payload.dll");
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
    ensure!(h == SHA || UPGRADABLE.contains(&h.as_str()),
        "现有 Payload.dll 并非受支持的社区版本，已保留原文件");
    Ok(Some(h))
}

// 新装由 publish 创建文件；覆盖已识别旧版时让 Windows 原子替换并保留原件。
pub(super) fn finish(target: &Path, bundled: &Path, before: &Option<String>) -> Result<bool> {
    let dest = target.join("Payload.dll");
    let current = inspect(target)?.context("玩法组件缺失")?;
    if current == SHA { return Ok(false); }
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
