//! 旧社区客户端两条本地连接的只读归属核对。不会打开游戏内存，也不会向游戏发送指令。
use anyhow::{Context, Result, ensure};
use netstat2::{AddressFamilyFlags, ProtocolFlags, ProtocolSocketInfo, TcpState, get_sockets_info};
use std::ffi::OsString;
use std::net::Ipv4Addr;
use std::path::Path;
use sysinfo::{Pid, ProcessRefreshKind, ProcessesToUpdate, System, UpdateKind};
use winreg::{RegKey, enums::HKEY_CURRENT_USER};

pub(crate) fn active_owner() -> Option<String> {
    let account: u32 = RegKey::predef(HKEY_CURRENT_USER)
        .open_subkey(r"Software\Valve\Steam\ActiveProcess")
        .ok()?
        .get_value("ActiveUser")
        .ok()?;
    (account != 0).then(|| (76_561_197_960_265_728_u64 + u64::from(account)).to_string())
}

fn matching(expected: &Path, ports: &[(u16, u16)]) -> Result<Vec<usize>> {
    ensure!(
        expected.is_absolute() && (2..=9).contains(&ports.len()),
        "invalid request"
    );
    let expected = expected.canonicalize()?;
    let sockets = get_sockets_info(
        AddressFamilyFlags::IPV4 | AddressFamilyFlags::IPV6,
        ProtocolFlags::TCP,
    )?;
    let owner = |&(local, remote): &(u16, u16)| {
        let mut owner = None;
        for row in &sockets {
            let ProtocolSocketInfo::Tcp(tcp) = &row.protocol_socket_info else {
                continue;
            };
            if tcp.state != TcpState::Established
                || tcp.local_port != local
                || tcp.remote_port != remote
                || tcp.local_addr.to_canonical() != Ipv4Addr::LOCALHOST
                || tcp.remote_addr.to_canonical() != Ipv4Addr::LOCALHOST
            {
                continue;
            }
            let [pid] = row.associated_pids.as_slice() else {
                return None;
            };
            if *pid == 0 || owner.replace(*pid).is_some() {
                return None;
            }
        }
        owner
    };
    let pid = owner(&ports[0]).context("HTTP connection has no unique owner")?;
    let proc_id = Pid::from_u32(pid);
    let mut sys = System::new();
    sys.refresh_processes_specifics(
        ProcessesToUpdate::Some(&[proc_id]),
        true,
        ProcessRefreshKind::nothing().with_exe(UpdateKind::Always),
    );
    let image = sys
        .process(proc_id)
        .and_then(|p| p.exe())
        .context("missing process")?
        .canonicalize()?;
    ensure!(
        super::same_path(&image, &expected),
        "different client executable"
    );
    Ok(ports[1..]
        .iter()
        .enumerate()
        .filter_map(|(i, p)| {
            (p != &ports[0] && p.1 != ports[0].1 && owner(p) == Some(pid)).then_some(i)
        })
        .collect())
}

pub(crate) fn check(args: &[OsString]) -> Result<()> {
    ensure!(
        args.len() >= 5 && args.len() <= 19 && args.len() % 2 == 1,
        "invalid client check"
    );
    let image = Path::new(&args[0]);
    let ports = args[1..]
        .chunks_exact(2)
        .map(|p| -> Result<_> {
            let a: u16 = p[0].to_str().context("invalid port")?.parse()?;
            let b: u16 = p[1].to_str().context("invalid port")?.parse()?;
            ensure!(a != 0 && b != 0 && a != b, "invalid socket pair");
            Ok((a, b))
        })
        .collect::<Result<Vec<_>>>()?;
    let matches = matching(image, &ports)?;
    ensure!(!matches.is_empty(), "no matching game connection");
    let steam = active_owner().context("no active Steam account")?;
    println!("{}", serde_json::json!({"steam":steam,"matches":matches}));
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use std::os::windows::process::CommandExt;
    use std::{
        fs,
        net::{TcpListener, TcpStream},
        process::{Command, Stdio},
        time::Duration,
    };
    #[test]
    fn only_matches_established_connections_from_the_same_image_and_pid() -> Result<()> {
        let target = std::env::current_exe()?;
        let listen = || TcpListener::bind((Ipv4Addr::LOCALHOST, 0));
        let web = listen()?;
        let rpc = listen()?;
        let other = listen()?;
        let h = TcpStream::connect(web.local_addr()?)?;
        let _web = web.accept()?;
        let r = TcpStream::connect(rpc.local_addr()?)?;
        let _rpc = rpc.accept()?;
        let script = format!(
            "$s=[Net.Sockets.TcpClient]::new('127.0.0.1',{}); [void][Console]::ReadLine(); $s.Dispose()",
            other.local_addr()?.port()
        );
        let mut child = Command::new("powershell.exe")
            .args(["-NoProfile", "-NonInteractive", "-Command", &script])
            .creation_flags(0x08000000)
            .stdin(Stdio::piped())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()?;
        let checked = (|| -> Result<()> {
            other.set_nonblocking(true)?;
            let deadline = std::time::Instant::now() + Duration::from_secs(10);
            let (_peer, addr) = loop {
                match other.accept() {
                    Ok(value) => break value,
                    Err(e)
                        if e.kind() == std::io::ErrorKind::WouldBlock
                            && std::time::Instant::now() < deadline =>
                    {
                        std::thread::sleep(Duration::from_millis(50))
                    }
                    Err(e) => return Err(e.into()),
                }
            };
            let pair = |c: &TcpStream| -> Result<(u16, u16)> {
                Ok((c.local_addr()?.port(), c.peer_addr()?.port()))
            };
            let http = pair(&h)?;
            let own = pair(&r)?;
            let foreign = (addr.port(), other.local_addr()?.port());
            assert_eq!(matching(&target, &[http, foreign, own, http])?, vec![1]);
            assert!(matching(&target, &[foreign, own]).is_err());
            let wrong = fs::canonicalize(std::env::var_os("SystemRoot").context("Windows")?)?
                .join("System32/notepad.exe");
            assert!(matching(&wrong, &[http, own]).is_err());
            Ok(())
        })();
        if let Some(mut input) = child.stdin.take() {
            let _ = input.write_all(b"\n");
        }
        let _ = child.kill();
        let _ = child.wait();
        checked
    }
}
