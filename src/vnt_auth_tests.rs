use super::*;
use rustls::{ClientConnection, ServerConnection};
use vnt_ipc::{
    context::config::GameUdpShare,
    tls::{HostIdentity, RoomIdentity, room_tls},
};

// 内存中的标准 TLS 客户端与服务端，不接触任何本机服务。
fn handshake(server: rustls::ServerConfig, client: rustls::ClientConfig) -> Result<rustls::pki_types::CertificateDer<'static>> {
    let mut server = ServerConnection::new(Arc::new(server))?;
    let mut client = ClientConnection::new(Arc::new(client), "brt-room".try_into()?)?;
    for _ in 0..12 {
        let mut buf = Vec::new();
        client.write_tls(&mut buf)?;
        if !buf.is_empty() {
            server.read_tls(&mut buf.as_slice())?;
            server.process_new_packets()?;
        }
        buf.clear();
        server.write_tls(&mut buf)?;
        if !buf.is_empty() {
            client.read_tls(&mut buf.as_slice())?;
            client.process_new_packets()?;
        }
        if !server.is_handshaking() && !client.is_handshaking() {
            return Ok(client.peer_certificates().context("缺少已验证的房主证书")?[0].clone());
        }
    }
    bail!("握手未完成")
}
#[test]
fn room_requires_both_the_host_identity_and_member_credential() -> Result<()> {
    let host = HostIdentity::generate()?;
    let server = || room_tls("room-one", Some(RoomIdentity::Host(host.clone()))).map(|v| v.0);
    let client = |password, pin| room_tls(password, Some(RoomIdentity::Guest(pin))).map(|v| v.1);
    let cert=handshake(server()?, client("room-one", host.fingerprint())?)?;
    // 直接验证本地负例：即使证书正确，无效的签名仍必须被两个回调拒绝。
    use rustls::{internal::msgs::codec::Codec,client::danger::ServerCertVerifier};
    let mut fixture=vec![8,7,0,64];fixture.extend([0;64]);
    let sig=rustls::DigitallySignedStruct::read_bytes(&fixture).map_err(|_|anyhow!("测试签名格式错误"))?;
    let verifier=vnt_ipc::tls::verifier::FingerprintVerifier::new(host.fingerprint());
    assert!(verifier.verify_tls12_signature(b"fixture",&cert,&sig).is_err());
    assert!(verifier.verify_tls13_signature(b"fixture",&cert,&sig).is_err());
    assert!(handshake(server()?, client("room-two", host.fingerprint())?).is_err());
    assert!(
        handshake(
            server()?,
            client("room-one", HostIdentity::generate()?.fingerprint())?
        )
        .is_err()
    );
    let anonymous = rustls::ClientConfig::builder()
        .dangerous()
        .with_custom_certificate_verifier(Arc::new(
            vnt_ipc::tls::verifier::FingerprintVerifier::new(host.fingerprint()),
        ))
        .with_no_client_auth();
    assert!(handshake(server()?, anonymous).is_err());
    assert!(
        CertValidationMode::InsecureSkipVerification
            .build_verifier()
            .is_err()
    );
    Ok(())
}
#[test]
fn revoke_waits_for_a_committed_write_and_old_grants_never_revive() -> Result<()> {
    use std::sync::mpsc;
    let share = GameUdpShare::new(None);
    let a = share.grant_owned(18777, Arc::new(|| true));
    let (inside, active) = mpsc::channel();
    let (release, wait) = mpsc::channel();
    let worker = share.clone();
    let writer = thread::spawn(move || {
        worker.while_granted(a, 18777, || {
            inside.send(()).unwrap();
            wait.recv().unwrap()
        })
    });
    active.recv_timeout(Duration::from_secs(2))?;
    let guard = share.clone();
    let (done, revoked) = mpsc::channel();
    let (starting, start) = mpsc::channel();
    let revoker = thread::spawn(move || {
        starting.send(()).unwrap();
        guard.set(None);
        done.send(()).unwrap()
    });
    start.recv_timeout(Duration::from_secs(2))?;
    assert!(revoked.recv_timeout(Duration::from_millis(50)).is_err());
    release.send(())?;
    writer.join().unwrap();
    revoker.join().unwrap();
    let b = share.grant_owned(18777, Arc::new(|| true));
    assert_eq!(share.while_granted(a, 18777, || 1), None);
    assert_eq!(share.while_granted(b, 18777, || 1), Some(1));
    let c = share.grant_owned(18777, Arc::new(|| false));
    assert_eq!(share.while_granted(c, 18777, || 1), None);
    assert_eq!(share.current(), None);
    Ok(())
}
