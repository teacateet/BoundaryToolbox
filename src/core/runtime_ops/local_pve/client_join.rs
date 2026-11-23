//! 仅与本次启动的大厅进程通讯。管道端点和游戏目标都由 Rust 决定。
use super::{Duration, File, HANDLE, OpenOptions, Result, Uuid, ensure};
use anyhow::Context;
use netstat2::{AddressFamilyFlags, ProtocolFlags, ProtocolSocketInfo, TcpState, get_sockets_info};
use serde::Deserialize;
use serde_json::Value;
use std::collections::HashSet;
use std::io::{ErrorKind, Read, Write};
use std::net::{Ipv4Addr, TcpListener, TcpStream};
use std::os::windows::fs::OpenOptionsExt;
use std::os::windows::io::AsRawHandle;
use std::process::Child;
use std::time::Instant;
use windows::Win32::Security::Cryptography::{BCRYPT_USE_SYSTEM_PREFERRED_RNG, BCryptGenRandom};
use windows::Win32::Storage::FileSystem::{SECURITY_ANONYMOUS, SECURITY_SQOS_PRESENT};
use windows::Win32::System::Pipes::{
    GetNamedPipeServerProcessId, PIPE_NOWAIT, PeekNamedPipe, SetNamedPipeHandleState,
};

const IO_TIMEOUT: Duration = Duration::from_millis(400);

pub(super) fn pipe_name() -> String {
    format!("brt_pve_{}", Uuid::new_v4().simple())
}

struct Peer(File);

impl Peer {
    fn open(name: &str, child: &mut Child) -> Result<Self> {
        ensure!(child.try_wait()?.is_none(), "大厅已退出");
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .security_qos_flags((SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS).0)
            .open(format!(r"\\.\pipe\{name}"))?;
        let handle = HANDLE(file.as_raw_handle());
        let mut owner = 0;
        unsafe {
            GetNamedPipeServerProcessId(handle, &mut owner)?;
        }
        ensure!(
            owner == child.id() && child.try_wait()?.is_none(),
            "命名管道不属于本次大厅"
        );
        unsafe {
            SetNamedPipeHandleState(handle, Some(&PIPE_NOWAIT), None, None)?;
        }
        Ok(Self(file))
    }

    fn send(&mut self, frame: &[u8]) -> Result<()> {
        ensure!(self.0.write(frame)? == frame.len(), "管道未完整接收命令");
        Ok(())
    }

    fn reply(&mut self, expected: &str) -> Result<Value> {
        let until = Instant::now() + IO_TIMEOUT;
        let mut bytes = Vec::new();
        let mut buf = [0; 512];
        loop {
            ensure!(Instant::now() < until, "等待管道响应超时");
            let mut available = 0;
            unsafe {
                PeekNamedPipe(
                    HANDLE(self.0.as_raw_handle()),
                    None,
                    0,
                    None,
                    Some(&mut available),
                    None,
                )?;
            }
            if available == 0 {
                std::thread::sleep(Duration::from_millis(5));
                continue;
            }
            match self.0.read(&mut buf) {
                Ok(n) if n > 0 => bytes.extend_from_slice(&buf[..n]),
                Ok(_) => anyhow::bail!("管道已关闭"),
                Err(e) if e.raw_os_error() == Some(232) || e.kind() == ErrorKind::WouldBlock => {
                    std::thread::sleep(Duration::from_millis(5));
                    continue;
                }
                Err(e) => return Err(e.into()),
            }
            ensure!(bytes.len() <= 4096, "管道响应过长");
            if bytes.contains(&b'\n') {
                let line = std::str::from_utf8(&bytes)?
                    .strip_suffix('\n')
                    .context("管道响应有额外数据")?
                    .trim_end_matches('\r');
                let (cmd, json) = line.split_once('\t').context("管道响应缺少参数")?;
                ensure!(cmd == expected, "管道响应不符");
                let data: Value = serde_json::from_str(json)?;
                ensure!(data.is_object(), "管道响应参数无效");
                return Ok(data);
            }
        }
    }

    fn ping(&mut self) -> Result<()> {
        self.send(b"ping\t{}\n")?;
        self.reply("pong")?;
        Ok(())
    }
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct Offer {
    v: u8,
    op: String,
    run_id: String,
    request_id: String,
    client_port: u16,
    rpc_port: u16,
    game_port: u16,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct Commit {
    op: String,
    request_id: String,
    nonce: String,
}

pub(super) fn read_line<T: serde::de::DeserializeOwned>(s: &mut TcpStream) -> Result<T> {
    let deadline = Instant::now() + Duration::from_secs(1);
    let mut bytes = Vec::new();
    let mut buf = [0; 512];
    loop {
        let remaining = deadline.saturating_duration_since(Instant::now());
        ensure!(!remaining.is_zero(), "等待本机交接超时");
        s.set_read_timeout(Some(remaining))?;
        let n = s.read(&mut buf)?;
        ensure!(n > 0 && bytes.len() + n <= 1024, "本机交接帧无效");
        bytes.extend_from_slice(&buf[..n]);
        if bytes.contains(&b'\n') {
            ensure!(
                bytes.iter().position(|c| *c == b'\n') == Some(bytes.len() - 1),
                "本机交接有多余数据"
            );
            return Ok(serde_json::from_slice(&bytes)?);
        }
    }
}

pub(super) fn owners(pairs: &[(u16, u16, u32)]) -> Result<()> {
    let rows = get_sockets_info(
        AddressFamilyFlags::IPV4 | AddressFamilyFlags::IPV6,
        ProtocolFlags::TCP,
    )?;
    for &(local, remote, pid) in pairs {
        let mut found = false;
        for row in &rows {
            let ProtocolSocketInfo::Tcp(tcp) = &row.protocol_socket_info else {
                continue;
            };
            if tcp.state == TcpState::Established
                && tcp.local_port == local
                && tcp.remote_port == remote
                && tcp.local_addr.to_canonical() == Ipv4Addr::LOCALHOST
                && tcp.remote_addr.to_canonical() == Ipv4Addr::LOCALHOST
            {
                ensure!(
                    row.associated_pids.as_slice() == [pid],
                    "本机 TCP 连接归属不符"
                );
                found = true;
            }
        }
        ensure!(found, "已登录的本机 TCP 连接不存在");
    }
    Ok(())
}

pub(super) struct Bridge {
    listener: TcpListener,
    nonce: String,
    consumed: HashSet<Uuid>,
    rpc_binding: Option<(u16, u16)>,
}

impl Bridge {
    pub(super) fn bind() -> Result<Self> {
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0))?;
        listener.set_nonblocking(true)?;
        let mut bytes = [0u8; 32];
        unsafe {
            BCryptGenRandom(None, &mut bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG).ok()?;
        }
        let nonce = bytes.iter().map(|b| format!("{b:02x}")).collect();
        Ok(Self {
            listener,
            nonce,
            consumed: HashSet::new(),
            rpc_binding: None,
        })
    }

    pub(super) fn advertisement(&self) -> Result<Value> {
        Ok(
            serde_json::json!({"v":1, "port":self.listener.local_addr()?.port(), "nonce":self.nonce}),
        )
    }

    pub(super) fn bind_rpc(&mut self, client: u16, node: u16) {
        self.rpc_binding = Some((client, node));
    }

    pub(super) fn accept_node(&self, node: u32) -> Result<Option<TcpStream>> {
        let (socket, remote) = match self.listener.accept() {
            Err(e) if e.kind() == ErrorKind::WouldBlock => return Ok(None),
            result => result?,
        };
        ensure!(remote.ip() == Ipv4Addr::LOCALHOST, "无效的回环对端");
        owners(&[(remote.port(), socket.local_addr()?.port(), node)])?;
        socket.set_nonblocking(false)?;
        Ok(Some(socket))
    }

    pub(super) fn probe(name: &str, child: &mut Child) -> Result<()> {
        Peer::open(name, child)?.ping()
    }

    pub(super) fn dispatch(
        &mut self,
        run: &str,
        name: &str,
        child: &mut Child,
        node: u32,
        port: u16,
        verify: impl FnOnce() -> Result<()>,
    ) -> Result<bool> {
        let Some(mut s) = self.accept_node(node)? else {
            return Ok(false);
        };
        let offer: Offer = read_line(&mut s)?;
        let id = Uuid::parse_str(&offer.request_id)?;
        ensure!(
            offer.v == 1
                && offer.op == "offer"
                && offer.run_id == run
                && offer.game_port == port
                && self
                    .rpc_binding
                    .is_none_or(|ports| ports == (offer.client_port, offer.rpc_port))
                && id.get_version_num() == 4
                && id.to_string() == offer.request_id
                && !self.consumed.contains(&id)
                && (self.rpc_binding.is_none() || self.consumed.is_empty())
                && self.consumed.len() < 4096,
            "交接代次、端口或请求无效"
        );
        let rpc = [
            (offer.client_port, offer.rpc_port, child.id()),
            (offer.rpc_port, offer.client_port, node),
        ];
        owners(&rpc)?;
        let mut peer = Peer::open(name, child)?;
        peer.ping()?;
        s.set_write_timeout(Some(Duration::from_secs(1)))?;
        writeln!(
            s,
            "{}",
            serde_json::json!({"op":"ready","requestId":offer.request_id,"nonce":self.nonce})
        )?;
        let commit: Commit = read_line(&mut s)?;
        ensure!(
            commit.op == "commit"
                && commit.request_id == offer.request_id
                && commit.nonce == self.nonce,
            "交接确认无效"
        );
        verify()?;
        owners(&rpc)?;
        ensure!(child.try_wait()?.is_none(), "大厅已退出");
        // 已提交的请求即使丢失 ACK 也不能重发。
        self.consumed.insert(id);
        peer.send(format!("join\t{{\"ip\":\"127.0.0.1:{port}\"}}\n").as_bytes())?;
        let reply = peer.reply("join_ack")?;
        ensure!(
            matches!(reply["status"].as_str(), Some("ok" | "accepted")),
            "大厅拒绝了房间"
        );
        Ok(true)
    }
}

#[cfg(test)]
#[path = "client_join_tests.rs"]
mod tests;
