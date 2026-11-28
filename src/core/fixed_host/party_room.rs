//! 一个票据独占一个房间。只通过父进程的管道报告状态，结束或 EOF 时回收自己的专服。
use super::*;
use std::ffi::OsString;
use std::io::{BufRead, BufReader};

pub(crate) fn run(mut args: impl Iterator<Item = OsString>) -> Result<()> {
    let target = PathBuf::from(args.next().context("缺少游戏目录")?);
    let mut next = || args.next().and_then(|s| s.into_string().ok()).context("房间参数不完整");
    let ticket = Uuid::parse_str(&next()?)?;
    let mode = next()?;
    let map = next()?;
    let port: u16 = next()?.parse()?;
    ensure!(args.next().is_none() && valid_room_port(port), "组队游戏端口无效");
    validate_win64_path(&target)?;
    let config = if mode == "Rush" && map == "Warehouse" {
        ("pvp", "", PVP_MODE.to_owned())
    } else {
        let difficulty = auto_room::party_difficulty(&super::super::process::launch_files(&target), &mode, &map)?;
        let bp = format!("BP_PBGameMode_{mode}");
        ("pve", difficulty, format!("[SERVER] Mode: /Game/Online/GameMode/{bp}.{bp}_C"))
    };
    let room = target.join(format!("BRT-Party-{ticket}"));
    fs::create_dir(&room).context("本票据已使用或无法建立房间")?;
    let _lock = room_lock(&room)?;
    ensure!(udp_owners(port)?.is_empty(), "组队游戏端口已占用");
    let mut settings = auto_room::config(config.1, &map, port);
    settings["mode"] = config.0.into();
    if config.0 == "pvp" { settings.as_object_mut().unwrap().remove("difficulty"); }
    File::create_new(room.join("serverconfig.json"))?.write_all(&serde_json::to_vec(&settings)?)?;
    ensure!(launch_configured(&target, port, &StartAttempt::default(), &room, &map, &config.2, None)?, "开局取消");
    let result = (|| -> Result<()> {
        let pin = pin_in_dirs(&target, port, std::slice::from_ref(&room))?;
        let stdin = std::io::stdin();
        let mut input = BufReader::new(stdin.lock());
        let mut output = std::io::stdout().lock();
        writeln!(output, "{}", serde_json::json!({"v":1,"ticket":ticket.to_string(),"mode":mode,"map":map,"port":port}))?;
        output.flush()?;
        loop {
            let mut line = String::new();
            if input.by_ref().take(16).read_line(&mut line)? == 0 || line.trim_end() == "stop" { break; }
            ensure!(line == "live\n" && pin(), "组队专服归属已失效");
            writeln!(output, "live")?;
            output.flush()?;
        }
        Ok(())
    })();
    // 此目录从未复用，一直持锁。其他进程占到同号端口也不属于本次专服。
    if let Some(owner) = saved_owner(&room)? {
        if is_owner(&owner, &target, &room) { kill_pids(&[owner.pid])?; }
        fs::remove_file(room.join(STATE))?;
    }
    result
}
