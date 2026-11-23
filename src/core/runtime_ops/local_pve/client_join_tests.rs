use super::*;
use serde_json::json;
use std::{fs, process, thread};

struct Lobby(Child);
impl Drop for Lobby {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

#[test]
fn controlled_child_only_joins_on_live_commit_once_even_if_ack_is_lost() -> Result<()> {
    let fixture = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/src/core/runtime_ops/local_pve/join_fixture.cjs"
    );
    let rpc = TcpListener::bind((Ipv4Addr::LOCALHOST, 0))?;
    let log = std::env::temp_dir().join(format!("brt-join-{}", Uuid::new_v4()));
    let pipe = pipe_name();
    let rpc_port = rpc.local_addr()?.port();
    let mut lobby = Lobby(
        super::super::super::hidden_command("node")
            .env_remove("NODE_OPTIONS")
            .env_remove("NODE_PATH")
            .args([fixture, &format!("-pipe={pipe}"), &rpc_port.to_string()])
            .arg(&log)
            .spawn()?,
    );
    rpc.set_nonblocking(true)?;
    let deadline = Instant::now() + Duration::from_secs(8);
    let (_rpc, remote) = loop {
        if let Ok(pair) = rpc.accept() {
            break pair;
        }
        ensure!(
            Instant::now() < deadline && lobby.0.try_wait()?.is_none(),
            "fixture RPC timed out"
        );
        thread::sleep(Duration::from_millis(10));
    };
    while let Err(reason) = Bridge::probe(&pipe, &mut lobby.0) {
        ensure!(
            Instant::now() < deadline,
            "fixture ping timed out: {reason:#}"
        );
        thread::sleep(Duration::from_millis(20));
    }
    let mut wrong = Lobby(
        super::super::super::hidden_command("node")
            .args(["-e", "setTimeout(()=>{},10000)"])
            .spawn()?,
    );
    assert!(Peer::open(&pipe, &mut wrong.0).is_err());
    let mut bridge = Bridge::bind()?;
    let address = bridge.listener.local_addr()?;
    let ipc = bridge.advertisement()?;
    assert_eq!(ipc["nonce"].as_str().unwrap().len(), 64);
    let run = Uuid::new_v4().to_string();
    let request = Uuid::new_v4().to_string();
    let game_port = 23456;
    let base = json!({"v":1,"op":"offer","runId":run,"requestId":request,
        "clientPort":remote.port(),"rpcPort":rpc_port,"gamePort":game_port});
    for mode in [
        "spoof-node",
        "spoof-rpc",
        "other-run",
        "other-port",
        "extra-target",
        "cancel",
        "bad-nonce",
        "stale-lease",
        "success",
        "replay",
        "lost-ack",
        "lost-replay",
    ] {
        let mut offer = base.clone();
        match mode {
            "spoof-rpc" => offer["clientPort"] = 0.into(),
            "other-run" => offer["runId"] = "old".into(),
            "other-port" => offer["gamePort"] = 9.into(),
            "extra-target" => offer["ip"] = "127.0.0.1:9".into(),
            "lost-ack" | "lost-replay" => {
                offer["requestId"] = "af3234dc-fca8-4d74-8897-738505d1aaff".into()
            }
            _ => {}
        }
        let mut socket = TcpStream::connect(address)?;
        writeln!(socket, "{offer}")?;
        let handshake = matches!(
            mode,
            "cancel" | "bad-nonce" | "stale-lease" | "success" | "lost-ack"
        );
        if mode == "lost-ack" {
            fs::write(log.with_extension("silent"), [])?;
        }
        let sender = thread::spawn(move || -> Result<()> {
            if handshake {
                let response: Value = read_line(&mut socket)?;
                ensure!(response["op"] == "ready" && response["requestId"] == offer["requestId"]);
                if mode != "cancel" {
                    let nonce = if mode == "bad-nonce" {
                        Value::Null
                    } else {
                        response["nonce"].clone()
                    };
                    writeln!(
                        socket,
                        "{}",
                        json!({"op":"commit","requestId":offer["requestId"],"nonce":nonce})
                    )?;
                }
            } else {
                let mut data = [0; 1];
                socket.set_read_timeout(Some(Duration::from_secs(2)))?;
                let _ = socket.read(&mut data);
            }
            Ok(())
        });
        let node_pid = if mode == "spoof-node" {
            wrong.0.id()
        } else {
            process::id()
        };
        let res = bridge.dispatch(&run, &pipe, &mut lobby.0, node_pid, game_port, || {
            ensure!(mode != "stale-lease", "room replaced");
            Ok(())
        });
        sender.join().unwrap()?;
        assert_eq!(res.is_ok(), mode == "success", "{mode}: {res:?}");
    }
    let lines = fs::read_to_string(&log)?;
    let joins: Vec<_> = lines
        .lines()
        .filter(|line| line.starts_with("join"))
        .collect();
    assert_eq!(joins, [r#"join	{"ip":"127.0.0.1:23456"}"#; 2]);
    fs::remove_file(log.with_extension("silent"))?;
    fs::remove_file(log)?;
    Ok(())
}
