use std::env;
use std::fs::File;
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::process::Stdio;

use anyhow::{Context, Result, ensure};
use serde_json::{Value, json};

const CREATE_NO_WINDOW: u32 = 0x0800_0000;
const INSTALLER_FOLDER_NAME: &str = "orbital_profile";

struct LaunchFiles {
    server_dir: PathBuf,
    node_exe: PathBuf,
    game_exe: PathBuf,
}

mod core;
use core::local_profile;

fn emit(value: Value) -> Result<()> {
    println!("{value}");
    io::stdout().flush()?;
    Ok(())
}

fn main() -> Result<()> {
    let args: Vec<_> = env::args().skip(1).collect();
    if args.first().is_some_and(|s| s == "--brt-archive-owner") {
        return local_profile::legacy_client::check(&env::args_os().skip(2).collect::<Vec<_>>());
    }
    ensure!(
        matches!(args.len(), 3 | 4 | 6),
        "Expected HOME SERVER NODE [PORT | HTTP RPC MM]"
    );
    let home = Path::new(&args[0]);
    let files = LaunchFiles {
        server_dir: PathBuf::from(&args[1]),
        node_exe: PathBuf::from(&args[2]),
        game_exe: Path::new(&args[1]).parent().context("Missing game directory")?.join("ProjectBoundarySteam-Win64-Shipping.exe"),
    };
    if args.len() == 4 {
        let pid = if args[3] == "default" {
            local_profile::running_managed_pid(home, &files)?
        } else {
            core::running_managed_pid_at(home, &files, args[3].parse()?)?
        };
        return emit(json!({ "pid": pid }));
    }
    match local_profile::prepare(home, &files)? {
        local_profile::ProfilePlan::Unsupported { reason } => {
            emit(json!({ "kind": "unsupported", "reason": reason }))
        }
        local_profile::ProfilePlan::Managed(profile) => {
            let mut reply = json!({ "kind": "managed", "appDir": profile.app_dir, "loadoutDir": profile.loadout_dir });
            if args.len() == 3 {
                return emit(reply);
            }
            let log = File::create(home.join("fixture.log"))?;
            let mut service = profile
                .command(home)?
                .env("PORT", &args[3])
                .env("RPC_PORT", &args[4])
                .env("MATCHMAKING_PORT", &args[5])
                .stdout(Stdio::from(log.try_clone()?))
                .stderr(Stdio::from(log))
                .spawn()
                .context("Starting managed profile")?;
            reply["pid"] = json!(service.id());
            let result = (|| -> Result<()> {
                emit(reply)?;
                io::stdin().read_line(&mut String::new())?;
                Ok(())
            })();
            if service.try_wait()?.is_none() {
                service.kill()?;
            }
            service.wait()?;
            result
        }
    }
}
