#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

//! 程序入口。具体 UI 控制逻辑放在 app 模块，核心安装逻辑放在 core 模块。

mod app;
mod core;
mod steam_directory;
mod vnt_platform;

slint::include_modules!();

fn main() -> anyhow::Result<()> {
    let mut args = std::env::args_os().skip(1);
    let action = args.next();
    if action.as_deref() == Some(std::ffi::OsStr::new("--brt-party-room")) {
        return core::fixed_host::party_room::run(args);
    }
    if action.as_deref() == Some(std::ffi::OsStr::new("--brt-steam-rooms")) {
        return steam_directory::run(args);
    }
    if action.as_deref() == Some(std::ffi::OsStr::new("--install-community")) {
        let path = args.next().ok_or_else(|| anyhow::anyhow!("请指定 Steam 游戏的 Win64 目录"))?;
        anyhow::ensure!(args.next().is_none(), "安装只接受一个目录");
        let core = core::InstallerCore::new(std::sync::Arc::new(|_| {}))?;
        let msg = core.install_community(std::path::Path::new(&path),
            std::sync::Arc::new(|_| {}), core::InstallCancelToken::new())?;
        println!("{msg}");
        return Ok(());
    }
    if action.as_deref() == Some(std::ffi::OsStr::new("--brt-archive-owner")) {
        return core::local_profile::legacy_client::check(&args.collect::<Vec<_>>());
    }
    app::run()
}
