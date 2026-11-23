use std::net::{Ipv4Addr, SocketAddrV4};
use vnt_ipc::port_mapping::PortMapping;

#[cfg(test)]
#[path = "vnt_room_smoke.rs"]
mod smoke;

pub(crate) const LOCAL_GAME_ENTRY: SocketAddrV4 = SocketAddrV4::new(Ipv4Addr::LOCALHOST, 7777);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum RoomError {
    NotReady,
    InvalidPeer,
    OwnPeer,
    OfflinePeer,
    InvalidPort,
    WrongMapping,
}

fn node_ip(text: &str) -> Option<Ipv4Addr> {
    let ip = text.trim().parse::<Ipv4Addr>().ok()?;
    let first = ip.octets()[0];
    (first != 0 && first != 127 && first < 224).then_some(ip)
}

pub(crate) fn own_ip(text: &str) -> Option<Ipv4Addr> {
    let address = match text.trim().split_once('/') {
        Some((ip, prefix))
            if prefix.bytes().all(|b| b.is_ascii_digit()) && prefix.parse::<u8>().ok()? <= 32 =>
        {
            ip
        }
        Some(_) => return None,
        None => text,
    };
    node_ip(address)
}

pub(crate) fn select_peer(
    own: &str,
    peer: &str,
    port: &str,
    online: impl FnOnce(Ipv4Addr) -> bool,
) -> Result<SocketAddrV4, RoomError> {
    let own = own_ip(own).ok_or(RoomError::NotReady)?;
    let peer = node_ip(peer).ok_or(RoomError::InvalidPeer)?;
    if peer == own {
        return Err(RoomError::OwnPeer);
    }
    if !online(peer) {
        return Err(RoomError::OfflinePeer);
    }
    let port = port.trim();
    if !port.bytes().all(|b| b.is_ascii_digit()) {
        return Err(RoomError::InvalidPort);
    }
    let port = port
        .parse::<u16>()
        .ok()
        .filter(|p| *p >= 1024 && ![6969, 8000, 9000].contains(p))
        .ok_or(RoomError::InvalidPort)?;
    Ok(SocketAddrV4::new(peer, port))
}

pub(crate) fn game_target(
    peer: SocketAddrV4,
    no_tun: bool,
    mapped_peer: Option<SocketAddrV4>,
) -> Result<SocketAddrV4, RoomError> {
    if !no_tun {
        return Ok(peer);
    }
    // 回环只用作已映射到本次选中房主和端口的入口。
    if mapped_peer == Some(peer) {
        Ok(LOCAL_GAME_ENTRY)
    } else {
        Err(RoomError::WrongMapping)
    }
}

pub(crate) fn guest_mapping(
    no_tun: bool,
    host: Option<SocketAddrV4>,
) -> Result<Vec<PortMapping>, String> {
    let Some(peer) = host else {
        return Ok(Vec::new());
    };
    if !no_tun {
        return Err("Game port forwarding requires no-adapter mode".into());
    }
    // 第一段是房客本地入口；虚拟目标及最后的游戏地址均属于房主。
    let mapping = format!("udp://{LOCAL_GAME_ENTRY}-{}-{peer}", peer.ip());
    Ok(vec![mapping.parse::<PortMapping>()?])
}

#[cfg(test)]
mod tests {
    use super::*;

    const OWN: &str = "10.26.0.3/24";
    const HOST: &str = "10.26.0.8";

    fn selected(port: &str) -> Result<SocketAddrV4, RoomError> {
        select_peer(OWN, HOST, port, |ip| ip.to_string() == HOST)
    }

    #[test]
    fn vnt_room_adapter_passes_default_and_selected_host_port() {
        for port in ["7777", "18777", "1024", "65535"] {
            let peer = selected(port).unwrap();
            assert_eq!(
                game_target(peer, false, None).unwrap().to_string(),
                format!("{HOST}:{port}")
            );
        }
        assert_eq!(selected(" 18777 "), selected("18777"));
    }

    #[test]
    fn vnt_room_rejects_invalid_and_reserved_fixed_room_ports() {
        for port in [
            "",
            " ",
            "0",
            "1023",
            "6969",
            "8000",
            "9000",
            "65536",
            "+7777",
            "-1",
            "1.5",
            "１２３４",
            "10.26.0.8:18777",
            "7 777",
        ] {
            assert_eq!(selected(port), Err(RoomError::InvalidPort), "{port:?}");
        }
    }

    #[test]
    fn vnt_room_rejects_self_stale_and_unusable_addresses() {
        for own in [OWN, "10.26.0.3", "10.26.0.3/32"] {
            assert_eq!(
                select_peer(own, "10.26.0.3", "7777", |_| true),
                Err(RoomError::OwnPeer)
            );
        }
        for own in ["", "-", "10.26.0.3/33", "10.26.0.3/24/8", "127.0.0.1"] {
            assert_eq!(
                select_peer(own, HOST, "7777", |_| true),
                Err(RoomError::NotReady)
            );
        }
        for address in [
            "",
            "localhost",
            "127.0.0.2",
            "0.0.0.0",
            "224.0.0.1",
            "255.255.255.255",
            "::1",
            "10.26.0.8:18777",
        ] {
            assert_eq!(
                select_peer(OWN, address, "7777", |_| true),
                Err(RoomError::InvalidPeer)
            );
        }
        assert_eq!(
            select_peer(OWN, HOST, "18777", |_| false),
            Err(RoomError::OfflinePeer)
        );
    }

    #[test]
    fn vnt_room_no_adapter_requires_the_exact_live_mapping() {
        let peer = selected("18777").unwrap();
        for mapping in [
            None,
            Some(selected("7777").unwrap()),
            Some("10.26.0.9:18777".parse().unwrap()),
        ] {
            assert_eq!(
                game_target(peer, true, mapping),
                Err(RoomError::WrongMapping)
            );
        }
        for remote in [peer, selected("7777").unwrap()] {
            assert_eq!(
                game_target(remote, true, Some(remote)).unwrap().to_string(),
                "127.0.0.1:7777"
            );
        }
    }

    #[test]
    fn vnt_room_vendor_udp_config_matches_the_game_launch_target() {
        for port in ["7777", "18777", "65535"] {
            let host = selected(port).unwrap();
            let [mapping]: [PortMapping; 1] =
                guest_mapping(true, Some(host)).unwrap().try_into().unwrap();
            assert_eq!(
                mapping.src_addr,
                game_target(host, true, Some(host)).unwrap().into()
            );
            assert_eq!(mapping.virtual_target_ip, *host.ip());
            assert_eq!(mapping.dst_host, host.ip().to_string());
            assert_eq!(mapping.dst_port, host.port());
            assert_eq!(
                mapping.to_string(),
                format!("udp://127.0.0.1:7777-10.26.0.8-10.26.0.8:{port}")
            );
            assert!(guest_mapping(false, Some(host)).is_err());
        }
        assert!(guest_mapping(false, None).unwrap().is_empty());
        assert!(guest_mapping(true, None).unwrap().is_empty());
    }
}
