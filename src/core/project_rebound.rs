//! ProjectRebound mirror helpers.

use std::time::Duration;

use anyhow::{Context, Result, bail};
use reqwest::blocking::Client;
use serde::Deserialize;

use super::{
    APP_VERSION, PROJECT_REBOUND_MIRROR_DIR, PROJECT_REBOUND_MIRROR_LIST_API,
    PROJECT_REBOUND_MIRROR_RELEASE_URL, build_blocking_http_client,
};

#[derive(Debug, Clone)]
pub struct ProjectReboundMirrorInfo {
    pub tag: String,
    pub modified_at: Option<String>,
    pub download_url: String,
}

#[derive(Debug, Deserialize)]
struct OpenListResponse {
    code: i32,
    message: String,
    data: Option<OpenListData>,
}

#[derive(Debug, Deserialize)]
struct OpenListData {
    #[serde(default)]
    content: Vec<OpenListEntry>,
}

#[derive(Debug, Deserialize)]
struct OpenListEntry {
    name: String,
    #[serde(default)]
    is_dir: bool,
    modified: Option<String>,
}

pub fn fetch_project_rebound_mirror_info(
    network_proxy_url: &str,
) -> Result<ProjectReboundMirrorInfo> {
    let client = build_blocking_http_client(network_proxy_url)?
        .timeout(Duration::from_secs(12))
        .user_agent(format!("boundary-toolbox/{APP_VERSION}"))
        .build()
        .context("创建 ProjectRebound 镜像检查客户端失败")?;
    fetch_project_rebound_mirror_info_with_client(&client)
}

pub(crate) fn fetch_project_rebound_mirror_info_with_client(
    client: &Client,
) -> Result<ProjectReboundMirrorInfo> {
    let body = format!(
        "{{\"path\":\"{}\",\"password\":\"\",\"page\":1,\"per_page\":100,\"refresh\":false}}",
        PROJECT_REBOUND_MIRROR_DIR
    );
    let text = client
        .post(PROJECT_REBOUND_MIRROR_LIST_API)
        .header("Content-Type", "application/json")
        .body(body)
        .send()
        .context("请求 ProjectRebound 镜像列表失败")?
        .error_for_status()
        .context("ProjectRebound 镜像列表接口返回错误")?
        .text()
        .context("读取 ProjectRebound 镜像列表失败")?;
    let response: OpenListResponse =
        serde_json::from_str(&text).context("解析 ProjectRebound 镜像列表失败")?;
    if response.code != 200 {
        bail!("ProjectRebound 镜像列表返回错误：{}", response.message);
    }
    let data = response.data.context("ProjectRebound 镜像列表缺少 data")?;
    let latest = data
        .content
        .into_iter()
        .filter(|entry| !entry.is_dir)
        .filter_map(|entry| {
            let tag = project_rebound_tag_from_file_name(&entry.name)?;
            Some((version_key(&tag), tag, entry.modified))
        })
        .max_by(|left, right| left.0.cmp(&right.0))
        .context("ProjectRebound 镜像目录没有版本 zip")?;

    Ok(ProjectReboundMirrorInfo {
        tag: latest.1,
        modified_at: latest.2,
        download_url: PROJECT_REBOUND_MIRROR_RELEASE_URL.to_string(),
    })
}

fn project_rebound_tag_from_file_name(name: &str) -> Option<String> {
    const PREFIX: &str = "ProjectRebound-";
    const SUFFIX: &str = ".zip";
    let lower = name.to_ascii_lowercase();
    if !lower.starts_with(&PREFIX.to_ascii_lowercase()) || !lower.ends_with(SUFFIX) {
        return None;
    }
    let tag = &name[PREFIX.len()..name.len() - SUFFIX.len()];
    if tag.eq_ignore_ascii_case("latest") {
        return None;
    }
    (!version_key(tag).is_empty()).then(|| tag.to_string())
}

fn version_key(value: &str) -> Vec<u64> {
    value
        .split(|ch: char| !ch.is_ascii_digit())
        .filter(|part| !part.is_empty())
        .filter_map(|part| part.parse::<u64>().ok())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_project_rebound_versioned_zip_name() {
        assert_eq!(
            project_rebound_tag_from_file_name("ProjectRebound-V0.8.4.zip").as_deref(),
            Some("V0.8.4")
        );
        assert_eq!(
            project_rebound_tag_from_file_name("ProjectRebound-latest.zip"),
            None
        );
    }

    #[test]
    fn sorts_project_rebound_version_key_numerically() {
        assert!(version_key("V0.8.10") > version_key("V0.8.4"));
    }
}
