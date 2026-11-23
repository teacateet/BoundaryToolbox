//! 工具箱的构建期资源流水线。
//!
//! 可执行文件会把原 Python 工具箱载荷打成 zip 内嵌，编译 Slint UI，
//! 并生成 Windows 图标。公开仓库或纯源码构建允许缺少私有载荷文件，
//! 这种情况下内嵌 zip 会保持为空，真正执行安装时会通过运行时校验快速失败。

use std::env;
use std::fs::File;
use std::io::{self, Write};
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, anyhow, bail};
use image::codecs::ico::{IcoEncoder, IcoFrame};
use walkdir::WalkDir;
use zip::CompressionMethod;
use zip::ZipWriter;
use zip::write::SimpleFileOptions;

// 只打包“必须随工具箱内嵌”的本地载荷。
//
// ProjectRebound、BoundaryMetaServer 和 Node.js 都在安装时在线下载，不能放进
// 这里；否则公开构建会误判 payload 缺失，发布包也容易和线上版本脱节。
const MANAGED_ITEMS: &[&str] = &["DT_ItemType.json", "dxgi.dll", "steam_appid.txt"];

fn main() -> Result<()> {
    // 构建系统 Cargo 通过环境变量把路径传给构建脚本；生成物全部放进 OUT_DIR，
    // 避免普通构建污染仓库。
    let manifest_dir =
        PathBuf::from(env::var("CARGO_MANIFEST_DIR").context("missing CARGO_MANIFEST_DIR")?);
    let out_dir = PathBuf::from(env::var("OUT_DIR").context("missing OUT_DIR")?);

    slint_build::compile("ui/appwindow.slint").context("compile Slint UI")?;
    build_payload_zip(None, &out_dir)?;
    let mark = manifest_dir.join("assets/brt-mark.png");
    build_icon(&mark, &out_dir)?;
    println!("cargo:rerun-if-changed={}", mark.display());
    println!("cargo:rerun-if-changed=assets/brt-mark.svg");
    emit_ui_rerun(&manifest_dir)?;

    embed_windows_icon(&out_dir)?;
    Ok(())
}

/// 递归监听 Slint UI 文件，避免拆分后的子文件变更被 Cargo 缓存漏掉。
fn emit_ui_rerun(manifest_dir: &Path) -> Result<()> {
    let ui_dir = manifest_dir.join("ui");
    for entry in WalkDir::new(&ui_dir)
        .into_iter()
        .filter_map(std::result::Result::ok)
    {
        let path = entry.path();
        if path.extension().is_some_and(|ext| ext == "slint") {
            println!("cargo:rerun-if-changed={}", path.display());
        }
    }
    Ok(())
}

/// 查找包含全部受管旧载荷条目的目录。
fn find_payload_root(project_root: &Path) -> Result<PathBuf> {
    // 环境变量 BOUNDARY_PAYLOAD_ROOT 是发布构建时使用的显式覆盖路径。
    if let Ok(raw) = env::var("BOUNDARY_PAYLOAD_ROOT") {
        let candidate = PathBuf::from(raw);
        if payload_root_has_all_items(&candidate) {
            println!("cargo:rerun-if-changed={}", candidate.display());
            return Ok(candidate);
        }
        bail!(
            "BOUNDARY_PAYLOAD_ROOT does not contain all managed items: {}",
            candidate.display()
        );
    }

    // 本地开发可能从 Rust crate 或原项目根目录运行，因此检查根目录及其直接子目录。
    let mut candidates = vec![project_root.to_path_buf()];
    for entry in std::fs::read_dir(project_root)
        .with_context(|| format!("read {}", project_root.display()))?
    {
        let entry =
            entry.with_context(|| format!("read child under {}", project_root.display()))?;
        if entry.file_type().map(|kind| kind.is_dir()).unwrap_or(false) {
            candidates.push(entry.path());
        }
    }

    for candidate in &candidates {
        if payload_root_has_all_items(candidate) {
            println!("cargo:rerun-if-changed={}", candidate.display());
            return Ok(candidate.clone());
        }
    }

    let checked = candidates
        .iter()
        .map(|path| path.display().to_string())
        .collect::<Vec<_>>()
        .join(", ");
    bail!("unable to locate payload root containing all managed items. checked: {checked}")
}

/// 确认候选载荷目录包含完整安装集。
fn payload_root_has_all_items(candidate: &Path) -> bool {
    MANAGED_ITEMS
        .iter()
        .all(|item| candidate.join(item).exists())
}

/// 构建供 `src/core.rs` 使用的内嵌载荷压缩包。
fn build_payload_zip(payload_root: Option<&Path>, out_dir: &Path) -> Result<()> {
    let payload_zip = out_dir.join("payload.zip");
    let file =
        File::create(&payload_zip).with_context(|| format!("create {}", payload_zip.display()))?;
    let mut zip = ZipWriter::new(file);
    let file_options = SimpleFileOptions::default()
        .compression_method(CompressionMethod::Deflated)
        .unix_permissions(0o644);
    let dir_options = SimpleFileOptions::default()
        .compression_method(CompressionMethod::Stored)
        .unix_permissions(0o755);

    let Some(payload_root) = payload_root else {
        // 纯源码构建仍需要 payload.zip，因为 core.rs 使用 include_bytes!。
        // 空 zip 可以保证二进制可构建，真正安装时再由运行时校验报告缺失项。
        zip.finish()?;
        return Ok(());
    };

    for item in MANAGED_ITEMS {
        let path = payload_root.join(item);
        if path.is_dir() {
            // 保留相对载荷根目录的路径，解压时才能在 Binaries\Win64 下还原原布局。
            for entry in WalkDir::new(&path) {
                let entry = entry.with_context(|| format!("walk {}", path.display()))?;
                let entry_path = entry.path();
                let rel = entry_path.strip_prefix(payload_root).with_context(|| {
                    format!(
                        "strip prefix {} from {}",
                        payload_root.display(),
                        entry_path.display()
                    )
                })?;
                let rel_name = rel.to_string_lossy().replace('\\', "/");
                if entry.file_type().is_dir() {
                    if !rel_name.is_empty() {
                        zip.add_directory(format!("{rel_name}/"), dir_options)?;
                    }
                    continue;
                }
                zip.start_file(rel_name, file_options)?;
                let mut source = File::open(entry_path)
                    .with_context(|| format!("open {}", entry_path.display()))?;
                io::copy(&mut source, &mut zip)?;
            }
        } else {
            zip.start_file(item.replace('\\', "/"), file_options)?;
            let mut source =
                File::open(&path).with_context(|| format!("open {}", path.display()))?;
            io::copy(&mut source, &mut zip)?;
        }
    }

    zip.finish()?;
    Ok(())
}

/// Windows 与启动页使用同一个 BRT 标识。
fn build_icon(source: &Path, out_dir: &Path) -> Result<()> {
    let icon_png = out_dir.join("app_icon.png");
    let icon_ico = out_dir.join("app_icon.ico");

    let image = image::open(source).with_context(|| format!("open {}", source.display()))?;
    image.save(&icon_png)?;
    write_multi_size_ico(&image, &icon_ico)?;
    Ok(())
}

/// 写入多尺寸 ICO，避免 Windows 资源管理器在小图标视图下显示异常。
fn write_multi_size_ico(image: &image::DynamicImage, icon_ico: &Path) -> Result<()> {
    let mut frames = Vec::new();
    for size in [16, 24, 32, 48, 64, 128, 256] {
        let resized = image
            .resize_exact(size, size, image::imageops::FilterType::Lanczos3)
            .to_rgba8();
        frames.push(
            IcoFrame::as_png(
                resized.as_raw(),
                size,
                size,
                image::ExtendedColorType::Rgba8,
            )
            .with_context(|| format!("encode {size}x{size} icon frame"))?,
        );
    }

    let output =
        File::create(icon_ico).with_context(|| format!("create {}", icon_ico.display()))?;
    IcoEncoder::new(output)
        .encode_images(&frames)
        .with_context(|| format!("write {}", icon_ico.display()))
}

/// 将生成的 ICO 和版本信息嵌入 Windows 可执行文件资源。
fn embed_windows_icon(out_dir: &Path) -> Result<()> {
    let rc_path = out_dir.join("app_icon.rc");
    let mut rc_file =
        File::create(&rc_path).with_context(|| format!("create {}", rc_path.display()))?;
    let version = env::var("CARGO_PKG_VERSION").context("missing CARGO_PKG_VERSION")?;
    let (major, minor, patch, build) = windows_file_version_parts(&version)?;
    writeln!(rc_file, "1 ICON \"app_icon.ico\"")?;
    writeln!(
        rc_file,
        r#"
1 VERSIONINFO
FILEVERSION {major},{minor},{patch},{build}
PRODUCTVERSION {major},{minor},{patch},{build}
FILEFLAGSMASK 0x3fL
FILEFLAGS 0x0L
FILEOS 0x40004L
FILETYPE 0x1L
FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "080404b0"
        BEGIN
            VALUE "FileDescription", "Boundary Resurrection Toolkit (BRT)\0"
            VALUE "FileVersion", "{version}\0"
            VALUE "InternalName", "BRT\0"
            VALUE "OriginalFilename", "BRT.exe\0"
            VALUE "ProductName", "Boundary Resurrection Toolkit\0"
            VALUE "ProductVersion", "{version}\0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0804, 1200
    END
END"#
    )?;
    embed_resource::compile(&rc_path, embed_resource::NONE)
        .manifest_required()
        .map_err(|error| anyhow!("{error}"))?;
    Ok(())
}

fn windows_file_version_parts(version: &str) -> Result<(u16, u16, u16, u16)> {
    let mut parts = version.split('.').map(|part| {
        part.parse::<u16>()
            .with_context(|| format!("invalid version part: {part}"))
    });
    let major = parts.next().transpose()?.unwrap_or(0);
    let minor = parts.next().transpose()?.unwrap_or(0);
    let patch = parts.next().transpose()?.unwrap_or(0);
    let build = parts.next().transpose()?.unwrap_or(0);
    if parts.next().is_some() {
        bail!("too many version parts for Windows resource: {version}");
    }
    Ok((major, minor, patch, build))
}
