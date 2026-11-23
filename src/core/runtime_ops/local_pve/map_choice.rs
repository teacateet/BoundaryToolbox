use super::*;
use serde_json::value::RawValue;
use sha2::{Digest, Sha256};
use std::fmt;
use std::io::Write;
use std::os::windows::io::AsRawHandle;
use windows::Win32::Foundation::HANDLE;
use windows::Win32::Storage::FileSystem::{
    BY_HANDLE_FILE_INFORMATION, FILE_ATTRIBUTE_REPARSE_POINT, FILE_FLAG_OPEN_REPARSE_POINT,
    GetFileInformationByHandle,
};

#[derive(Clone, Debug)]
pub(crate) struct MapChoice {
    dir: PathBuf,
    mode: String,
    pub(crate) current: String,
    pub(crate) difficulty: Option<&'static str>,
    pub(crate) names: Vec<String>,
    config_id: (u32, u32, u32),
    config_hash: [u8; 32],
    table_hash: [u8; 32],
}

impl fmt::Display for MapChoice {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} 不支持当前地图 {}。请选择此难度支持的地图。",
            self.mode, self.current
        )
    }
}
impl std::error::Error for MapChoice {}

pub(crate) struct DifficultyChoice {
    base: MapChoice,
    table: Vec<u8>,
    pub(crate) index: usize,
}

impl DifficultyChoice {
    pub(crate) fn open(target: &Path) -> Result<Self> {
        let files = super::super::launch_files(target);
        let plan = Plan::read(&files.launcher_exe)?;
        let table = read_map_table(&files)?;
        Ok(Self {
            base: MapChoice::snapshot(&plan, Vec::new(), &table)?,
            table,
            index: plan.difficulty_index,
        })
    }

    pub(crate) fn select(self, index: usize) -> Result<MapChoice> {
        let &(key, mode) = DIFFICULTIES.get(index).context("请选择列表中的难度")?;
        Ok(MapChoice {
            names: menu_maps(&self.table, mode)?,
            mode: mode.to_owned(),
            difficulty: Some(key),
            ..self.base
        })
    }
}

fn identity(file: &File) -> Result<(u32, u32, u32)> {
    let mut info = BY_HANDLE_FILE_INFORMATION::default();
    unsafe {
        GetFileInformationByHandle(HANDLE(file.as_raw_handle()), &mut info)?;
    }
    ensure!(
        info.nNumberOfLinks == 1 && info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT.0 == 0,
        "配置文件不是独立的普通文件，未修改"
    );
    Ok((
        info.dwVolumeSerialNumber,
        info.nFileIndexHigh,
        info.nFileIndexLow,
    ))
}

impl MapChoice {
    pub(super) fn snapshot(plan: &Plan, names: Vec<String>, table: &[u8]) -> Result<Self> {
        let mut reader = &plan._config;
        reader.rewind()?;
        let mut bytes = Vec::new();
        reader.read_to_end(&mut bytes)?;
        Ok(Self {
            dir: plan.dir.canonicalize()?,
            mode: plan.mode_id.clone(),
            current: plan.map.clone(),
            difficulty: None,
            names,
            config_id: identity(reader)?,
            config_hash: Sha256::digest(bytes).into(),
            table_hash: Sha256::digest(table).into(),
        })
    }

    pub(crate) fn save(&self, target: &Path, index: usize) -> Result<()> {
        let selected = self.names.get(index).context("请选择列表中的地图")?;
        let files = super::super::launch_files(&target.canonicalize()?);
        let dir = files.launcher_exe.parent().context("无效安装目录")?;
        ensure!(
            dir == self.dir && dir.canonicalize()? == self.dir,
            "所选安装已变化，未修改配置"
        );
        let mut table = OpenOptions::new()
            .read(true)
            .share_mode(FILE_SHARE_READ.0)
            .open(
                files
                    .server_dir
                    .join("game/definitions/DT_GameModeDefinition.json"),
            )
            .context("模式表不可读或正在被写入，请重新预检")?;
        let mut rows = Vec::new();
        table.read_to_end(&mut rows)?;
        ensure!(
            <[u8; 32]>::from(Sha256::digest(rows)) == self.table_hash,
            "模式表已变化，未修改配置，请重新预检"
        );
        // 同一独占句柄内比对快照和写回，不能在检查后重新按路径打开。
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .share_mode(0)
            .custom_flags(FILE_FLAG_OPEN_REPARSE_POINT.0)
            .open(dir.join("serverconfig.json"))
            .context("配置已被占用、移动或不可写，未修改，请重新预检")?;
        let mut old = Vec::new();
        file.read_to_end(&mut old)?;
        ensure!(
            identity(&file)? == self.config_id
                && <[u8; 32]>::from(Sha256::digest(&old)) == self.config_hash,
            "房间配置已变化，未覆盖。请重新进入预检"
        );
        let mut new = if let Some(key) = self.difficulty {
            replace_field(&old, "difficulty", key)?
        } else {
            old.clone()
        };
        if !self.current.eq_ignore_ascii_case(selected) {
            new = replace_field(&new, "map", selected)?;
        }
        if new == old {
            return Ok(());
        }
        let mut write = |bytes: &[u8]| -> std::io::Result<()> {
            file.rewind()?;
            file.write_all(bytes)?;
            file.set_len(bytes.len() as u64)?;
            file.sync_all()
        };
        if let Err(error) = write(&new) {
            write(&old).context("保存失败，还原原配置也失败，请检查磁盘")?;
            return Err(error).context("保存失败，已还原原配置");
        }
        Ok(())
    }
}

fn replace_field(bytes: &[u8], key: &str, selected: &str) -> Result<Vec<u8>> {
    let json = bytes.strip_prefix(b"\xef\xbb\xbf").unwrap_or(bytes);
    let fields: BTreeMap<String, &RawValue> = serde_json::from_slice(json)?;
    let quoted = serde_json::to_vec(selected)?;
    let mut out = bytes.to_owned();
    if let Some(value) = fields.get(key) {
        // RawValue 从输入切片借用，只替换顶层值；不重排字段、换行或数值。
        let start = value.get().as_ptr() as usize - bytes.as_ptr() as usize;
        out.splice(start..start + value.get().len(), quoted);
    } else {
        let brace = bytes
            .iter()
            .position(|b| *b == b'{')
            .context("房间配置不是对象")?;
        let mut field = serde_json::to_vec(key)?;
        field.push(b':');
        field.extend(quoted);
        if !fields.is_empty() {
            field.push(b',');
        }
        out.splice(brace + 1..brace + 1, field);
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn edit_preserves_bom_nested_map_and_unknown_values() -> Result<()> {
        let text = "\u{feff}{\r\n \"nested\":{\"map\":\"Warehouse\"}, \"map\" : \"Warehouse\", \"externalPort\":7777, \"v\":1.20e+2}\r\n";
        assert_eq!(
            replace_field(text.as_bytes(), "map", "CircularX")?,
            text.replace("\"map\" : \"Warehouse\"", "\"map\" : \"CircularX\"")
                .as_bytes()
        );
        assert_eq!(
            replace_field(br#"{"mode":"pve"}"#, "map", "OSS")?,
            br#"{"map":"OSS","mode":"pve"}"#
        );
        Ok(())
    }

    #[test]
    fn saved_difficulty_reopens_as_the_requested_room() -> Result<()> {
        let target = std::env::temp_dir().join(uuid::Uuid::new_v4().to_string());
        let files = super::super::super::launch_files(&target);
        let dir = files.launcher_exe.parent().unwrap();
        let definitions = files.server_dir.join("game/definitions");
        fs::create_dir_all(dir)?;
        fs::create_dir_all(&definitions)?;
        let path = dir.join("serverconfig.json");
        let table = definitions.join("DT_GameModeDefinition.json");
        fs::write(&table, br#"[{"Name":"DT_GameModeDefinition","Rows":{
            "Rush_PVE_Easy":{"MapsForMatch":[{"RowName":"warehouse"}]},
            "Rush_PVE_Hard":{"MapsForMatch":[{"RowName":"OSS"}]}}}]"#)?;
        fs::write(&path, br#"{"mode":"pve","port":7777,"extra":"keep"}"#)?;
        // 老配置缺少两个字段，当前默认地图已被简单难度支持。
        let easy = DifficultyChoice::open(&target)?.select(0)?;
        easy.save(&target, 0)?;
        assert_eq!(fs::read_to_string(&path)?, r#"{"difficulty":"easy","mode":"pve","port":7777,"extra":"keep"}"#);
        let hard = DifficultyChoice::open(&target)?.select(2)?;
        let reader = Plan::read(&files.launcher_exe)?;
        assert!(hard.save(&target, 0).is_err());
        drop(reader);
        hard.save(&target, 0)?;
        let plan = Plan::read(&files.launcher_exe)?;
        assert_eq!(plan.map, "OSS");
        assert_eq!(plan.mode_class, "/Game/Online/GameMode/BP_PBGameMode_Rush_PVE_Hard.BP_PBGameMode_Rush_PVE_Hard_C");
        assert_eq!(menu_maps(&fs::read(table)?, &plan.mode_id)?, ["OSS"]);
        drop(plan);
        fs::remove_dir_all(target)?;
        Ok(())
    }

    #[test]
    fn save_rejects_replacement_table_change_and_other_install() -> Result<()> {
        let temp = std::env::temp_dir().join(uuid::Uuid::new_v4().to_string());
        fs::create_dir(&temp)?;
        let run = || -> Result<()> {
            let target = temp.join("one");
            let files = super::super::super::launch_files(&target);
            fs::create_dir_all(files.launcher_exe.parent().unwrap())?;
            let second = temp.join("two");
            fs::create_dir(&second)?;
            let path = files.launcher_exe.with_file_name("serverconfig.json");
            let config = br#"{"mode":"pve","port":7777,"map":"Warehouse"}"#;
            fs::write(&path, config)?;
            let data = br#"[{"Name":"DT_GameModeDefinition","Rows":{"Rush_PVE_Normal":{"MapsForMatch":[{"RowName":"OSS"}]}}}]"#;
            let table = files
                .server_dir
                .join("game/definitions/DT_GameModeDefinition.json");
            fs::create_dir_all(table.parent().unwrap())?;
            fs::write(&table, data)?;
            let choice = match preflight(&files) {
                Err(e) => e.downcast::<MapChoice>()?,
                Ok(_) => bail!("expected map conflict"),
            };
            assert!(choice.save(&second, 0).is_err());
            assert!(choice.save(&target, 1).is_err());
            fs::write(&table, b"[]")?;
            assert!(
                choice
                    .save(&target, 0)
                    .unwrap_err()
                    .to_string()
                    .contains("模式表已变化")
            );
            fs::write(&table, data)?;
            fs::rename(&path, path.with_extension("original"))?;
            fs::write(&path, config)?;
            assert!(
                choice
                    .save(&target, 0)
                    .unwrap_err()
                    .to_string()
                    .contains("配置已变化")
            );
            assert_eq!(fs::read(&path)?, config);
            Ok(())
        };
        let result = run();
        fs::remove_dir_all(temp)?;
        result
    }
}
