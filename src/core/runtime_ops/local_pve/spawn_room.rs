//! 本机单人房的一次性档案凭据。原作大厅、游戏参数和就绪文件均不接收密钥。
use crate::core::local_profile::room_attestation;
use anyhow::Result;
use sha2::{Digest, Sha256};
use std::process::Command;
use uuid::Uuid;
use windows::Win32::Security::Cryptography::{BCRYPT_USE_SYSTEM_PREFERRED_RNG, BCryptGenRandom};
pub(in crate::core::runtime_ops) use crate::core::local_profile::legacy_client::active_owner;

const ROOM: &str = "BRT_LOCAL_SPAWN_ROOM";
const TOKEN: &str = "BRT_LOCAL_SPAWN_TOKEN";

pub(in crate::core::runtime_ops) fn clear(cmd: &mut Command) {
    cmd.env_remove(ROOM)
        .env_remove(TOKEN)
        .env_remove("BRT_SPAWN_ROOMS")
        .env_remove(room_attestation::ENV);
}

pub(in crate::core::runtime_ops) struct Grant {
    room: String,
    token: String,
    verifier: String,
    owner: String,
    signature: String,
}

impl Grant {
    pub(in crate::core::runtime_ops) fn issue(
        choice: Uuid,
        owner: String,
        run: &str,
        signer: &[u8; 32],
    ) -> Result<Self> {
        let mut key = [0u8; 32];
        unsafe {
            BCryptGenRandom(None, &mut key, BCRYPT_USE_SYSTEM_PREFERRED_RNG).ok()?;
        }
        let verifier = format!("{:x}", Sha256::digest(key));
        let token = key.iter().map(|b| format!("{b:02x}")).collect();
        let signature = room_attestation::sign(signer, run, &choice.to_string(), &owner, &verifier);
        Ok(Self {
            room: format!("pve-{choice}"),
            token,
            verifier,
            owner,
            signature,
        })
    }

    pub(in crate::core::runtime_ops) fn attach(&self, cmd: &mut Command) {
        cmd.env(ROOM, &self.room).env(TOKEN, &self.token);
    }

    // 校验值经原有已核验的控制连接返回，绝不把可重放的 bearer 给大厅。
    pub(in crate::core::runtime_ops) fn proof(&self) -> serde_json::Value {
        serde_json::json!({"verifier": self.verifier, "owner": self.owner, "signature": self.signature})
    }
}
