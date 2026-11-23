#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

//! 程序入口。具体 UI 控制逻辑放在 app 模块，核心安装逻辑放在 core 模块。

mod app;
mod core;
mod vnt_platform;

slint::include_modules!();

fn main() -> anyhow::Result<()> {
    let mut args = std::env::args_os().skip(1);
    if args.next().as_deref() == Some(std::ffi::OsStr::new("--brt-archive-owner")) {
        return core::local_profile::legacy_client::check(&args.collect::<Vec<_>>());
    }
    app::run()
}
