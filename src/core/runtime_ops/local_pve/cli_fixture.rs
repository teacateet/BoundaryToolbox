//! 只由隔离测试编译的命令行夹具，不加载游戏。

use std::fs;
use std::os::windows::process::CommandExt;
use std::process::Command;
use std::time::{Duration, Instant};

fn main() {
    assert_eq!(std::env::args().nth(1).as_deref(), Some("-cli"));
    let port: u16 = fs::read_to_string("test-port").unwrap().parse().unwrap();
    let mut game = Command::new(fs::read_to_string("test-node").unwrap())
        .creation_flags(0x08000000) // CREATE_NO_WINDOW
        .env_remove("NODE_OPTIONS")
        .env_remove("NODE_PATH")
        .arg("--no-global-search-paths")
        .arg(fs::read_to_string("test-script").unwrap())
        .arg("game")
        .arg(port.to_string())
        .spawn()
        .unwrap();
    let limit = Instant::now() + Duration::from_secs(8);
    while !std::path::Path::new("listening.json").exists() {
        assert!(Instant::now() < limit && game.try_wait().unwrap().is_none());
        std::thread::sleep(Duration::from_millis(20));
    }
    fs::create_dir_all("logs").unwrap();
    if let Ok(ms) = fs::read_to_string("test-delay") {
        std::thread::sleep(Duration::from_millis(ms.parse().unwrap()));
    }
    let class = "BP_PBGameMode_Rush_PVE_Normal";
    let map = fs::read_to_string("test-map").unwrap_or_else(|_| "CircularX".into());
    let log = format!(
        "[Launcher] Server launched. PID = {}\n[SERVER] Map loaded: {map}\n[SERVER] Mode: /Game/Online/GameMode/{class}.{class}_C\n[SERVER] Port: {port}\n[SERVER] Server is now listening.\n",
        game.id()
    );
    fs::write("logs/log-fixture.txt", log).unwrap();
    game.wait().unwrap();
}
