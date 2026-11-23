//! 只处理工具箱自己的临时房间签名密钥。按托管部署隔离，Windows 注销后失效。
use anyhow::{Context, Result, ensure};
use hmac::{Hmac, Mac};
use sha2::{Digest, Sha256};
use std::path::Path;
use windows::Win32::Security::{Credentials::*, Cryptography::*};
use windows::core::{PCWSTR, PWSTR};

pub(in crate::core) const ENV: &str = "BRT_PROFILE_ROOM_SIGNER";
pub(in crate::core) fn hex(key: &[u8]) -> String {
    key.iter().map(|b| format!("{b:02x}")).collect()
}
fn target(lease: &Path) -> Result<Vec<u16>> {
    let path = lease
        .parent()
        .context("缺少托管目录")?
        .canonicalize()?
        .join(lease.file_name().context("缺少房间回执名称")?);
    let hash = Sha256::digest(path.as_os_str().to_string_lossy().to_lowercase().as_bytes());
    Ok(format!("BoundaryToolbox/LocalRoom/v1/{hash:x}")
        .encode_utf16()
        .chain([0])
        .collect())
}
pub(in crate::core) fn replace(lease: &Path) -> Result<[u8; 32]> {
    let mut name = target(lease)?;
    let mut key = [0; 32];
    unsafe {
        BCryptGenRandom(None, &mut key, BCRYPT_USE_SYSTEM_PREFERRED_RNG).ok()?;
        let cred = CREDENTIALW {
            Type: CRED_TYPE_GENERIC,
            TargetName: PWSTR(name.as_mut_ptr()),
            CredentialBlob: key.as_mut_ptr(),
            CredentialBlobSize: 32,
            Persist: CRED_PERSIST_SESSION,
            ..Default::default()
        };
        CredWriteW(&cred, 0)?;
    }
    Ok(key)
}
pub(in crate::core) fn read(lease: &Path) -> Result<[u8; 32]> {
    let name = target(lease)?;
    let mut raw = std::ptr::null_mut();
    let key = unsafe {
        CredReadW(PCWSTR(name.as_ptr()), CRED_TYPE_GENERIC, None, &mut raw)?;
        let c = &*raw;
        let key = if c.Persist == CRED_PERSIST_SESSION
            && c.CredentialBlobSize == 32
            && !c.CredentialBlob.is_null()
        {
            Some(*c.CredentialBlob.cast::<[u8; 32]>())
        } else {
            None
        };
        CredFree(raw.cast());
        key
    };
    key.context("工具箱房间签名记录无效")
}

pub(in crate::core) fn session_key(lease: &Path) -> Result<[u8; 32]> {
    use std::fs::OpenOptions;
    use std::os::windows::fs::{MetadataExt, OpenOptionsExt};
    use std::time::{Duration, Instant};
    use windows::Win32::{
        Foundation::ERROR_NOT_FOUND,
        Storage::FileSystem::{
            FILE_ATTRIBUTE_REPARSE_POINT, FILE_FLAG_DELETE_ON_CLOSE, FILE_FLAG_OPEN_REPARSE_POINT,
        },
    };
    let deadline = Instant::now() + Duration::from_secs(2);
    let lock_path = lease.with_extension("signer-lock");
    let guard = loop {
        match OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .share_mode(0)
            .custom_flags((FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DELETE_ON_CLOSE).0)
            .open(&lock_path)
        {
            Ok(file) => break file,
            Err(e)
                if matches!(e.raw_os_error(), Some(5 | 32 | 80 | 183))
                    && Instant::now() < deadline =>
            {
                std::thread::sleep(Duration::from_millis(25))
            }
            Err(e) => return Err(e.into()),
        }
    };
    let meta = guard.metadata()?;
    ensure!(
        meta.is_file() && meta.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT.0 == 0,
        "房间签名锁无效"
    );
    match read(lease) {
        Err(e)
            if e.downcast_ref::<windows::core::Error>()
                .is_some_and(|e| e.code() == ERROR_NOT_FOUND.to_hresult()) =>
        {
            replace(lease)
        }
        existing => existing,
    }
}
pub(in crate::core) fn sign(
    key: &[u8; 32],
    run: &str,
    id: &str,
    owner: &str,
    verifier: &str,
) -> String {
    let mut mac = Hmac::<Sha256>::new_from_slice(key).expect("32-byte signing key");
    mac.update(format!("brt-local-room-v1\n{run}\n{id}\n{owner}\n{verifier}").as_bytes());
    hex(&mac.finalize().into_bytes())
}
#[cfg(test)]
pub(in crate::core) fn forget_test_key(lease: &Path) -> Result<()> {
    let name = target(lease)?;
    unsafe {
        CredDeleteW(PCWSTR(name.as_ptr()), CRED_TYPE_GENERIC, None)?;
    }
    Ok(())
}
