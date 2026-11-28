//! 只移除旧分发包中路径和内容都能确认、且新版已不使用的文件。
use super::linked;
use sha2::{Digest, Sha256};
use std::{
    fs, io,
    os::windows::{fs::OpenOptionsExt, io::AsRawHandle},
    path::Path,
};
use windows::Win32::{Foundation::HANDLE, Storage::FileSystem::*};

// 来自 V0.3 一键包；不会按扩展名或目录名批量删除。
const GAME: &[(&str, &str)] = &[
    (
        "startgame.bat",
        "f7d69ff487288f6f82d5f8008bdbf5759ca02738ac5be6b4ab58a8f727675f39",
    ),
    (
        "ProjectReboundServerWrapper.exe",
        "90826f3e199c94bef16025f5319a9d8201fd8eed7f81e9e0594d5bef5767cc17",
    ),
];

// 历代玩家 ZIP 中的文件，不包括调试构建、回滚备份或玩家档案。
const PACKAGE: &[(&str, &str)] = &[
    (
        "components/gameplay/live39/Payload.dll",
        "6982f63b4542429157b3a48f99bf2daa38cf7616d3dc47e1381817f221c1a30e",
    ),
    (
        "components/gameplay/live40/Payload.dll",
        "0c18c5bfc9d923be2bf6162e68ddc70f014eb0a79e4eec7a37f91bef9b860941",
    ),
    (
        "components/gameplay/live41/Payload.dll",
        "b6195bf1a68e6ffe005888baf22dd12f91c4c8938e69de48702160dcad4b2bc0",
    ),
    (
        "components/gameplay/live42/Payload.dll",
        "d679ad4f972b8a974a3dafb2e23d72076b14bbfa25448e203f1f0e92a3899c05",
    ),
    (
        "components/gameplay/live43/Payload.dll",
        "dbacaf4a67fd953c90864df4b0205edb4c58b52784a9b028bc930a54017f6711",
    ),
    (
        "components/gameplay/live44-radar/Payload.dll",
        "40780fd95da980868e15ba8f6af898206ea67f6d66561c3f29a7097ef7673104",
    ),
    (
        "components/gameplay/live45/Payload.dll",
        "aac377cf254d20793b165ba66213cc06caaf0dae300e22688b0c265fce9a3506",
    ),
    (
        "components/gameplay/live46/Payload.dll",
        "99358bf3bf67d46095406ab72e0007b27d55529e6962f9b8eca37aa77d02222f",
    ),
];

fn remove_known(root: &Path, rel: &str, expected: &str, active: &Path) -> io::Result<bool> {
    let path = root.join(rel);
    if path == active {
        return Ok(false);
    }
    let root_meta = fs::symlink_metadata(root)?;
    if linked(&root_meta) {
        return Ok(false);
    }
    let mut checked = root.to_path_buf();
    for part in Path::new(rel).components() {
        checked.push(part);
        match fs::symlink_metadata(&checked) {
            Ok(m) if linked(&m) || (checked == path && !m.is_file()) => return Ok(false),
            Ok(_) => (),
            Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(false),
            Err(e) => return Err(e),
        }
    }
    let mut file = fs::OpenOptions::new()
        .access_mode(FILE_GENERIC_READ.0 | DELETE.0)
        .share_mode(FILE_SHARE_READ.0)
        .custom_flags(FILE_FLAG_OPEN_REPARSE_POINT.0)
        .open(&path)?;
    if linked(&file.metadata()?) || path.canonicalize()? != root.canonicalize()?.join(rel) {
        return Ok(false);
    }
    let mut sha = Sha256::new();
    io::copy(&mut file, &mut sha)?;
    if format!("{:x}", sha.finalize()) != expected {
        return Ok(false);
    }
    // 在同一文件句柄上校验和删除，不会误删随后换进来的同名文件。
    let disposition = FILE_DISPOSITION_INFO { DeleteFile: true };
    unsafe {
        SetFileInformationByHandle(
            HANDLE(file.as_raw_handle()),
            FileDispositionInfo,
            (&disposition as *const FILE_DISPOSITION_INFO).cast(),
            size_of_val(&disposition) as u32,
        )
    }?;
    Ok(true)
}

pub(super) fn finish(mut message: String, game: &Path, runtime: &Path, active: &Path) -> String {
    let mut removed = 0;
    let mut failed = Vec::new();
    for (root, files) in [(game, GAME), (runtime, PACKAGE)] {
        for &(path, hash) in files {
            match remove_known(root, path, hash, active) {
                Ok(true) => removed += 1,
                Ok(false) => (),
                Err(_) => failed.push(path),
            }
        }
    }
    if removed > 0 {
        message.push_str(&format!(" 已清理 {removed} 个旧版组件。"));
    }
    if !failed.is_empty() {
        message.push_str(&format!(
            " 无法检查或清理：{}。关闭旧工具箱并检查文件权限后可重试。",
            failed.join("、")
        ));
    }
    message
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn cleanup_needs_matching_bytes_and_preserves_active_and_unrelated_files() -> io::Result<()> {
        let root = std::env::temp_dir().join(format!("brt-obsolete-{}", uuid::Uuid::new_v4()));
        fs::create_dir(&root)?;
        let file = root.join("startgame.bat");
        let active = root.join("Payload.dll");
        fs::write(&file, b"custom settings")?;
        fs::write(&active, b"active")?;
        let expected = format!("{:x}", Sha256::digest(b"original"));
        assert!(!remove_known(&root, "startgame.bat", &expected, &active)?);
        assert_eq!(fs::read(&file)?, b"custom settings");
        fs::write(&file, b"original")?;
        assert!(!remove_known(&root, "startgame.bat", &expected, &file)?);
        assert!(remove_known(&root, "startgame.bat", &expected, &active)?);
        assert!(!remove_known(&root, "startgame.bat", &expected, &active)?);
        assert_eq!(fs::read(&active)?, b"active");
        fs::remove_file(active)?;
        fs::remove_dir(root)
    }
}
