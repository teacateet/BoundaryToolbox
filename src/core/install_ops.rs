//! InstallerCore 安装、更新和卸载流程。

use std::collections::{HashMap, HashSet};
use std::env;
use std::ffi::{OsStr, OsString};
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::thread;
use std::time::Duration;

use anyhow::{Context, Result, bail};
use chrono::Local;
use encoding_rs::GBK;
use uuid::Uuid;

use super::cleanup::{clean_engine_ini, clean_legacy_range_mod};
use super::filesystem::{copy_path, delete_path, write_json_file};
use super::pathing::validate_win64_path;
use super::payload::{
    collect_stats, download_boundary_meta_server, download_node_runtime,
    download_project_rebound_release, extract_boundary_meta_server, extract_managed_item,
    extract_node_runtime, extract_zip_to_dir, is_boundary_meta_server_online_item,
    is_nodejs_online_item, is_project_rebound_online_file, open_payload_archive,
};
use super::util::{ensure_dir, hidden_command, iso_now};
use super::*;

struct ProjectReboundDownload {
    source_url: String,
    tag: String,
    bytes: Vec<u8>,
}

impl InstallerCore {
    /// 在触碰目标文件前确认内嵌载荷条目齐全。
    pub fn validate_payload(&self) -> Result<()> {
        let mut archive = open_payload_archive()?;
        for item in MANAGED_ITEMS {
            if is_project_rebound_online_file(item.name)
                || is_nodejs_online_item(item.name)
                || is_boundary_meta_server_online_item(item.name)
            {
                continue;
            }
            let exists = match item.kind {
                ItemKind::File => archive.by_name(item.name).is_ok(),
                ItemKind::Dir => archive
                    .file_names()
                    .any(|name| name == item.name || name.starts_with(&format!("{}/", item.name))),
            };
            if !exists {
                bail!("内嵌载荷缺少：{}", item.name);
            }
        }
        Ok(())
    }

    fn download_project_rebound_with_fallback(
        &self,
        progress: &ProgressReporter,
        cancel: &InstallCancelToken,
        network_proxy_url: &str,
    ) -> Result<ProjectReboundDownload> {
        let mirror_info = match fetch_project_rebound_mirror_info(network_proxy_url) {
            Ok(info) => Some(info),
            Err(error) => {
                self.log(format!(
                    "读取 ProjectRebound 镜像版本失败，将直接尝试 latest.zip：{}",
                    error
                ));
                None
            }
        };
        let mirror_url = mirror_info
            .as_ref()
            .map(|info| info.download_url.as_str())
            .unwrap_or(PROJECT_REBOUND_MIRROR_RELEASE_URL)
            .to_string();
        let mirror_detail = mirror_info
            .as_ref()
            .map(|info| match info.modified_at.as_deref() {
                Some(modified) => format!("{}，更新时间 {}", info.tag, modified),
                None => info.tag.clone(),
            })
            .unwrap_or_else(|| "latest".to_string());
        self.log(format!(
            "优先下载 ProjectRebound 镜像版本：{} ({})",
            mirror_url, mirror_detail
        ));
        let project_download_progress = |downloaded, total| {
            report_download_progress(
                progress,
                0.06,
                0.30,
                "下载 ProjectRebound",
                "ProjectRebound-latest.zip",
                downloaded,
                total,
            );
        };

        match download_project_rebound_release(
            &mirror_url,
            network_proxy_url,
            Some(&project_download_progress),
            Some(cancel),
        ) {
            Ok(bytes) => {
                let tag = mirror_info
                    .as_ref()
                    .map(|info| info.tag.clone())
                    .unwrap_or_else(|| self.fetch_project_rebound_github_tag_or_default());
                self.log(format!("ProjectRebound 镜像下载完成：{}", mirror_url));
                Ok(ProjectReboundDownload {
                    source_url: mirror_url,
                    tag,
                    bytes,
                })
            }
            Err(error) => {
                if cancel.is_cancelled() {
                    return Err(error);
                }
                let mirror_error = error.to_string();
                self.log(format!(
                    "ProjectRebound 镜像下载失败，回退 GitHub：{}",
                    mirror_error
                ));
                let github_tag = self.fetch_project_rebound_github_tag_or_default();
                let github_url = self
                    .proxied_github_url(&PROJECT_REBOUND_RELEASE_URL.replace("{tag}", &github_tag));
                self.log(format!(
                    "下载 ProjectRebound GitHub 回退版本：{}",
                    github_url
                ));
                let bytes = download_project_rebound_release(
                    &github_url,
                    network_proxy_url,
                    Some(&project_download_progress),
                    Some(cancel),
                )
                .with_context(|| {
                    format!("ProjectRebound 镜像下载失败：{mirror_error}；GitHub 回退下载也失败")
                })?;
                Ok(ProjectReboundDownload {
                    source_url: github_url,
                    tag: github_tag,
                    bytes,
                })
            }
        }
    }

    fn fetch_project_rebound_github_tag_or_default(&self) -> String {
        fetch_project_rebound_latest_tag(
            &self.proxied_github_url(PROJECT_REBOUND_LATEST_RELEASE_API),
            &self.network_proxy_url(),
        )
        .unwrap_or_else(|error| {
            self.log(format!(
                "读取 ProjectRebound GitHub 版本失败，使用默认 V0.8.0：{}",
                error
            ));
            "V0.8.0".to_string()
        })
    }

    /// 将工具箱载荷安装或更新到所选游戏目录。
    ///
    /// 在线依赖会先下载并完整校验，然后才替换目标文件。
    /// 已存在且不属于安装器的文件只备份一次，并记录进 state.json 供后续恢复。
    pub fn install_with_progress(
        &self,
        target_win64: &Path,
        progress: ProgressReporter,
        cancel: InstallCancelToken,
    ) -> Result<String> {
        check_install_cancel(&cancel)?;
        report_install_progress(&progress, 0.01, "准备安装", "验证内嵌载荷和目标目录。");
        self.validate_payload()?;
        check_install_cancel(&cancel)?;
        report_install_progress(&progress, 0.04, "准备安装", "检查游戏 Win64 目录。");
        validate_win64_path(target_win64)?;
        check_install_cancel(&cancel)?;
        let network_proxy_url = self.network_proxy_url();
        let project_rebound =
            self.download_project_rebound_with_fallback(&progress, &cancel, &network_proxy_url)?;
        let project_rebound_url = project_rebound.source_url.clone();
        let project_rebound_version = Some(project_rebound.tag.clone());
        let boundary_meta_server_url = self.proxied_github_url(BOUNDARY_META_SERVER_ARCHIVE_URL);
        check_install_cancel(&cancel)?;
        report_install_progress(
            &progress,
            0.32,
            "安装 ProjectRebound",
            "解压 Release.zip 到游戏目录。",
        );
        extract_zip_to_dir(&project_rebound.bytes, target_win64)
            .context("解压 ProjectRebound Release.zip 失败")?;
        check_install_cancel(&cancel)?;
        self.log(format!(
            "下载 BoundaryMetaServer 在线版本：{}",
            boundary_meta_server_url
        ));
        report_install_progress(
            &progress,
            0.34,
            "准备登录服务器",
            "从 GitHub 下载 BoundaryMetaServer。",
        );
        let meta_server_download_progress = |downloaded, total| {
            report_download_progress(
                &progress,
                0.36,
                0.52,
                "下载 BoundaryMetaServer",
                "main.zip",
                downloaded,
                total,
            );
        };
        let boundary_meta_server = download_boundary_meta_server(
            &self.installer_home.join("downloads"),
            &boundary_meta_server_url,
            &network_proxy_url,
            Some(&meta_server_download_progress),
            Some(&cancel),
        )?;
        check_install_cancel(&cancel)?;
        let meta_server_detail = if boundary_meta_server.cache_hit {
            format!("使用缓存：{}", boundary_meta_server.zip_name)
        } else {
            format!("下载完成：{}", boundary_meta_server.zip_name)
        };
        report_install_progress(&progress, 0.54, "登录服务器已准备", meta_server_detail);
        self.log("准备 Node.js 在线运行时。");
        report_install_progress(&progress, 0.56, "准备 Node.js", "查询最新 LTS 运行时。");
        let node_download_progress = |downloaded, total| {
            report_download_progress(
                &progress,
                0.58,
                0.68,
                "下载 Node.js",
                "Windows 运行时 zip",
                downloaded,
                total,
            );
        };
        let node_runtime = download_node_runtime(
            &self.installer_home.join("downloads"),
            &network_proxy_url,
            Some(&node_download_progress),
            Some(&cancel),
        )?;
        check_install_cancel(&cancel)?;
        let node_detail = if node_runtime.cache_hit {
            format!("使用缓存：{}", node_runtime.zip_name)
        } else {
            format!("下载完成：{}", node_runtime.zip_name)
        };
        report_install_progress(&progress, 0.70, "Node.js 已准备", node_detail);

        let existing_state = self.load_state(target_win64)?.unwrap_or_default();
        let existing_markers = self.load_markers(target_win64)?.unwrap_or_default();
        check_install_cancel(&cancel)?;

        // 运行时文件会被游戏/服务加载，因此备份或原子替换前先停止相关进程。
        report_install_progress(
            &progress,
            0.72,
            "准备写入文件",
            "关闭相关运行进程并清理旧配置。",
        );
        self.stop_runtime_processes_before_install(target_win64)?;
        check_install_cancel(&cancel)?;
        let legacy_removed = clean_legacy_range_mod(target_win64, self.logger.clone())?;
        let cleaned = clean_engine_ini(self.logger.clone())?;
        check_install_cancel(&cancel)?;
        let install_id = format!(
            "{}_{}",
            Local::now().format("%Y%m%d_%H%M%S"),
            &Uuid::new_v4().simple().to_string()[..8]
        );
        let paths = self.metadata_paths(target_win64);
        ensure_dir(&paths.metadata_dir)?;
        let backup_root = paths.backups_root.join(&install_id);
        ensure_dir(&backup_root)?;

        let existing_map: HashMap<String, ManagedRecord> = existing_state
            .managed_items
            .iter()
            .cloned()
            .map(|item| (item.name.clone(), item))
            .collect();
        let existing_marker_set: HashSet<String> =
            existing_markers.managed_names.into_iter().collect();

        let mut managed_records = Vec::new();
        let item_count = MANAGED_ITEMS.len().max(1) as f32;
        for (index, item) in MANAGED_ITEMS.iter().enumerate() {
            check_install_cancel(&cancel)?;
            let item_value = 0.76 + 0.16 * (index as f32 / item_count);
            report_install_progress(
                &progress,
                item_value,
                "写入安装文件",
                format!("({}/{}) {}", index + 1, MANAGED_ITEMS.len(), item.name),
            );
            let target_path = target_win64.join(item.name);
            let existing_record = existing_map.get(item.name);
            let installer_managed_before = existing_marker_set.contains(item.name);
            let existed_before_install = target_path.exists();
            let mut backup_relative =
                existing_record.and_then(|record| record.backup_relative.clone());

            if target_path.exists() && !installer_managed_before {
                // 只备份安装器接管前就存在的文件；已标记为安装器管理的文件可直接覆盖，
                // 避免每次更新都产生备份链。
                backup_relative = Some(self.backup_item(&target_path, &backup_root, item.name)?);
            }

            let source_path = if is_boundary_meta_server_online_item(item.name) {
                self.log(format!(
                    "安装在线 BoundaryMetaServer：{} -> {}",
                    boundary_meta_server.zip_name,
                    target_path.display()
                ));
                extract_boundary_meta_server(&boundary_meta_server, &target_path)?;
                check_install_cancel(&cancel)?;
                report_install_progress(
                    &progress,
                    (item_value + 0.02).min(0.92),
                    "安装登录服务器依赖",
                    "执行 npm ci --omit=dev --ignore-scripts，使用国内 npm 源下载依赖。",
                );
                self.install_boundary_meta_server_dependencies(
                    &target_path,
                    &target_win64.join(NODEJS_DIR_NAME).join("node.exe"),
                    &cancel,
                )?;
                check_install_cancel(&cancel)?;
                boundary_meta_server.source_url.clone()
            } else if is_project_rebound_online_file(item.name) {
                // ProjectRebound Release.zip 已整体解压，这里只记录单项来源。
                self.log(format!("ProjectRebound 文件已解压：{}", item.name));
                format!("{}#{}", project_rebound_url, item.name)
            } else if is_nodejs_online_item(item.name) {
                self.log(format!(
                    "安装在线 Node.js 运行时：{} -> {}",
                    node_runtime.zip_name,
                    target_path.display()
                ));
                extract_node_runtime(&node_runtime, &target_path)?;
                node_runtime.source_url.clone()
            } else {
                // 内嵌条目先删除再解压，因为目录无法用文件写入替换，且可能残留旧子项。
                if target_path.exists() {
                    self.log(format!("覆盖目标内容：{}", target_path.display()));
                    delete_path(&target_path)?;
                }
                self.log(format!(
                    "复制内嵌载荷：{} -> {}",
                    item.name,
                    target_path.display()
                ));
                extract_managed_item(item, target_win64)?;
                format!("embedded://{}", item.name)
            };

            // 写入完成后再采集状态，确保 size/hash 对应实际安装内容。
            check_install_cancel(&cancel)?;
            let stats = collect_stats(&target_path)?;
            managed_records.push(ManagedRecord {
                name: item.name.to_string(),
                item_type: match item.kind {
                    ItemKind::File => "file".to_string(),
                    ItemKind::Dir => "dir".to_string(),
                },
                target_path: target_path.display().to_string(),
                source_path,
                installed_at: iso_now(),
                existed_before_install,
                backup_relative,
                installer_managed_before,
                size: stats.size,
                sha256: stats.sha256,
                file_count: stats.file_count,
                dir_count: stats.dir_count,
            });
        }

        report_install_progress(
            &progress,
            0.94,
            "写入安装记录",
            "生成 state.json 和安装标记。",
        );
        check_install_cancel(&cancel)?;
        if let Some(ref tag) = project_rebound_version {
            let _ = std::fs::write(target_win64.join("project_rebound_version.txt"), tag);
        }
        let state = InstallState {
            version: APP_VERSION.to_string(),
            app_id: APP_ID.to_string(),
            install_id: install_id.clone(),
            source_root: format!(
                "embedded://payload.zip + {} + {} + {}",
                project_rebound_url, boundary_meta_server.source_url, node_runtime.source_url
            ),
            target_dir: target_win64.display().to_string(),
            installed_at: if existing_state.installed_at.is_empty() {
                iso_now()
            } else {
                existing_state.installed_at
            },
            updated_at: iso_now(),
            managed_items: managed_records,
            project_rebound_version,
        };
        write_json_file(&paths.state_file, &state)?;
        self.write_markers(target_win64, install_id)?;
        self.log(format!("安装状态已写入：{}", paths.state_file.display()));

        let mut notes = vec![
            "安装完成。".to_string(),
            "Payload.dll 和 ProjectReboundServerLauncher.exe 已从在线 Release 更新。".to_string(),
            format!(
                "BoundaryMetaServer 已从 GitHub 在线安装：{}。",
                boundary_meta_server.zip_name
            ),
            format!("Node.js 运行时已在线安装：{}。", node_runtime.version),
        ];
        if !legacy_removed.is_empty() {
            notes.push(format!(
                "已清理旧靶场模组残留 {} 项。",
                legacy_removed.len()
            ));
        }
        if let Some(path) = cleaned {
            notes.push(format!("并已清理冲突配置：{}", path.display()));
        }
        report_install_progress(&progress, 1.0, "安装完成", "社区服文件已写入目标目录。");
        Ok(notes.join("\n"))
    }

    /// 在 state.json 完整时移除安装文件并恢复备份。
    pub fn uninstall(&self, target_win64: &Path) -> Result<String> {
        validate_win64_path(target_win64)?;
        let paths = self.metadata_paths(target_win64);
        let state = self.load_state(target_win64)?;
        let markers = self.load_markers(target_win64)?;
        let cleaned = clean_engine_ini(self.logger.clone())?;

        if let Some(state) = state {
            self.log("检测到 state.json，执行完整卸载。");
            // 按安装记录反向处理，避免先删父目录导致子文件无法处理。
            for item in state.managed_items.iter().rev() {
                if !MANAGED_ITEMS
                    .iter()
                    .any(|managed| managed.name == item.name)
                {
                    continue;
                }
                let target_path = target_win64.join(&item.name);
                if target_path.exists() {
                    self.log(format!("删除已安装内容：{}", target_path.display()));
                    delete_path(&target_path)?;
                }
                if let Some(backup_relative) = &item.backup_relative {
                    let backup_path = target_win64.join(METADATA_DIR_NAME).join(backup_relative);
                    if backup_path.exists() {
                        self.log(format!(
                            "恢复备份：{} -> {}",
                            backup_path.display(),
                            target_path.display()
                        ));
                        copy_path(&backup_path, &target_path)?;
                    } else {
                        self.log(format!(
                            "警告：找不到备份，无法恢复 {}：{}",
                            item.name,
                            backup_path.display()
                        ));
                    }
                }
            }
            if paths.metadata_dir.exists() {
                self.log(format!(
                    "删除安装器元数据目录：{}",
                    paths.metadata_dir.display()
                ));
                delete_path(&paths.metadata_dir)?;
            }
            let mut message = "卸载完成。".to_string();
            if let Some(path) = cleaned {
                message.push_str(&format!("\n并已清理冲突配置：{}", path.display()));
            }
            return Ok(message);
        }

        // 对 markers.json 的能力刻意做了限制：只允许删除已知文件，不做恢复，
        // 因为备份关系已经丢失。
        let marker_names: Vec<String> = markers
            .unwrap_or_default()
            .managed_names
            .into_iter()
            .filter(|name| MANAGED_ITEMS.iter().any(|item| item.name == name))
            .collect();
        if marker_names.is_empty() {
            bail!("未找到可用于卸载的 state.json 或 markers.json，无法安全判断已安装内容。");
        }

        self.log("state.json 缺失，进入受限卸载模式。");
        for name in marker_names {
            let target_path = target_win64.join(&name);
            if target_path.exists() {
                self.log(format!("受限卸载删除：{}", target_path.display()));
                delete_path(&target_path)?;
            }
        }

        let mut warning = "state.json 缺失，已按受管标记删除安装内容，但无法恢复历史备份，请手动检查 .pve_installer 目录。".to_string();
        if let Some(path) = cleaned {
            warning.push_str(&format!("\n并已清理冲突配置：{}", path.display()));
        }
        Ok(warning)
    }

    fn install_boundary_meta_server_dependencies(
        &self,
        server_dir: &Path,
        node_exe: &Path,
        cancel: &InstallCancelToken,
    ) -> Result<()> {
        check_install_cancel(cancel)?;
        let server_dir = node_compatible_existing_dir(server_dir, "BoundaryMetaServer 目录")?;
        let node_exe = node_compatible_existing_file(node_exe, "node.exe")?;
        let node_dir = node_exe.parent().context("Node.js 运行时路径缺少父目录")?;
        let npm_cli = node_compatible_existing_file(
            &node_dir
                .join("node_modules")
                .join("npm")
                .join("bin")
                .join("npm-cli.js"),
            "npm-cli.js",
        )?;
        let package_lock = node_compatible_existing_file(
            &server_dir.join("package-lock.json"),
            "package-lock.json",
        )?;

        let npm_cache = self.installer_home.join("npm-cache");
        ensure_dir(&npm_cache)?;
        let npm_path = npm_script_path(node_dir, &server_dir);
        self.log(format!(
            "安装 BoundaryMetaServer npm 依赖：server={}，node={}，npm={}，lock={}，registry={}",
            server_dir.display(),
            node_exe.display(),
            npm_cli.display(),
            package_lock.display(),
            NPM_REGISTRY_URL
        ));
        let mut child = hidden_command(&node_exe)
            .current_dir(&server_dir)
            .arg("--")
            .arg(&npm_cli)
            .arg("ci")
            .arg("--omit=dev")
            .arg("--ignore-scripts")
            .arg("--no-audit")
            .arg("--no-fund")
            .arg("--loglevel=error")
            .arg(format!("--registry={NPM_REGISTRY_URL}"))
            .arg("--replace-registry-host=always")
            .env("NO_UPDATE_NOTIFIER", "1")
            .env("npm_config_update_notifier", "false")
            .env("npm_config_cache", &npm_cache)
            .env("npm_config_registry", NPM_REGISTRY_URL)
            .env("npm_config_replace_registry_host", "always")
            .env("npm_config_ignore_scripts", "true")
            .env("npm_config_scripts_prepend_node_path", "true")
            .env("npm_config_node", &node_exe)
            .env("NODE", &node_exe)
            .env("PATH", npm_path)
            .env_remove("NODE_OPTIONS")
            .env_remove("NODE_PATH")
            .env_remove("NPM_CONFIG_USERCONFIG")
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .context("执行 npm ci 安装登录服务器依赖失败")?;
        let status = loop {
            if cancel.is_cancelled() {
                let _ = child.kill();
                let _ = child.wait();
                bail!("安装已取消");
            }
            if let Some(status) = child.try_wait().context("检查 npm ci 进程状态失败")? {
                break status;
            }
            thread::sleep(Duration::from_millis(200));
        };
        let mut stdout = Vec::new();
        let mut stderr = Vec::new();
        if let Some(mut pipe) = child.stdout.take() {
            let _ = pipe.read_to_end(&mut stdout);
        }
        if let Some(mut pipe) = child.stderr.take() {
            let _ = pipe.read_to_end(&mut stderr);
        }
        if !status.success() {
            bail!(
                "安装登录服务器依赖失败：{}\n执行环境：node={}；npm={}；server={}",
                command_output_text(&stdout, &stderr, status.code()),
                node_exe.display(),
                npm_cli.display(),
                server_dir.display()
            );
        }
        for dependency in ["body-parser", "express", "protobufjs"] {
            if !server_dir.join("node_modules").join(dependency).exists() {
                bail!("安装登录服务器依赖失败：缺少 node_modules\\{dependency}");
            }
        }
        let output_text = command_output_text(&stdout, &stderr, status.code());
        if !output_text.is_empty() {
            self.log(format!("BoundaryMetaServer npm 输出：{output_text}"));
        }
        Ok(())
    }
}

fn node_compatible_existing_file(path: &Path, label: &str) -> Result<PathBuf> {
    if !path.is_file() {
        bail!(
            "安装登录服务器依赖失败：{label} 不是有效文件：{}",
            path.display()
        );
    }
    let canonical = path.canonicalize().with_context(|| {
        format!(
            "安装登录服务器依赖失败：解析 {label} 绝对路径失败：{}",
            path.display()
        )
    })?;
    Ok(strip_windows_verbatim_prefix(&canonical))
}

fn node_compatible_existing_dir(path: &Path, label: &str) -> Result<PathBuf> {
    if !path.is_dir() {
        bail!(
            "安装登录服务器依赖失败：{label} 不是有效目录：{}",
            path.display()
        );
    }
    let canonical = path.canonicalize().with_context(|| {
        format!(
            "安装登录服务器依赖失败：解析 {label} 绝对路径失败：{}",
            path.display()
        )
    })?;
    Ok(strip_windows_verbatim_prefix(&canonical))
}

#[cfg(windows)]
fn strip_windows_verbatim_prefix(path: &Path) -> PathBuf {
    let text = path.to_string_lossy();
    if let Some(rest) = text.strip_prefix(r"\\?\UNC\") {
        return PathBuf::from(format!(r"\\{rest}"));
    }
    if let Some(rest) = text.strip_prefix(r"\\?\") {
        return PathBuf::from(rest);
    }
    path.to_path_buf()
}

#[cfg(not(windows))]
fn strip_windows_verbatim_prefix(path: &Path) -> PathBuf {
    path.to_path_buf()
}

fn check_install_cancel(cancel: &InstallCancelToken) -> Result<()> {
    if cancel.is_cancelled() {
        bail!("安装已取消");
    }
    Ok(())
}

fn report_install_progress(
    progress: &ProgressReporter,
    value: f32,
    title: impl Into<String>,
    detail: impl Into<String>,
) {
    progress(InstallProgress {
        value: value.clamp(0.0, 1.0),
        title: title.into(),
        detail: detail.into(),
    });
}

fn report_download_progress(
    progress: &ProgressReporter,
    start: f32,
    end: f32,
    title: &str,
    name: &str,
    downloaded: u64,
    total: Option<u64>,
) {
    let value = if let Some(total) = total.filter(|total| *total > 0) {
        let ratio = (downloaded as f32 / total as f32).clamp(0.0, 1.0);
        start + (end - start) * ratio
    } else if downloaded == 0 {
        start
    } else {
        start + (end - start) * 0.5
    };
    report_install_progress(
        progress,
        value,
        title,
        format_download_detail(name, downloaded, total),
    );
}

fn format_download_detail(name: &str, downloaded: u64, total: Option<u64>) -> String {
    match total.filter(|total| *total > 0) {
        Some(total) => format!(
            "{}：{} / {} ({:.0}%)",
            name,
            format_size(downloaded),
            format_size(total),
            (downloaded as f64 / total as f64 * 100.0).clamp(0.0, 100.0)
        ),
        None => format!("{}：已下载 {}", name, format_size(downloaded)),
    }
}

fn format_size(bytes: u64) -> String {
    const KIB: f64 = 1024.0;
    const MIB: f64 = 1024.0 * 1024.0;
    const GIB: f64 = 1024.0 * 1024.0 * 1024.0;
    let bytes = bytes as f64;
    if bytes >= GIB {
        format!("{:.2} GiB", bytes / GIB)
    } else if bytes >= MIB {
        format!("{:.1} MiB", bytes / MIB)
    } else if bytes >= KIB {
        format!("{:.1} KiB", bytes / KIB)
    } else {
        format!("{bytes:.0} B")
    }
}

fn command_output_text(stdout: &[u8], stderr: &[u8], code: Option<i32>) -> String {
    let stderr = decode_process_output(stderr).trim().to_string();
    let stdout = decode_process_output(stdout).trim().to_string();
    let text = if !stderr.is_empty() {
        stderr
    } else if !stdout.is_empty() {
        stdout
    } else {
        match code {
            Some(0) => String::new(),
            Some(code) => format!("退出码 {code}"),
            None => "进程被系统终止".to_string(),
        }
    };
    trim_process_output(&text)
}

fn decode_process_output(bytes: &[u8]) -> String {
    if bytes.is_empty() {
        return String::new();
    }
    if let Ok(text) = std::str::from_utf8(bytes) {
        return text.to_string();
    }
    let (decoded, _, _) = GBK.decode(bytes);
    decoded.into_owned()
}

fn npm_script_path(node_dir: &Path, server_dir: &Path) -> OsString {
    let existing_path = env::var_os("PATH").or_else(|| env::var_os("Path"));
    build_npm_script_path(node_dir, server_dir, existing_path.as_deref())
}

fn build_npm_script_path(node_dir: &Path, server_dir: &Path, existing: Option<&OsStr>) -> OsString {
    let mut paths = vec![
        node_dir.to_path_buf(),
        node_dir.join("node_modules").join("npm").join("bin"),
        server_dir.join("node_modules").join(".bin"),
    ];
    if let Some(existing) = existing {
        paths.extend(env::split_paths(existing));
    }
    env::join_paths(paths).unwrap_or_else(|_| node_dir.as_os_str().to_os_string())
}

fn trim_process_output(text: &str) -> String {
    const MAX_LEN: usize = 1600;
    if text.chars().count() <= MAX_LEN {
        return text.to_string();
    }
    let mut trimmed = text.chars().take(MAX_LEN).collect::<String>();
    trimmed.push_str("...");
    trimmed
}

#[derive(serde::Deserialize)]
struct GitHubReleaseTag {
    tag_name: String,
}

/// 查询 ProjectRebound 最新稳定 Release tag。
fn fetch_project_rebound_latest_tag(api_url: &str, network_proxy_url: &str) -> Result<String> {
    let client = build_blocking_http_client(network_proxy_url)?
        .timeout(std::time::Duration::from_secs(12))
        .user_agent(format!("boundary-toolbox/{}", APP_VERSION))
        .build()
        .context("创建 ProjectRebound 版本检查客户端失败")?;
    let text = client
        .get(api_url)
        .header("Accept", "application/vnd.github+json")
        .send()
        .context("请求 ProjectRebound 最新 Release 失败")?
        .error_for_status()
        .context("ProjectRebound Release API 返回错误")?
        .text()
        .context("读取 ProjectRebound Release 响应失败")?;
    let release: GitHubReleaseTag =
        serde_json::from_str(&text).context("解析 ProjectRebound Release 信息失败")?;
    Ok(release.tag_name)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn npm_script_path_prefers_embedded_node() {
        let node_dir = PathBuf::from(r"C:\Boundary\nodejs");
        let server_dir = PathBuf::from(r"C:\Boundary\BoundaryMetaServer-main");
        let existing = OsStr::new(r"C:\Windows\System32;C:\Windows");
        let value = build_npm_script_path(&node_dir, &server_dir, Some(existing));
        let paths = env::split_paths(&value).collect::<Vec<_>>();

        assert_eq!(paths[0], node_dir);
        assert_eq!(
            paths[1],
            PathBuf::from(r"C:\Boundary\nodejs\node_modules\npm\bin")
        );
        assert_eq!(
            paths[2],
            PathBuf::from(r"C:\Boundary\BoundaryMetaServer-main\node_modules\.bin")
        );
    }

    #[test]
    fn decodes_gbk_process_output() {
        let (bytes, _, _) = GBK.encode("'node' 不是内部或外部命令");

        assert!(decode_process_output(&bytes).contains("不是内部或外部命令"));
    }

    #[test]
    fn strips_windows_verbatim_prefix_for_node() {
        assert_eq!(
            strip_windows_verbatim_prefix(Path::new(r"\\?\E:\Boundary\nodejs\node.exe")),
            PathBuf::from(r"E:\Boundary\nodejs\node.exe")
        );
        assert_eq!(
            strip_windows_verbatim_prefix(Path::new(r"\\?\UNC\server\share\node.exe")),
            PathBuf::from(r"\\server\share\node.exe")
        );
    }
}
