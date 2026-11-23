use std::collections::BTreeMap;
use std::fs::File;
use std::io::BufReader;
use std::path::Path;

use anyhow::{Context, Result, ensure};
use serde::Deserialize;
use serde_json::Value;

#[derive(Deserialize)]
struct Table {
    #[serde(rename = "Rows")]
    rows: BTreeMap<String, Value>,
}

fn rows(directory: &Path, name: &str) -> Result<BTreeMap<String, Value>> {
    let path = directory.join(format!("DT_{name}Definition.json"));
    let file = File::open(&path).with_context(|| path.display().to_string())?;
    let tables: Vec<Table> = serde_json::from_reader(BufReader::new(file))
        .with_context(|| format!("无法读取 {name} 表"))?;
    ensure!(tables.len() == 1, "{name} 表结构不正确");
    Ok(tables.into_iter().next().unwrap().rows)
}

#[derive(Debug)]
pub(crate) struct Item {
    pub id: String,
    pub category: String,
    pub detail: String,
}

pub(crate) struct Catalog {
    pub weapons: Vec<Item>,
    pub pods: usize,
    pub parts: usize,
    pub roles: usize,
}

const SLOTS: &[(&str, &str)] = &[
    ("SightOpticalScope", "光学瞄具"), ("SightIronScope", "机械瞄具"),
    ("MuzzleScope", "枪口"), ("BarrelScope", "枪管"), ("HandGuardScope", "护木"),
    ("ReceiverUpperScope", "上机匣"), ("GripScope", "握把"), ("PointerScope", "指示器"),
    ("AmmoStorageDeviceScope", "供弹具"), ("StockScope", "枪托"),
    ("ProjectileScope", "弹药"), ("SuitScope", "外观"), ("OrnamentScope", "挂饰"),
];

pub(crate) fn read(win64: &Path) -> Result<Catalog> {
    let directory = win64.join("BoundaryMetaServer-main/game/definitions");
    let weapons = rows(&directory, "Weapon")?;
    let parts = rows(&directory, "WeaponPart")?;
    let pods = rows(&directory, "Pod")?;
    let roles = rows(&directory, "Character")?;
    let mut items = Vec::with_capacity(weapons.len());
    for (id, row) in weapons {
        let prefix = format!("{id}_");
        let mut lines = Vec::new();
        let mut users = Vec::new();
        for (role_id, role) in &roles {
            let role_weapon = format!("{role_id}_{id}");
            if role["CustomScope"]["WeaponScope"].as_array().is_some_and(|scope|
                scope.iter().any(|value| value.as_str() == Some(&role_weapon))) {
                users.push(role_id.as_str());
            }
        }
        if !users.is_empty() {
            lines.push(format!("角色   {}", users.join("   ")));
        }
        for &(field, label) in SLOTS {
            let Some(values) = row["CustomScope"][field].as_array() else { continue };
            let choices: Vec<_> = values.iter().filter_map(Value::as_str)
                .map(|part| part.strip_prefix(&prefix).unwrap_or(part)).collect();
            if !choices.is_empty() {
                lines.push(format!("{label}   {}", choices.join("   /   ")));
            }
        }
        let kind = row["Description"]["SubType"].as_str().unwrap_or("");
        items.push(Item {
            id,
            category: kind.rsplit("::").next().unwrap_or(kind).to_owned(),
            detail: lines.join("\n\n"),
        });
    }
    ensure!(!items.is_empty(), "武器表为空");
    Ok(Catalog { weapons: items, pods: pods.len(), parts: parts.len(), roles: roles.len() })
}
