//! InstallerCore 基础构造、日志和载荷说明。

use std::env;
use std::path::Path;
use std::sync::{Arc, RwLock};

use anyhow::{Context, Result};
use reqwest::blocking::{Client, ClientBuilder};

use super::util::{ensure_dir, now_text, resolve_installer_home};
use super::*;

impl InstallerCore {
    /// 创建核心服务，并准备工具箱主目录和日志目录。
    pub fn new(logger: Logger) -> Result<Self> {
        let current_exe = env::current_exe().context("无法定位当前程序")?;
        let runtime_dir = current_exe
            .parent()
            .map(Path::to_path_buf)
            .context("无法解析运行目录")?;
        let installer_home = resolve_installer_home(&runtime_dir);
        ensure_dir(&installer_home)?;
        ensure_dir(&installer_home.join("logs"))?;
        Ok(Self {
            runtime_dir,
            installer_home,
            logger,
            github_proxy_prefix: Arc::new(RwLock::new(DEFAULT_GITHUB_PROXY_PREFIX.to_string())),
            network_proxy_url: Arc::new(RwLock::new(String::new())),
        })
    }

    /// 通过 UI/会话 logger 发送带时间戳的日志行。
    pub fn log(&self, message: impl Into<String>) {
        (self.logger)(format!("[{}] {}", now_text(), message.into()));
    }

    /// 日志和设置页展示的载荷来源说明。
    pub fn payload_label(&self) -> &'static str {
        "内嵌载荷 + ProjectRebound/Node.js 在线依赖"
    }

    /// 更新 GitHub 下载代理前缀；传空字符串代表直连 GitHub。
    pub fn set_github_proxy_prefix(&self, value: &str) {
        if let Ok(mut proxy) = self.github_proxy_prefix.write() {
            *proxy = normalize_github_proxy_prefix(value);
        }
    }

    /// 返回当前 GitHub 下载代理前缀。
    pub fn github_proxy_prefix(&self) -> String {
        self.github_proxy_prefix
            .read()
            .map(|proxy| proxy.clone())
            .unwrap_or_default()
    }

    /// 更新 Clash、v2rayN 等本地代理软件的 HTTP/SOCKS 地址；传空字符串代表不使用。
    pub fn set_network_proxy_url(&self, value: &str) {
        if let Ok(mut proxy) = self.network_proxy_url.write() {
            *proxy = normalize_network_proxy_url(value);
        }
    }

    /// 返回当前网络代理地址。
    pub fn network_proxy_url(&self) -> String {
        self.network_proxy_url
            .read()
            .map(|proxy| proxy.clone())
            .unwrap_or_default()
    }

    /// 如果 URL 指向 github.com，则按当前代理前缀拼接；代理为空时原样返回。
    pub fn proxied_github_url(&self, url: &str) -> String {
        proxied_github_url(&self.github_proxy_prefix(), url)
    }
}

pub fn normalize_github_proxy_prefix(value: &str) -> String {
    let value = value.trim();
    if value.is_empty() {
        return String::new();
    }
    if value.ends_with('/') {
        value.to_string()
    } else {
        format!("{value}/")
    }
}

pub fn proxied_github_url(proxy_prefix: &str, url: &str) -> String {
    if url.starts_with("https://github.com/") {
        let proxy_prefix = normalize_github_proxy_prefix(proxy_prefix);
        if proxy_prefix.is_empty() {
            url.to_string()
        } else {
            format!("{proxy_prefix}{url}")
        }
    } else {
        url.to_string()
    }
}

pub fn normalize_network_proxy_url(value: &str) -> String {
    let value = value.trim();
    if value.is_empty() {
        return String::new();
    }
    if value.contains("://") {
        return value.to_string();
    }
    if value.chars().all(|ch| ch.is_ascii_digit()) {
        return format!("http://127.0.0.1:{value}");
    }
    if value.starts_with("127.0.0.1:")
        || value.starts_with("localhost:")
        || value.starts_with("[::1]:")
    {
        return format!("http://{value}");
    }
    value.to_string()
}

pub fn build_blocking_http_client(network_proxy_url: &str) -> Result<ClientBuilder> {
    let proxy_url = normalize_network_proxy_url(network_proxy_url);
    let builder = Client::builder();
    if proxy_url.is_empty() {
        return Ok(builder);
    }
    Ok(builder.proxy(
        reqwest::Proxy::all(&proxy_url)
            .with_context(|| format!("代理软件地址无效：{proxy_url}"))?,
    ))
}
