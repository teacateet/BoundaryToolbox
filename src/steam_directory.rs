//! BRT 的公开房间目录。Steam SDK 只在独立子进程中初始化，不改变游戏或主窗口的 Steam 上下文。
use anyhow::{Context, Result, bail, ensure};
use libloading::os::windows::Library;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    ffi::{CStr, CString, OsString, c_char, c_void},
    fs,
    io::{self, BufRead, Write},
    os::windows::fs::OpenOptionsExt,
    path::Path,
    sync::mpsc,
    thread,
    time::{Duration, Instant},
};
use windows::Win32::Storage::FileSystem::FILE_SHARE_READ;

const APP_ID: u32 = 1364020;
const SDK_SHA: &str = "a44e5537939ae4eebc69000589aa9b2437a667813a1657cc779198bae9b815a9";
const KIND: &CStr = c"brt.directory";
const VERSION: &CStr = c"boundary.rooms.1";
const HOST: &CStr = c"brt.host";
const INVITE: &CStr = c"brt.invite";
const HEARTBEAT: &CStr = c"brt.seen";
pub(crate) const PREFIX: &str = "BRT-DIRECTORY ";
type Ptr = *mut c_void;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub(crate) struct Room {
    pub(crate) host: String,
    pub(crate) invite: String,
}

fn host_name(s: &str) -> String {
    s.chars()
        .filter(|c| !c.is_control() && !matches!(c,'\u{202a}'..='\u{202e}'|'\u{2066}'..='\u{2069}'))
        .take(48)
        .collect::<String>()
        .trim()
        .to_string()
}

fn eligible(kind: &str, name: &str, link: &str, seen: &str, now: u32) -> Option<Room> {
    let stamp = seen.parse::<u32>().ok()?;
    let host = host_name(name);
    if kind != VERSION.to_str().ok()?
        || host.is_empty()
        || now == 0
        || stamp == 0
        || stamp > now.saturating_add(5)
        || now.saturating_sub(stamp) > 45
        || !crate::app::public_invite(link)
    {
        return None;
    }
    Some(Room {
        host,
        invite: link.to_string(),
    })
}

macro_rules! bindings {
    ($($name:ident : ($($arg:ty),*) -> $out:ty),+ $(,)?) => {
        #[allow(non_snake_case)]
        struct Api { _lib: Library, $( $name: unsafe extern "C" fn($($arg),*) -> $out ),+ }
        impl Api {
            fn load(dir: &Path) -> Result<Self> {
                let base = dir.ancestors().nth(3).context("请选择 Steam 游戏目录")?;
                let dll = base.join("Engine/Binaries/ThirdParty/Steamworks/Steamv157/Win64/steam_api64.dll").canonicalize().context("找不到游戏自带的 Steam 组件")?;
                let mut file = fs::OpenOptions::new().read(true).share_mode(FILE_SHARE_READ.0).open(&dll)?;
                let mut digest = Sha256::new(); io::copy(&mut file,&mut digest)?;
                ensure!(format!("{:x}",digest.finalize()) == SDK_SHA,"Steam 组件与支持的游戏版本不符");
                // 只从已核验的绝对路径和 System32 加载依赖。
                let lib = unsafe { Library::load_with_flags(dll, 0x100 | 0x800) }?;
                Ok(Self { $( $name: unsafe { *lib.get(concat!(stringify!($name),"\0").as_bytes())? } ),+, _lib:lib })
            }
        }
    }
}
bindings! {
    SteamAPI_Init: () -> bool, SteamAPI_Shutdown: () -> (), SteamAPI_RunCallbacks: () -> (),
    SteamAPI_SteamUtils_v010: () -> Ptr, SteamAPI_SteamFriends_v017: () -> Ptr, SteamAPI_SteamMatchmaking_v009: () -> Ptr,
    SteamAPI_ISteamUtils_GetAppID: (Ptr) -> u32, SteamAPI_ISteamUtils_GetServerRealTime: (Ptr) -> u32,
    SteamAPI_ISteamUtils_IsAPICallCompleted: (Ptr,u64,*mut bool) -> bool,
    SteamAPI_ISteamUtils_GetAPICallResult: (Ptr,u64,Ptr,i32,i32,*mut bool) -> bool,
    SteamAPI_ISteamFriends_GetPersonaName: (Ptr) -> *const c_char,
    SteamAPI_ISteamMatchmaking_RequestLobbyList: (Ptr) -> u64,
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter: (Ptr,*const c_char,*const c_char,i32) -> (),
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter: (Ptr,i32) -> (),
    SteamAPI_ISteamMatchmaking_GetLobbyByIndex: (Ptr,i32) -> u64,
    SteamAPI_ISteamMatchmaking_GetLobbyData: (Ptr,u64,*const c_char) -> *const c_char,
    SteamAPI_ISteamMatchmaking_CreateLobby: (Ptr,i32,i32) -> u64,
    SteamAPI_ISteamMatchmaking_SetLobbyData: (Ptr,u64,*const c_char,*const c_char) -> bool,
    SteamAPI_ISteamMatchmaking_SetLobbyType: (Ptr,u64,i32) -> bool,
    SteamAPI_ISteamMatchmaking_LeaveLobby: (Ptr,u64) -> (),
}

// Steamworks v157 在 Windows x64 使用 8 字节对齐。LobbyMatchList_t = 510，LobbyCreated_t = 513。
#[repr(C)]
#[derive(Default)]
struct Created {
    result: i32,
    lobby: u64,
}
const _: () = assert!(size_of::<Created>() == 16);
struct Session {
    kind: CString,
    api: Api,
    mm: Ptr,
    utils: Ptr,
    friends: Ptr,
}
impl Drop for Session {
    fn drop(&mut self) {
        unsafe { (self.api.SteamAPI_Shutdown)() }
    }
}
fn string(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}

impl Session {
    fn open(dir: &Path) -> Result<Self> {
        let api = Api::load(dir)?;
        unsafe {
            ensure!(
                (api.SteamAPI_Init)(),
                "Steam 未就绪。请登录拥有《边境》的 Steam 账号并保持在线"
            );
            let s = Self {
                kind: KIND.to_owned(),
                mm: (api.SteamAPI_SteamMatchmaking_v009)(),
                utils: (api.SteamAPI_SteamUtils_v010)(),
                friends: (api.SteamAPI_SteamFriends_v017)(),
                api,
            };
            ensure!(
                !s.mm.is_null()
                    && !s.utils.is_null()
                    && !s.friends.is_null()
                    && (s.api.SteamAPI_ISteamUtils_GetAppID)(s.utils) == APP_ID,
                "Steam 未连接《边境》大厅服务"
            );
            Ok(s)
        }
    }
    fn result<T: Default>(&self, call: u64, id: i32) -> Result<T> {
        ensure!(call != 0, "Steam 未接受大厅请求");
        let end = Instant::now() + Duration::from_secs(18);
        unsafe {
            loop {
                (self.api.SteamAPI_RunCallbacks)();
                let mut failed = false;
                if (self.api.SteamAPI_ISteamUtils_IsAPICallCompleted)(self.utils, call, &mut failed)
                {
                    let mut value = T::default();
                    ensure!(
                        !failed
                            && (self.api.SteamAPI_ISteamUtils_GetAPICallResult)(
                                self.utils,
                                call,
                                (&mut value as *mut T).cast(),
                                size_of::<T>() as i32,
                                id,
                                &mut failed
                            )
                            && !failed,
                        "Steam 大厅请求失败"
                    );
                    return Ok(value);
                }
                ensure!(Instant::now() < end, "Steam 大厅响应超时，请刷新重试");
                thread::sleep(Duration::from_millis(50));
            }
        }
    }
    fn list(&self) -> Result<Vec<Room>> {
        let a = &self.api;
        unsafe {
            (a.SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter)(self.mm, 3);
            (a.SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter)(
                self.mm,
                self.kind.as_ptr(),
                VERSION.as_ptr(),
                0,
            );
            let count: u32 = self.result(
                (a.SteamAPI_ISteamMatchmaking_RequestLobbyList)(self.mm),
                510,
            )?;
            let now = (a.SteamAPI_ISteamUtils_GetServerRealTime)(self.utils);
            let mut rooms = Vec::new();
            for i in 0..count.min(50) {
                let lobby = (a.SteamAPI_ISteamMatchmaking_GetLobbyByIndex)(self.mm, i as i32);
                let get = |key: &CStr| {
                    string((a.SteamAPI_ISteamMatchmaking_GetLobbyData)(
                        self.mm,
                        lobby,
                        key.as_ptr(),
                    ))
                };
                if let Some(r) = eligible(
                    &get(&self.kind),
                    &get(HOST),
                    &get(INVITE),
                    &get(HEARTBEAT),
                    now,
                ) {
                    rooms.push(r);
                }
            }
            Ok(rooms)
        }
    }
    fn host(&self, link: &str, closed: mpsc::Receiver<()>) -> Result<()> {
        ensure!(
            crate::app::public_invite(link),
            "只有 BRT 好友房邀请可以公开"
        );
        let a = &self.api;
        unsafe {
            let name = host_name(&string((a.SteamAPI_ISteamFriends_GetPersonaName)(
                self.friends,
            )));
            #[cfg(test)]
            let name = if self.kind.as_c_str() != KIND {
                "BRT QA".to_owned()
            } else {
                name
            };
            ensure!(!name.is_empty(), "无法读取当前 Steam 昵称");
            let created: Created = self.result(
                (a.SteamAPI_ISteamMatchmaking_CreateLobby)(self.mm, 0, 10),
                513,
            )?;
            ensure!(
                created.result == 1 && created.lobby != 0,
                "Steam 创建大厅失败（{}）",
                created.result
            );
            let id = created.lobby;
            let outcome = (|| -> Result<()> {
                let set = |key: &CStr, value: &CStr| {
                    ensure!(
                        (a.SteamAPI_ISteamMatchmaking_SetLobbyData)(
                            self.mm,
                            id,
                            key.as_ptr(),
                            value.as_ptr()
                        ),
                        "Steam 未保存房间信息"
                    );
                    Ok::<_, anyhow::Error>(())
                };
                let beat = || {
                    let now = (a.SteamAPI_ISteamUtils_GetServerRealTime)(self.utils);
                    ensure!(now > 0, "Steam 时钟不可用");
                    set(HEARTBEAT, &CString::new(now.to_string())?)
                };
                set(HOST, &CString::new(name)?)?;
                set(INVITE, &CString::new(link)?)?;
                beat()?;
                set(&self.kind, VERSION)?;
                if closed.try_recv().is_ok() {
                    return Ok(());
                }
                // Invisible 可由 RequestLobbyList 搜索，不会作为原版游戏大厅显示给 Steam 好友。
                ensure!(
                    (a.SteamAPI_ISteamMatchmaking_SetLobbyType)(self.mm, id, 3),
                    "Steam 未公开房间"
                );
                println!("{PREFIX}READY");
                io::stdout().flush()?;
                let mut last = Instant::now();
                loop {
                    match closed.recv_timeout(Duration::from_millis(100)) {
                        Ok(()) | Err(mpsc::RecvTimeoutError::Disconnected) => break,
                        Err(mpsc::RecvTimeoutError::Timeout) => (),
                    }
                    (a.SteamAPI_RunCallbacks)();
                    if last.elapsed() >= Duration::from_secs(10) {
                        beat()?;
                        last = Instant::now();
                    }
                }
                Ok(())
            })();
            (a.SteamAPI_ISteamMatchmaking_SetLobbyType)(self.mm, id, 0);
            (a.SteamAPI_ISteamMatchmaking_LeaveLobby)(self.mm, id);
            outcome
        }
    }
}

pub(crate) fn run(mut args: impl Iterator<Item = OsString>) -> Result<()> {
    let op = args.next().context("缺少目录操作")?;
    ensure!(args.next().is_none(), "目录操作参数错误");
    let dir = std::env::current_dir()?;
    let s = Session::open(&dir)?;
    if op == "list" {
        println!("{PREFIX}{}", serde_json::to_string(&s.list()?)?);
        return Ok(());
    }
    if op != "host" {
        bail!("未知目录操作");
    }
    let mut lines = io::stdin().lock().lines();
    let link = lines.next().context("没有邀请")??;
    drop(lines);
    let (tx, rx) = mpsc::channel();
    thread::spawn(move || {
        let _ = io::stdin().read_line(&mut String::new());
        let _ = tx.send(());
    });
    s.host(&link, rx)
}

#[cfg(test)]
mod tests {
    use super::*;
    // 在两个独立进程中用一次性筛选键验证发现和撤下。测试房不在玩家目录中，没有对应的游戏服。
    #[test]
    #[ignore = "requires online Steam and a second test process"]
    fn qa_helper() -> Result<()> {
        let mut s = Session::open(&std::env::current_dir()?)?;
        s.kind = CString::new(format!(
            "brt.qa.{}",
            uuid::Uuid::parse_str(&std::env::var("BRT_QA_DIRECTORY")?)?.simple()
        ))?;
        if std::env::var("BRT_QA_OP")? == "list" {
            println!("QA-ROWS {}", serde_json::to_string(&s.list()?)?);
            return Ok(());
        }
        let link = format!(
            "brt://room/{}/10.193.24.5:18777",
            uuid::Uuid::new_v4().simple()
        );
        let (tx, rx) = mpsc::channel();
        thread::spawn(move || {
            let _ = io::stdin().read_line(&mut String::new());
            let _ = tx.send(());
        });
        s.host(&link, rx)
    }
    #[test]
    fn directory_ignores_wrong_versions_stale_and_non_friend_targets() {
        let link = format!(
            "brt://room/{}/10.12.4.5:18777",
            uuid::Uuid::new_v4().simple()
        );
        let k = VERSION.to_str().unwrap();
        assert_eq!(
            eligible(k, " 茶猫\n", &link, "100", 103).unwrap().host,
            "茶猫"
        );
        assert!(eligible(k, "茶猫", &link, "100", 146).is_none());
        assert!(eligible(k, "茶猫", &link, "130", 100).is_none());
        assert!(eligible("other", "茶猫", &link, "100", 100).is_none());
        assert!(eligible(k, "茶猫", "brt://lan/192.168.1.8:18777", "100", 100).is_none());
        assert!(eligible(k, "茶猫", "127.0.0.1:18777", "100", 100).is_none());
    }
}
