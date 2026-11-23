//! 内嵌载荷、在线 ProjectRebound 更新和安装记录统计。

use super::*;

use std::fs::{self, File};
use std::io::{self, Cursor, Read};
use std::path::{Component, Path, PathBuf};
use std::time::Duration;

use anyhow::{Context, Result, bail};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use walkdir::WalkDir;
use zip::ZipArchive;

use super::filesystem::delete_path;
use super::util::ensure_dir;

#[derive(Debug, Deserialize)]
struct NodeRelease {
    version: String,
    lts: serde_json::Value,
    #[serde(default)]
    files: Vec<String>,
}

#[derive(Debug, Clone)]
pub(crate) struct NodeRuntimePackage {
    pub(crate) source_url: String,
    pub(crate) version: String,
    pub(crate) zip_name: String,
    pub(crate) cache_hit: bool,
    bytes: Vec<u8>,
}

#[derive(Debug, Clone)]
pub(crate) struct BoundaryMetaServerPackage {
    pub(crate) source_url: String,
    pub(crate) zip_name: String,
    pub(crate) cache_hit: bool,
    bytes: Vec<u8>,
}

pub(crate) type DownloadProgress<'a> = &'a dyn Fn(u64, Option<u64>);

#[derive(Debug, Clone)]
pub(crate) struct ItemStats {
    pub(crate) size: u64,
    pub(crate) sha256: Option<String>,
    pub(crate) file_count: Option<u64>,
    pub(crate) dir_count: Option<u64>,
}

/// 打开构建期内嵌的载荷压缩包。
pub(crate) fn open_payload_archive() -> Result<ZipArchive<Cursor<&'static [u8]>>> {
    ZipArchive::new(Cursor::new(PAYLOAD_ZIP_BYTES)).context("无法读取内嵌载荷")
}

/// 将一个受管内嵌文件或目录解压到目标根目录。
pub(crate) fn extract_managed_item(item: &ManagedItem, target_root: &Path) -> Result<()> {
    let mut archive = open_payload_archive()?;
    match item.kind {
        ItemKind::File => {
            let mut entry = archive
                .by_name(item.name)
                .with_context(|| format!("内嵌载荷缺少文件 {}", item.name))?;
            let target = target_root.join(item.name);
            if let Some(parent) = target.parent() {
                fs::create_dir_all(parent)?;
            }
            let mut output = File::create(&target)?;
            io::copy(&mut entry, &mut output)?;
        }
        ItemKind::Dir => {
            let prefix = format!("{}/", item.name);
            let names: Vec<String> = archive
                .file_names()
                .filter(|name| *name == item.name || name.starts_with(&prefix))
                .map(ToOwned::to_owned)
                .collect();
            if names.is_empty() {
                bail!("内嵌载荷缺少目录 {}", item.name);
            }
            for name in names {
                let mut entry = archive.by_name(&name)?;
                let out_path = target_root.join(Path::new(&name));
                if entry.is_dir() {
                    fs::create_dir_all(&out_path)?;
                    continue;
                }
                if let Some(parent) = out_path.parent() {
                    fs::create_dir_all(parent)?;
                }
                let mut output = File::create(&out_path)?;
                io::copy(&mut entry, &mut output)?;
            }
        }
    }
    Ok(())
}

/// 将在线下载的 ProjectRebound Release.zip 原样解压到目标目录。
///
/// 这个函数只负责解压；Release.zip 的结构校验应在调用方或专门的校验函数中完成。
pub(crate) fn extract_zip_to_dir(zip_bytes: &[u8], target: &Path) -> Result<()> {
    let mut archive = ZipArchive::new(Cursor::new(zip_bytes)).context("无法打开 Release.zip")?;
    for index in 0..archive.len() {
        let mut entry = archive.by_index(index)?;
        let name = entry.name().to_string();
        let out_path = target.join(&name);
        if entry.is_dir() {
            fs::create_dir_all(&out_path)?;
            continue;
        }
        if let Some(parent) = out_path.parent() {
            fs::create_dir_all(parent)?;
        }
        let mut output = File::create(&out_path)?;
        io::copy(&mut entry, &mut output)?;
    }
    Ok(())
}

/// 判断文件是否来自在线 ProjectRebound release。
pub(crate) fn is_project_rebound_online_file(name: &str) -> bool {
    PROJECT_REBOUND_ONLINE_FILES
        .iter()
        .any(|item| item.eq_ignore_ascii_case(name))
}

/// Node.js 运行时在线下载。
pub(crate) fn is_nodejs_online_item(name: &str) -> bool {
    name.eq_ignore_ascii_case(NODEJS_DIR_NAME)
}

/// 登录服务器从 GitHub 下载源码包并安装依赖。
pub(crate) fn is_boundary_meta_server_online_item(name: &str) -> bool {
    name.eq_ignore_ascii_case(BOUNDARY_META_SERVER_DIR_NAME)
}

/// 下载当前 ProjectRebound Release.zip。
pub(crate) fn download_project_rebound_release(
    source_url: &str,
    network_proxy_url: &str,
    progress: Option<DownloadProgress<'_>>,
    cancel: Option<&InstallCancelToken>,
) -> Result<Vec<u8>> {
    download_bytes_with_progress(
        source_url,
        network_proxy_url,
        Duration::from_secs(90),
        progress,
        cancel,
    )
    .context("下载 ProjectRebound Release 失败")
}

/// 下载 BoundaryMetaServer main 分支源码包。
pub(crate) fn download_boundary_meta_server(
    cache_dir: &Path,
    source_url: &str,
    network_proxy_url: &str,
    progress: Option<DownloadProgress<'_>>,
    cancel: Option<&InstallCancelToken>,
) -> Result<BoundaryMetaServerPackage> {
    check_download_cancel(cancel)?;
    ensure_dir(cache_dir)?;
    let zip_name = "BoundaryMetaServer-main.zip".to_string();
    let zip_path = cache_dir.join(&zip_name);
    if zip_path.exists() {
        let bytes = fs::read(&zip_path)
            .with_context(|| format!("读取 BoundaryMetaServer 缓存失败：{}", zip_path.display()))?;
        if boundary_meta_server_archive_valid(&bytes) {
            if let Some(progress) = progress {
                progress(bytes.len() as u64, Some(bytes.len() as u64));
            }
            return Ok(BoundaryMetaServerPackage {
                source_url: source_url.to_string(),
                zip_name,
                cache_hit: true,
                bytes,
            });
        }
    }

    let bytes = download_bytes_with_progress(
        source_url,
        network_proxy_url,
        Duration::from_secs(180),
        progress,
        cancel,
    )
    .context("下载 BoundaryMetaServer 失败")?;
    check_download_cancel(cancel)?;
    if !boundary_meta_server_archive_valid(&bytes) {
        bail!("BoundaryMetaServer 压缩包结构无效：缺少 index.js。");
    }
    fs::write(&zip_path, &bytes)
        .with_context(|| format!("写入 BoundaryMetaServer 缓存失败：{}", zip_path.display()))?;
    Ok(BoundaryMetaServerPackage {
        source_url: source_url.to_string(),
        zip_name,
        cache_hit: false,
        bytes,
    })
}

/// 下载最新 LTS Node.js Windows zip，并用官方 SHASUMS256 校验。
pub(crate) fn download_node_runtime(
    cache_dir: &Path,
    network_proxy_url: &str,
    progress: Option<DownloadProgress<'_>>,
    cancel: Option<&InstallCancelToken>,
) -> Result<NodeRuntimePackage> {
    check_download_cancel(cancel)?;
    let release = latest_node_lts_release(network_proxy_url)?;
    check_download_cancel(cancel)?;
    let arch = node_windows_arch()?;
    let file_key = format!("win-{arch}-zip");
    if !release.files.iter().any(|file| file == &file_key) {
        bail!("Node.js {} 不提供 Windows {} zip。", release.version, arch);
    }
    let zip_name = format!("node-{}-win-{}.zip", release.version, arch);
    let base_url = format!("https://nodejs.org/dist/{}/", release.version);
    let zip_url = format!("{base_url}{zip_name}");
    let shasums_url = format!("{base_url}SHASUMS256.txt");
    let expected_sha = download_text(&shasums_url, network_proxy_url)
        .ok()
        .and_then(|text| parse_shasum_for_file(&text, &zip_name));
    check_download_cancel(cancel)?;
    ensure_dir(cache_dir)?;
    let zip_path = cache_dir.join(&zip_name);

    if zip_path.exists() {
        let bytes = fs::read(&zip_path)
            .with_context(|| format!("读取 Node.js 缓存失败：{}", zip_path.display()))?;
        if expected_sha
            .as_deref()
            .is_none_or(|expected| compute_sha256_bytes(&bytes).eq_ignore_ascii_case(expected))
        {
            if let Some(progress) = progress {
                progress(bytes.len() as u64, Some(bytes.len() as u64));
            }
            return Ok(NodeRuntimePackage {
                source_url: zip_url,
                version: release.version,
                zip_name,
                cache_hit: true,
                bytes,
            });
        }
    }

    let bytes = download_bytes_with_progress(
        &zip_url,
        network_proxy_url,
        Duration::from_secs(240),
        progress,
        cancel,
    )
    .context("下载 Node.js 运行时失败")?;
    check_download_cancel(cancel)?;
    if let Some(expected) = expected_sha {
        let actual = compute_sha256_bytes(&bytes);
        if !actual.eq_ignore_ascii_case(&expected) {
            bail!("Node.js 运行时校验失败：期望 {expected}，实际 {actual}");
        }
    }
    fs::write(&zip_path, &bytes)
        .with_context(|| format!("写入 Node.js 缓存失败：{}", zip_path.display()))?;
    Ok(NodeRuntimePackage {
        source_url: zip_url,
        version: release.version,
        zip_name,
        cache_hit: false,
        bytes,
    })
}

/// 将 Node.js zip 根目录内的文件解压为目标 nodejs 目录。
pub(crate) fn extract_node_runtime(package: &NodeRuntimePackage, target_dir: &Path) -> Result<()> {
    if target_dir.exists() {
        delete_path(target_dir)?;
    }
    fs::create_dir_all(target_dir)?;
    let mut archive = ZipArchive::new(Cursor::new(package.bytes.as_slice()))
        .context("无法读取 Node.js 运行时压缩包")?;
    let root_prefix = format!("node-{}-win-", package.version);
    for index in 0..archive.len() {
        let mut entry = archive.by_index(index)?;
        let entry_name = entry.name().replace('\\', "/");
        if entry_name.ends_with('/') {
            continue;
        }
        let relative = entry_name
            .strip_prefix(&root_prefix)
            .and_then(|name| name.split_once('/').map(|(_, rest)| rest))
            .with_context(|| {
                format!(
                    "Node.js 运行时压缩包结构不符合预期：{} / {}",
                    package.zip_name, entry_name
                )
            })?;
        if relative.trim().is_empty() {
            continue;
        }
        let relative_path = Path::new(relative);
        if !is_safe_relative_path(relative_path) {
            bail!("Node.js 运行时压缩包包含不安全路径：{relative}");
        }
        let out_path = target_dir.join(relative_path);
        if let Some(parent) = out_path.parent() {
            fs::create_dir_all(parent)?;
        }
        let mut output =
            File::create(&out_path).with_context(|| format!("创建 {}", out_path.display()))?;
        io::copy(&mut entry, &mut output)
            .with_context(|| format!("写入 {}", out_path.display()))?;
    }
    if !target_dir.join("node.exe").exists() {
        bail!("Node.js 运行时安装后未找到 node.exe。");
    }
    Ok(())
}

/// 将 GitHub 源码 zip 解压为目标 BoundaryMetaServer-main 目录。
pub(crate) fn extract_boundary_meta_server(
    package: &BoundaryMetaServerPackage,
    target_dir: &Path,
) -> Result<()> {
    if target_dir.exists() {
        delete_path(target_dir)?;
    }
    fs::create_dir_all(target_dir)?;
    let mut archive = ZipArchive::new(Cursor::new(package.bytes.as_slice()))
        .context("无法读取 BoundaryMetaServer 压缩包")?;
    for index in 0..archive.len() {
        let mut entry = archive.by_index(index)?;
        let entry_name = entry.name().replace('\\', "/");
        if entry.is_dir() || entry_name.ends_with('/') {
            continue;
        }
        let Some(relative_path) = github_archive_relative_path(&entry_name) else {
            continue;
        };
        if !is_safe_relative_path(&relative_path) {
            bail!("BoundaryMetaServer 压缩包包含不安全路径：{entry_name}");
        }
        let out_path = target_dir.join(&relative_path);
        if let Some(parent) = out_path.parent() {
            fs::create_dir_all(parent)?;
        }
        let mut output =
            File::create(&out_path).with_context(|| format!("创建 {}", out_path.display()))?;
        io::copy(&mut entry, &mut output)
            .with_context(|| format!("写入 {}", out_path.display()))?;
    }
    if !target_dir.join("index.js").exists() {
        bail!("BoundaryMetaServer 安装后未找到 index.js。");
    }
    Ok(())
}

fn is_safe_relative_path(path: &Path) -> bool {
    path.components()
        .all(|component| matches!(component, Component::Normal(_) | Component::CurDir))
}

fn boundary_meta_server_archive_valid(bytes: &[u8]) -> bool {
    let Ok(mut archive) = ZipArchive::new(Cursor::new(bytes)) else {
        return false;
    };
    (0..archive.len()).any(|index| {
        archive.by_index(index).ok().is_some_and(|entry| {
            github_archive_relative_path(&entry.name().replace('\\', "/")).is_some_and(|path| {
                path.file_name()
                    .is_some_and(|name| name.to_string_lossy().eq_ignore_ascii_case("index.js"))
            })
        })
    })
}

fn github_archive_relative_path(entry_name: &str) -> Option<PathBuf> {
    let normalized = Path::new(entry_name);
    let mut components = normalized.components();
    match components.next()? {
        Component::Normal(_) => {}
        _ => return None,
    }
    let relative = components.as_path();
    if relative.as_os_str().is_empty() {
        None
    } else {
        Some(relative.to_path_buf())
    }
}

fn latest_node_lts_release(network_proxy_url: &str) -> Result<NodeRelease> {
    let text = download_text(NODEJS_DIST_INDEX_URL, network_proxy_url)
        .context("请求 Node.js 版本索引失败")?;
    let releases =
        serde_json::from_str::<Vec<NodeRelease>>(&text).context("解析 Node.js 版本索引失败")?;
    releases
        .into_iter()
        .find(|release| release.lts != serde_json::Value::Bool(false))
        .context("Node.js 版本索引中没有找到 LTS 版本")
}

fn node_windows_arch() -> Result<&'static str> {
    match std::env::consts::ARCH {
        "x86_64" => Ok("x64"),
        "aarch64" => Ok("arm64"),
        "x86" => Ok("x86"),
        other => bail!("当前架构暂不支持自动下载 Node.js Windows 运行时：{other}"),
    }
}

fn parse_shasum_for_file(text: &str, file_name: &str) -> Option<String> {
    text.lines().find_map(|line| {
        let mut parts = line.split_whitespace();
        let sha = parts.next()?;
        let name = parts.next()?;
        if name.eq_ignore_ascii_case(file_name)
            && sha.len() == 64
            && sha.chars().all(|ch| ch.is_ascii_hexdigit())
        {
            Some(sha.to_string())
        } else {
            None
        }
    })
}

fn download_text(url: &str, network_proxy_url: &str) -> Result<String> {
    build_blocking_http_client(network_proxy_url)?
        .timeout(Duration::from_secs(30))
        .user_agent(format!("boundary-toolbox/{}", APP_VERSION))
        .build()?
        .get(url)
        .send()
        .with_context(|| format!("请求失败：{url}"))?
        .error_for_status()
        .with_context(|| format!("服务器返回错误：{url}"))?
        .text()
        .with_context(|| format!("读取响应失败：{url}"))
}

fn download_bytes_with_progress(
    url: &str,
    network_proxy_url: &str,
    timeout: Duration,
    progress: Option<DownloadProgress<'_>>,
    cancel: Option<&InstallCancelToken>,
) -> Result<Vec<u8>> {
    check_download_cancel(cancel)?;
    let mut response = build_blocking_http_client(network_proxy_url)?
        .timeout(timeout)
        .user_agent(format!("boundary-toolbox/{}", APP_VERSION))
        .build()?
        .get(url)
        .send()
        .with_context(|| format!("请求失败：{url}"))?
        .error_for_status()
        .with_context(|| format!("服务器返回错误：{url}"))?;
    let total = response.content_length();
    let mut bytes = Vec::with_capacity(total.unwrap_or_default().min(usize::MAX as u64) as usize);
    let mut downloaded = 0_u64;
    let mut last_reported = 0_u64;
    if let Some(progress) = progress {
        progress(downloaded, total);
    }

    let mut buffer = [0_u8; 128 * 1024];
    loop {
        check_download_cancel(cancel)?;
        let read = response
            .read(&mut buffer)
            .with_context(|| format!("读取下载内容失败：{url}"))?;
        if read == 0 {
            break;
        }
        bytes.extend_from_slice(&buffer[..read]);
        downloaded += read as u64;
        if let Some(progress) = progress {
            let finished = total.is_some_and(|total| downloaded >= total);
            if finished || downloaded.saturating_sub(last_reported) >= 1024 * 1024 {
                progress(downloaded, total);
                last_reported = downloaded;
            }
        }
    }
    check_download_cancel(cancel)?;
    if let Some(progress) = progress
        && downloaded != last_reported
    {
        progress(downloaded, total);
    }
    Ok(bytes)
}

fn check_download_cancel(cancel: Option<&InstallCancelToken>) -> Result<()> {
    if cancel.is_some_and(InstallCancelToken::is_cancelled) {
        bail!("安装已取消");
    }
    Ok(())
}

/// 流式计算文件 SHA-256，避免完整载入大文件。
fn compute_file_sha256(path: &Path) -> Result<String> {
    let mut file = File::open(path)?;
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 1024 * 1024];
    loop {
        let read = file.read(&mut buffer)?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(format!("{:x}", hasher.finalize()))
}

fn compute_sha256_bytes(bytes: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    format!("{:x}", hasher.finalize())
}

/// 为安装记录采集大小、哈希和文件数量元数据。
pub(crate) fn collect_stats(path: &Path) -> Result<ItemStats> {
    if path.is_file() {
        return Ok(ItemStats {
            size: path.metadata()?.len(),
            sha256: Some(compute_file_sha256(path)?),
            file_count: None,
            dir_count: None,
        });
    }

    let mut total_size = 0_u64;
    let mut file_count = 0_u64;
    let mut dir_count = 0_u64;
    for entry in WalkDir::new(path) {
        let entry = entry?;
        if entry.file_type().is_file() {
            file_count += 1;
            total_size += entry.metadata()?.len();
        } else if entry.file_type().is_dir() && entry.path() != path {
            dir_count += 1;
        }
    }
    Ok(ItemStats {
        size: total_size,
        sha256: None,
        file_count: Some(file_count),
        dir_count: Some(dir_count),
    })
}
