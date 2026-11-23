//! Windows 防火墙端口放行。

use anyhow::{Context, Result, bail};

use super::*;

impl InstallerCore {
    /// 放行 Boundary 社区服和联机需要的固定 TCP/UDP 端口。
    pub fn ensure_boundary_firewall_ports(&self) -> Result<String> {
        ensure_boundary_firewall_ports_impl()?;
        let tcp = join_ports(ONLINE_TCP_PORTS);
        let udp = join_ports(ONLINE_UDP_PORTS);
        let summary = format!("已放行联机端口：TCP {tcp} / UDP {udp}");
        self.log(summary.clone());
        Ok(summary)
    }
}

fn join_ports(ports: &[u16]) -> String {
    ports
        .iter()
        .map(u16::to_string)
        .collect::<Vec<_>>()
        .join(",")
}

#[cfg(windows)]
fn ensure_boundary_firewall_ports_impl() -> Result<()> {
    use std::os::windows::process::CommandExt;
    use std::process::{Command, Output};

    struct Rule {
        name: &'static str,
        dir: &'static str,
        protocol: &'static str,
        ports: String,
    }

    fn run_netsh(args: Vec<String>) -> Result<Output> {
        let mut command = Command::new("netsh");
        command.args(args);
        command.creation_flags(CREATE_NO_WINDOW);
        let output = command.output().context("执行 netsh 失败")?;
        if !output.status.success() {
            let stdout = String::from_utf8_lossy(&output.stdout);
            let stderr = String::from_utf8_lossy(&output.stderr);
            bail!("netsh 返回失败：{}{}", stdout.trim(), stderr.trim());
        }
        Ok(output)
    }

    fn reset_rule(rule: &Rule) -> Result<()> {
        let _ = run_netsh(vec![
            "advfirewall".to_string(),
            "firewall".to_string(),
            "delete".to_string(),
            "rule".to_string(),
            format!("name={}", rule.name),
        ]);

        run_netsh(vec![
            "advfirewall".to_string(),
            "firewall".to_string(),
            "add".to_string(),
            "rule".to_string(),
            format!("name={}", rule.name),
            format!("dir={}", rule.dir),
            "action=allow".to_string(),
            format!("protocol={}", rule.protocol),
            format!("localport={}", rule.ports),
            "profile=any".to_string(),
            "enable=yes".to_string(),
        ])
        .with_context(|| format!("添加防火墙规则失败：{}", rule.name))?;
        Ok(())
    }

    let tcp_ports = join_ports(ONLINE_TCP_PORTS);
    let udp_ports = join_ports(ONLINE_UDP_PORTS);
    let rules = [
        Rule {
            name: "Boundary Toolbox 联机 TCP 入站",
            dir: "in",
            protocol: "TCP",
            ports: tcp_ports.clone(),
        },
        Rule {
            name: "Boundary Toolbox 联机 TCP 出站",
            dir: "out",
            protocol: "TCP",
            ports: tcp_ports,
        },
        Rule {
            name: "Boundary Toolbox 联机 UDP 入站",
            dir: "in",
            protocol: "UDP",
            ports: udp_ports.clone(),
        },
        Rule {
            name: "Boundary Toolbox 联机 UDP 出站",
            dir: "out",
            protocol: "UDP",
            ports: udp_ports,
        },
    ];

    for rule in &rules {
        reset_rule(rule)?;
    }

    Ok(())
}

#[cfg(not(windows))]
fn ensure_boundary_firewall_ports_impl() -> Result<()> {
    bail!("当前平台不支持自动开放 Windows 防火墙端口")
}
