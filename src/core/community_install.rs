//! 从已校验的上游发布包补齐所选 Steam 安装；不运行下载内容。
use super::{
    InstallCancelToken, InstallProgress, InstallerCore, ProgressReporter,
    build_blocking_http_client, pathing::validate_win64_path,
};
use anyhow::{Context, Result, bail, ensure};
use sha2::{Digest, Sha256};
use std::{
    fs::{self, File},
    io::{self, Read, Write},
    path::{Component, Path, PathBuf},
    time::Duration,
};
use uuid::Uuid;
use zip::ZipArchive;
#[path = "live41_install.rs"]
mod live41;

pub const REPOSITORY: &str = "https://github.com/STanJK/ProjectRebound";
const META: &str = "BoundaryMetaServer-main/";
const REQUIRED: &[&str] = &[
    "dxgi.dll",
    "DT_ItemType.json",
    "steam_appid.txt",
    "Payload.dll",
    "nodejs/node.exe",
    "ServerLauncher/ProjectReboundServerLauncher.exe",
    "ServerLauncher/slint_cpp.dll",
    "ServerLauncher/serverconfig.json",
    "BoundaryMetaServer-main/index.js",
    "BoundaryMetaServer-main/game/loadoutStore.js",
    "BoundaryMetaServer-main/game/definitions/DT_WeaponDefinition.json",
    "BoundaryMetaServer-main/node_modules/body-parser/package.json",
    "BoundaryMetaServer-main/node_modules/express/package.json",
    "BoundaryMetaServer-main/node_modules/protobufjs/package.json",
];

#[derive(Clone, Copy)]
struct Artifact {
    name: &'static str,
    url: &'static str,
    sha: &'static str,
}
const ARTIFACTS: [Artifact; 3] = [
    Artifact {
        name: "rebound-base-stable.zip",
        url: "https://github.com/STanJK/ProjectRebound/releases/download/Stable/Release.zip",
        sha: "75b023aa985adb3156108fcaa9921f5f4c29e4660a49f63720562604c419e0d6",
    },
    Artifact {
        name: "rebound-0.8.4.zip",
        url: "https://github.com/STanJK/ProjectRebound/releases/download/V0.8.4/Release.zip",
        sha: "fff143b06c2782d559a19c86d32979c09fdeaa29afcccc99a16637c70fc16094",
    },
    Artifact {
        name: "node-v24.22.3-win-x64.zip",
        url: "https://nodejs.org/dist/v24.22.3/node-v24.22.3-win-x64.zip",
        sha: "e612af99f626ab415ea225af6bce5c971feb448cb5cae250bb1c3fa50c5f7d9f",
    },
];

fn check(cancel: &InstallCancelToken) -> Result<()> {
    ensure!(!cancel.is_cancelled(), "安装已取消");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{process::Command, sync::Arc};
    #[test]
    fn retry_gateway_failure_but_return_permanent_error() -> Result<()> {
        use std::{io::{BufRead, BufReader}, net::TcpListener};
        let listener = TcpListener::bind("127.0.0.1:0")?;
        let url = format!("http://{}/qa", listener.local_addr()?);
        let server = std::thread::spawn(move || -> Result<()> {
            for status in ["502 Bad Gateway", "200 OK", "404 Not Found"] {
                let (mut peer, _) = listener.accept()?;
                let mut lines = BufReader::new(peer.try_clone()?).lines();
                while let Some(line) = lines.next() { if line?.is_empty() { break; } }
                write!(peer, "HTTP/1.1 {status}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")?;
            }
            Ok(())
        });
        let client = reqwest::blocking::Client::builder().no_proxy().timeout(Duration::from_secs(4)).build()?;
        assert!(get_archive(&client, &url, &InstallCancelToken::new())?.status().is_success());
        let err = get_archive(&client, &url, &InstallCancelToken::new()).unwrap_err();
        assert_eq!(err.downcast_ref::<reqwest::Error>().and_then(|e| e.status()).map(|s| s.as_u16()), Some(404));
        server.join().expect("test server")?;
        Ok(())
    }
    #[test]
    fn excludes_player_data_and_legacy_tools() {
        for path in [
            "BoundaryMetaServer-main/data/loadouts/example.json",
            "BoundaryMetaServer-main/proxy.js",
            "startgame.bat",
            "ProjectBoundarySteam-Win64-Shipping.exe",
        ] {
            assert!(selected(1, path).is_none());
        }
        assert_eq!(selected(0, "Release/dxgi.dll"), Some("dxgi.dll"));
        assert!(selected(0, "Release/Payload.dll").is_none());
        assert!(selected(2, "node-v24.22.3-win-x64/npm.cmd").is_none());
    }
    #[test]
    fn conflict_and_rollback_keep_user_files() -> Result<()> {
        let root = std::env::temp_dir().join(format!("brt-install-{}", Uuid::new_v4()));
        let (src, dst) = (root.join("stage"), root.join("target"));
        fs::create_dir_all(&src)?;
        fs::create_dir_all(dst.join("ServerLauncher"))?;
        fs::write(src.join("Payload.dll"), b"new")?;
        fs::write(dst.join("Payload.dll"), b"custom")?;
        fs::write(
            dst.join("ServerLauncher/serverconfig.json"),
            b"custom settings",
        )?;
        assert!(pending(&dst, &src, &["Payload.dll".into()]).is_err());
        assert!(pending(&dst, &src, &["ServerLauncher/serverconfig.json".into()])?.is_empty());
        fs::create_dir(src.join("dir"))?;
        fs::write(src.join("dir/first"), b"write")?;
        assert!(
            publish(
                &src,
                &dst,
                &["dir/first".into(), "Payload.dll".into()],
                &InstallCancelToken::new()
            )
            .is_err()
        );
        assert!(!dst.join("dir").exists());
        assert_eq!(fs::read(dst.join("Payload.dll"))?, b"custom");
        assert_eq!(
            fs::read(dst.join("ServerLauncher/serverconfig.json"))?,
            b"custom settings"
        );
        fs::remove_dir_all(root)?;
        Ok(())
    }
    #[test]
    #[ignore = "downloads pinned upstream releases and runs the isolated Node runtime"]
    fn upstream_install_into_clean_game_directory() -> Result<()> {
        let game = PathBuf::from(
            std::env::var_os("BOUNDARY_QA_WIN64").context("Missing BOUNDARY_QA_WIN64")?,
        )
        .join(crate::core::GAME_EXE);
        let qa = Path::new(env!("CARGO_MANIFEST_DIR")).join(".workflow/qa");
        let root = qa.join(format!("community-{}", Uuid::new_v4()));
        let dst = root.join("ProjectBoundary/Binaries/Win64");
        fs::create_dir_all(&dst)?;
        fs::copy(&game, dst.join(crate::core::GAME_EXE))?;
        let mut core = InstallerCore::new(Arc::new(|_| {}))?;
        core.runtime_dir = PathBuf::from(
            std::env::var_os("BOUNDARY_QA_BUNDLE_DIR").context("Missing extracted BRT directory: BOUNDARY_QA_BUNDLE_DIR")?,
        );
        core.installer_home = qa.join("community-install-cache");
        let progress: ProgressReporter = Arc::new(|_| {});
        let first = core.install_community(&dst, progress.clone(), InstallCancelToken::new())?;
        assert!(first.contains("已补齐"));
        assert_eq!(hash(&dst.join("Payload.dll"))?, live41::SHA);
        let files = crate::core::process::launch_files(&dst);
        let config: serde_json::Value = serde_json::from_slice(&fs::read(dst.join("ServerLauncher/serverconfig.json"))?)?;
        assert_eq!(config["difficulty"], "normal");
        assert_eq!(config["map"], "CircularX");
        assert!(!files.server_dir.join("data/loadouts").exists());
        let node=Command::new(&files.node_exe).current_dir(&files.server_dir).args(["-e","for(const p of ['express','body-parser','protobufjs'])require(p);console.log(process.version)"]).output()?;
        ensure!(
            node.status.success(),
            "{}",
            String::from_utf8_lossy(&node.stderr)
        );
        let plan = crate::core::local_profile::prepare(&core.installer_home, &files)?;
        ensure!(
            matches!(plan, crate::core::local_profile::ProfilePlan::Managed(_)),
            "新安装档案未被 BRT 识别"
        );
        assert_eq!(hash(&game)?, hash(&dst.join(crate::core::GAME_EXE))?);
        let again = core.install_community(&dst, progress, InstallCancelToken::new())?;
        assert!(again.contains("保持不变"));
        println!(
            "{first} Node {}；二次安装未覆盖。证据目录 {}",
            String::from_utf8_lossy(&node.stdout).trim(),
            dst.display()
        );
        Ok(())
    }
}
fn hash(path: &Path) -> Result<String> {
    let mut sha = Sha256::new();
    io::copy(&mut File::open(path)?, &mut sha)?;
    Ok(format!("{:x}", sha.finalize()))
}
fn present(root: &Path) -> bool {
    REQUIRED
        .iter()
        .all(|p| fs::metadata(root.join(p)).is_ok_and(|m| m.is_file() && m.len() > 0))
}
fn selected(group: usize, name: &str) -> Option<&str> {
    match group {
        0 => name
            .strip_prefix("Release/")
            .filter(|s| matches!(*s, "dxgi.dll" | "DT_ItemType.json" | "steam_appid.txt")),
        1 if matches!(
            name,
            "Payload.dll"
                | "project_rebound_version.txt"
                | "ServerLauncher/ProjectReboundServerLauncher.exe"
                | "ServerLauncher/slint_cpp.dll"
                | "ServerLauncher/serverconfig.json"
        ) =>
        {
            Some(name)
        }
        1 => name
            .strip_prefix(META)
            .filter(|p| {
                matches!(
                    *p,
                    "index.js" | "package.json" | "package-lock.json" | "LICENSE.txt"
                ) || p.starts_with("game/")
                    || p.starts_with("node_modules/")
            })
            .map(|_| name),
        2 => match name.strip_prefix("node-v24.22.3-win-x64/")? {
            "node.exe" => Some("nodejs/node.exe"),
            "LICENSE" => Some("nodejs/LICENSE"),
            _ => None,
        },
        _ => None,
    }
}

fn get_archive(client: &reqwest::blocking::Client, url: &str, cancel: &InstallCancelToken) -> Result<reqwest::blocking::Response> {
    for attempt in 0..3 {
        check(cancel)?;
        match client.get(url).header("User-Agent", "Boundary-Resurrection-Toolkit").send().and_then(|r| r.error_for_status()) {
            Ok(response) => return Ok(response),
            Err(err) => {
                let transient = err.is_connect() || err.is_timeout() || err.status().is_some_and(|s| matches!(s.as_u16(), 408 | 502 | 503 | 504));
                if attempt == 2 || !transient { return Err(err.into()); }
                for _ in 0..10 * (attempt + 1) {
                    check(cancel)?;
                    std::thread::sleep(Duration::from_millis(100));
                }
            }
        }
    }
    unreachable!()
}

fn obtain(
    cache: &Path,
    art: Artifact,
    proxy: &str,
    cancel: &InstallCancelToken,
    changed: &dyn Fn(f32),
) -> Result<PathBuf> {
    let path = cache.join(art.name);
    if path.is_file() && hash(&path)? == art.sha {
        return Ok(path);
    }
    let tmp = cache.join(format!("{}.part-{}", art.name, Uuid::new_v4()));
    let result = (|| -> Result<()> {
        let client = build_blocking_http_client(proxy)?
            .connect_timeout(Duration::from_secs(15))
            .timeout(Duration::from_secs(210))
            .build()?;
        let mut response = get_archive(&client, art.url, cancel)?;
        let mut file = File::create_new(&tmp)?;
        let total = response.content_length().unwrap_or(0);
        let mut written = 0u64;
        let mut last_percent = 0;
        let mut sha = Sha256::new();
        let mut buf = [0u8; 64 * 1024];
        loop {
            check(cancel)?;
            let n = response.read(&mut buf)?;
            if n == 0 {
                break;
            }
            written += n as u64;
            ensure!(written <= 128 * 1024 * 1024, "安装归档超过大小限制");
            file.write_all(&buf[..n])?;
            sha.update(&buf[..n]);
            if total > 0 && written.saturating_mul(100) / total > last_percent {
                last_percent = written.saturating_mul(100) / total;
                changed(last_percent.min(100) as f32 / 100.0);
            }
        }
        ensure!(
            format!("{:x}", sha.finalize()) == art.sha,
            "{} 的 SHA-256 不符",
            art.name
        );
        file.sync_all()?;
        drop(file);
        fs::rename(&tmp, &path)?;
        Ok(())
    })();
    if result.is_err() {
        let _ = fs::remove_file(&tmp);
    }
    result.with_context(|| format!("获取 {} 失败：{}", art.name, art.url))?;
    Ok(path)
}

fn extract(
    archives: &[PathBuf],
    stage: &Path,
    cancel: &InstallCancelToken,
) -> Result<Vec<PathBuf>> {
    let mut files = Vec::new();
    for (group, p) in archives.iter().enumerate() {
        let mut z = ZipArchive::new(File::open(p)?)?;
        for i in 0..z.len() {
            check(cancel)?;
            let mut entry = z.by_index(i)?;
            if entry.is_dir() {
                continue;
            }
            let Some(name) = selected(group, entry.name()) else {
                continue;
            };
            ensure!(
                !entry.is_symlink()
                    && !name.contains(['\\', ':'])
                    && Path::new(name)
                        .components()
                        .all(|c| matches!(c, Component::Normal(_))),
                "归档中存在无效路径"
            );
            let rel = PathBuf::from(name);
            let dest = stage.join(&rel);
            fs::create_dir_all(dest.parent().unwrap())?;
            io::copy(&mut entry, &mut File::create_new(&dest)?)?;
            files.push(rel);
        }
    }
    ensure!(present(stage), "发布包缺少必要的运行文件");
    // 上游的 Warehouse 不是原作普通人机队列地图。只改暂存的新配置，保留玩家已有选择。
    let config = stage.join("ServerLauncher/serverconfig.json");
    let mut settings: serde_json::Value = serde_json::from_slice(&fs::read(&config)?)?;
    ensure!(settings["offline"] == true && settings["difficulty"] == "normal" && settings["mode"] == "pve", "发布包内房间配置与预期不符");
    settings["map"] = "CircularX".into();
    settings["serverName"] = "BRT".into();
    fs::write(config, serde_json::to_vec_pretty(&settings)?)?;
    Ok(files)
}

fn linked(meta: &fs::Metadata) -> bool {
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        meta.file_attributes() & 0x400 != 0
    }
    #[cfg(not(windows))]
    {
        meta.file_type().is_symlink()
    }
}
fn pending(target: &Path, stage: &Path, files: &[PathBuf]) -> Result<Vec<PathBuf>> {
    let mut add = Vec::new();
    for rel in files {
        let mut path = target.to_path_buf();
        for part in rel.components() {
            path.push(part);
            match fs::symlink_metadata(&path) {
                Ok(meta) => ensure!(!linked(&meta), "组件路径含重定向：{}", path.display()),
                Err(e) if e.kind() == io::ErrorKind::NotFound => break,
                Err(e) => return Err(e.into()),
            }
        }
        let dest = target.join(rel);
        if dest.try_exists()? {
            ensure!(dest.is_file(), "路径不是文件：{}", dest.display());
            if rel == Path::new("ServerLauncher/serverconfig.json") {
                continue;
            }
            if rel == Path::new("steam_appid.txt")
                && fs::read_to_string(&dest)?.trim() == fs::read_to_string(stage.join(rel))?.trim()
            {
                continue;
            }
            ensure!(
                hash(&dest)? == hash(&stage.join(rel))?,
                "现有组件版本不同，已保留原件：{}",
                rel.display()
            );
        } else {
            add.push(rel.clone());
        }
    }
    Ok(add)
}
fn ensure_parents(path: &Path, created: &mut Vec<PathBuf>) -> Result<()> {
    if path.is_dir() {
        ensure!(!linked(&fs::symlink_metadata(path)?), "组件目录含重定向");
        return Ok(());
    }
    ensure_parents(path.parent().context("无效目录")?, created)?;
    fs::create_dir(path)?;
    created.push(path.to_path_buf());
    Ok(())
}
fn publish(
    stage: &Path,
    target: &Path,
    files: &[PathBuf],
    cancel: &InstallCancelToken,
) -> Result<()> {
    let (mut made, mut dirs) = (Vec::new(), Vec::new());
    let result = (|| -> Result<()> {
        for rel in files {
            check(cancel)?;
            let dest = target.join(rel);
            ensure_parents(dest.parent().unwrap(), &mut dirs)?;
            let mut source = File::open(stage.join(rel))?;
            let mut out = File::create_new(&dest)?;
            made.push(dest);
            io::copy(&mut source, &mut out)?;
        }
        Ok(())
    })();
    if result.is_err() {
        for path in made.iter().rev() {
            let _ = fs::remove_file(path);
        }
        for dir in dirs.iter().rev() {
            let _ = fs::remove_dir(dir);
        }
    }
    result
}

impl InstallerCore {
    pub fn install_community(
        &self,
        target: &Path,
        progress: ProgressReporter,
        cancel: InstallCancelToken,
    ) -> Result<String> {
        validate_win64_path(target)?;
        ensure!(
            target.join(super::GAME_EXE).is_file() && !linked(&fs::symlink_metadata(target)?),
            "请选择游戏本体所在的 Win64 目录"
        );
        check(&cancel)?;
        let bundled = live41::validated_bundle(&self.runtime_dir, target)?;
        let payload = live41::inspect(target)?;
        if present(target) {
            let changed = live41::finish(target, &bundled, &payload)?;
            return Ok(if changed {
                "已更新本地人机玩法至 live41，已备份旧组件并保留玩家配装。".into()
            } else { "已安装 live41；现有组件和玩家配装均保持不变。".into() });
        }
        let cache = self.installer_home.join("downloads/community");
        fs::create_dir_all(&cache)?;
        let notify = |value, title: &str, detail: &str| {
            progress(InstallProgress {
                value,
                title: title.to_owned(),
                detail: detail.to_owned(),
            })
        };
        let mut archives = Vec::new();
        for (index, art) in ARTIFACTS.iter().enumerate() {
            let changed = |fraction| {
                notify(
                    (index as f32 + fraction) / 4.0,
                    "下载并校验社区组件",
                    art.name,
                )
            };
            changed(0.0);
            archives.push(obtain(
                &cache,
                *art,
                &self.network_proxy_url(),
                &cancel,
                &changed,
            )?);
        }
        let stage = cache.join(format!("stage-{}", Uuid::new_v4()));
        fs::create_dir(&stage)?;
        let result = (|| -> Result<String> {
            notify(0.77, "校验安装文件", "正在检查现有组件与档案。");
            let mut files = extract(&archives, &stage, &cancel)?;
            if payload.is_some() {
                files.retain(|p| p != Path::new("Payload.dll"));
            } else {
                fs::copy(&bundled, stage.join("Payload.dll"))?;
                ensure!(hash(&stage.join("Payload.dll"))? == live41::SHA, "玩法组件校验失败");
            }
            let add = pending(target, &stage, &files)?;
            check(&cancel)?;
            notify(0.90, "安装社区组件", "仅补齐缺失文件，保留现有房间配置。");
            publish(&stage, target, &add, &cancel)?;
            live41::finish(target, &bundled, &payload)?;
            if !present(target) {
                bail!("安装目录的文件在操作期间发生变化，请重新检查");
            }
            notify(1.0, "安装完成", "ProjectRebound V0.8.4 · BRT live41");
            Ok(format!(
                "社区环境安装完成：已补齐 {} 个文件，live41 已就绪。现在可以进入原版大厅。",
                add.len()
            ))
        })();
        let _ = fs::remove_dir_all(&stage);
        result
    }
}
