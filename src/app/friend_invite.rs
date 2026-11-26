use std::net::{IpAddr, Ipv4Addr, SocketAddrV4, UdpSocket};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) enum Invite {
    Lan(SocketAddrV4),
    Vnt(SocketAddrV4),
}

fn lan_ip(ip: Ipv4Addr) -> bool {
    ip.is_private() || ip.is_link_local()
}

impl Invite {
    pub(super) fn new(vnt: bool, ip: Ipv4Addr, port: u16) -> Option<Self> {
        let first = ip.octets()[0];
        if first == 0
            || first == 127
            || first >= 224
            || (!vnt && !lan_ip(ip))
            || !crate::core::fixed_host::valid_room_port(port)
        {
            return None;
        }
        let peer = SocketAddrV4::new(ip, port);
        Some(if vnt {
            Self::Vnt(peer)
        } else {
            Self::Lan(peer)
        })
    }

    pub(super) fn parse(value: &str) -> Option<Self> {
        let s = value.trim();
        if s.len() > 80 {
            return None;
        }
        let (vnt, addr) = if let Some(s) = s.strip_prefix("brt://lan/") {
            (false, s)
        } else {
            (true, s.strip_prefix("brt://vnt/")?)
        };
        let (ip, port) = addr.split_once(':')?;
        if !port.bytes().all(|c| c.is_ascii_digit()) {
            return None;
        }
        Self::new(vnt, ip.parse().ok()?, port.parse().ok()?)
    }
}

impl std::fmt::Display for Invite {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Lan(peer) => write!(f, "brt://lan/{peer}"),
            Self::Vnt(peer) => write!(f, "brt://vnt/{peer}"),
        }
    }
}

fn choose_lan(
    preferred: Option<Ipv4Addr>,
    mut addresses: Vec<Ipv4Addr>,
    virtual_ip: Option<Ipv4Addr>,
) -> Option<Ipv4Addr> {
    addresses.retain(|ip| lan_ip(*ip) && Some(*ip) != virtual_ip);
    addresses.sort_unstable();
    addresses.dedup();
    preferred
        .filter(|ip| addresses.contains(ip))
        .or_else(|| (addresses.len() == 1).then(|| addresses[0]))
}

pub(super) fn local_lan_ip(virtual_ip: Option<Ipv4Addr>) -> Option<Ipv4Addr> {
    // UDP connect 只查询 Windows 选择的出口，没有发送数据，也不开放监听端口。
    let preferred = UdpSocket::bind((Ipv4Addr::UNSPECIFIED, 0))
        .ok()
        .and_then(|s| {
            s.connect((Ipv4Addr::new(192, 0, 2, 1), 9)).ok()?;
            match s.local_addr().ok()?.ip() {
                IpAddr::V4(ip) => Some(ip),
                _ => None,
            }
        });
    let interfaces = sysinfo::Networks::new_with_refreshed_list();
    choose_lan(
        preferred,
        interfaces
            .values()
            .flat_map(|nic| nic.ip_networks())
            .filter_map(|net| {
                if let IpAddr::V4(ip) = net.addr {
                    Some(ip)
                } else {
                    None
                }
            })
            .collect(),
        virtual_ip,
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn invites_carry_only_transport_host_and_actual_game_port() {
        for s in ["brt://lan/192.168.2.3:18888", "brt://vnt/10.26.0.8:65535"] {
            assert_eq!(Invite::parse(s).unwrap().to_string(), s);
        }
        for s in [
            "brt://lan/127.0.0.1:7777",
            "brt://vnt/127.0.0.2:7777",
            "brt://vnt/0.2.3.4:7777",
            "brt://lan/8.8.8.8:7777",
            "brt://vnt/224.1.2.3:7777",
            "brt://lan/10.1.2.3:6969",
            "brt://lan/10.1.2.3:65536",
            "brt://lan/10.1.2.3:0",
            "brt://vnt/10.1.2.3:+7777",
            "brt://vnt/10.1.2.3:7777?password=example",
            "brt://vnt/name@10.1.2.3:7777",
            "https://host.example/7777",
            "brt://vnt/10.2.3.4",
            "brt://lan/10.1.2.3:7777\nrun",
        ] {
            assert!(Invite::parse(s).is_none(), "{s}");
        }
    }
    #[test]
    fn lan_address_uses_the_os_route_or_a_single_private_interface() {
        let a = Ipv4Addr::new(192, 168, 1, 23);
        let b = Ipv4Addr::new(10, 0, 0, 20);
        assert_eq!(choose_lan(Some(b), vec![a, b], None), Some(b));
        assert_eq!(choose_lan(Some(b), vec![a, b], Some(b)), Some(a));
        assert_eq!(
            choose_lan(
                Some(Ipv4Addr::LOCALHOST),
                vec![a, a, Ipv4Addr::LOCALHOST],
                None
            ),
            Some(a)
        );
        assert_eq!(choose_lan(None, vec![a, b], None), None);
        assert_eq!(choose_lan(None, vec![Ipv4Addr::LOCALHOST], None), None);
    }
}
