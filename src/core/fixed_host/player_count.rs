use super::*;
use std::sync::atomic::{AtomicU64, Ordering as AtomicOrdering};
use windows::Win32::Foundation::CloseHandle;
use windows::Win32::System::Memory::{FILE_MAP_READ, MapViewOfFile, OpenFileMappingW, UnmapViewOfFile};
use windows::Win32::System::SystemInformation::GetTickCount64;
use windows::core::HSTRING;

fn decode(word: u64, role: u8, now: u64) -> Option<u8> {
    let count = word as u8;
    (word != 0 && (word >> 8) as u8 == role && count <= 128 &&
        now.checked_sub(word >> 16).is_some_and(|age| age <= 6000)).then_some(count)
}

fn sample(pid: u32, role: u8) -> Option<u8> {
    let name = HSTRING::from(format!("Local\\BRT.RoomCount.v1.{pid}"));
    // 映射仅包含由游戏进程原子写入的一个对齐字。工具箱只读，旧组件没有映射时返回未知。
    unsafe {
        let section = OpenFileMappingW(FILE_MAP_READ.0, false, &name).ok()?;
        let view = MapViewOfFile(section, FILE_MAP_READ, 0, 0, 8);
        let _ = CloseHandle(section);
        if view.Value.is_null() { return None; }
        let word = (&*view.Value.cast::<AtomicU64>()).load(AtomicOrdering::Acquire);
        let _ = UnmapViewOfFile(view);
        decode(word, role, GetTickCount64())
    }
}

pub(crate) fn current(target: &Path, hosted: Option<&(PathBuf, u16)>) -> (Option<u8>, Option<u8>) {
    let host = hosted.and_then(|(path, port)| {
        // 仅接受本房的启动器及所选安装实际拥有端口的游戏子进程。
        let owners = udp_owners(*port).ok()?;
        if owners.len() != 1 { return None; }
        let pid = *owners.iter().next()?;
        let id = Pid::from_u32(pid);
        let sys = process_snapshot(&[id]);
        let proc = sys.process(id)?;
        if !same_file(proc.exe(), &path.join(GAME_EXE)) { return None; }
        let parent = proc.parent()?.as_u32();
        for room in [room_path(path, *port).ok()?, coop_path(path, *port)] {
            if saved_owner(&room).ok().flatten().is_some_and(|o| o.pid == parent && is_owner(&o, path, &room)) {
                return sample(pid, 1);
            }
        }
        None
    });
    let mut sys = System::new();
    sys.refresh_processes_specifics(ProcessesToUpdate::All, true, ProcessRefreshKind::nothing());
    let ids: Vec<_> = sys.processes().iter().filter(|(_,p)| p.name().eq_ignore_ascii_case(GAME_EXE))
        .map(|(id,_)| *id).collect();
    let sys = process_snapshot(&ids);
    let exe = target.join(GAME_EXE);
    let mut clients = ids.iter().filter_map(|id| sys.process(*id)).filter(|p| same_file(p.exe(), &exe)
        && !p.cmd().iter().any(|arg| arg.eq_ignore_ascii_case("-server")));
    let client = clients.next().and_then(|p| if clients.next().is_none() {sample(p.pid().as_u32(), 2)} else {None});
    (host, client)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn count_belongs_to_role_and_expires_including_zero() {
        let empty = (10_000 << 16) | 0x0100;
        assert_eq!(decode(empty, 1, 16_000), Some(0));
        assert_eq!(decode(empty, 1, 16_001), None);
        assert_eq!(decode(empty, 2, 10_000), None);
        assert_eq!(decode(empty, 1, 9_999), None);
        assert_eq!(decode(empty + 129, 1, 10_000), None);
        assert_eq!(decode(0, 1, 0), None);
    }
}
