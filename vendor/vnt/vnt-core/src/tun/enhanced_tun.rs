use crate::context::NetworkAddr;
use crate::nat::internal_nat::InternalNatInbound;
use crate::protocol::transmission::TransmissionBytes;
use crate::tun::TunDataInbound;

#[derive(Clone)]
pub enum EnhancedTunInbound {
    Disabled,
    Tun(TunDataInbound),
    Nat(InternalNatInbound),
}
impl EnhancedTunInbound {
    pub async fn inbound(&self, data: TransmissionBytes, net: &NetworkAddr) -> anyhow::Result<()> {
        match self {
            EnhancedTunInbound::Disabled => Ok(()),
            EnhancedTunInbound::Tun(tun) => tun.send(data, net).await,
            EnhancedTunInbound::Nat(nat) => nat.send(&data, net).await,
        }
    }
}
