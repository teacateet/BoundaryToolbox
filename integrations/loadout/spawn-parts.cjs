'use strict';

const scopes = ['MuzzleScope', 'BarrelScope', 'HandGuardScope', 'ReceiverUpperScope', 'GripScope',
    'SightOpticalScope', 'PointerScope', 'SightIronScope', 'AmmoStorageDeviceScope', 'StockScope'];

function checkedParts(store, weaponId, rows) {
    const index = store.getRoleDefinition('').index;
    const base = index.resolveBaseWeaponId(weaponId) || weaponId;
    const result = new Map();
    for (const row of rows) {
        const slot = row.SlotId;
        const value = row.PartId || '';
        if (!Number.isInteger(slot) || slot < 1 || slot > scopes.length || result.has(slot) ||
            (value && value !== 'None' && !index.isPartAllowedForWeaponSlot(base, scopes[slot - 1], value)))
            throw Error('Invalid part for selected weapon slot');
        result.set(slot, row);
    }
    return result;
}

function decodeSaved(store, type, raw, roleId, weaponId) {
    if (typeof raw !== 'string' || !/^(?:[0-9a-f]{2})+$/i.test(raw)) throw Error('Invalid stored weapon archive');
    const saved = type.decode(Buffer.from(raw, 'hex'));
    if (saved.RoleId?.toUpperCase() !== roleId.toUpperCase() || !saved.WeaponArchive ||
        store.nativeWeaponId(roleId, saved.WeaponArchive.WeaponId) !== weaponId) throw Error('Mismatched stored weapon');
    checkedParts(store, weaponId, saved.WeaponArchive.Parts);
    return saved;
}

function priorFor(store, role, roleId, id) {
    const base = store.getRoleDefinition(roleId).index.resolveBaseWeaponId(id);
    for (const key of [id, base]) {
        if (key && Object.hasOwn(role._weaponArchives || {}, key)) return role._weaponArchives[key];
    }
    if (role._weaponArchiveRaw) {
        const old = store.decodeWeaponArchiveRaw(role._weaponArchiveRaw);
        if (old && store.nativeWeaponId(roleId, old.weaponId) === id) return role._weaponArchiveRaw;
    }
    return null;
}

// protobuf 原型也有空串。只有实际出现在字节流中的字段才是本次编辑。
function mergeAppearance(previous, edit, infoKey) {
    if (!edit) return previous;
    if (!previous || !Object.keys(edit).length) return edit;
    const merged = { ...previous, ...edit };
    if (!Object.hasOwn(edit, infoKey)) return merged;
    const info = edit[infoKey];
    const oldInfo = previous[infoKey];
    if (info && oldInfo && Object.keys(info).length &&
        (!Object.hasOwn(info, 'Type') || info.Type === oldInfo.Type)) {
        merged[infoKey] = { ...oldInfo, ...info };
    }
    return merged;
}

// 同一把枪的增量写入只覆盖这次明确传来的部位；每个干员和枪都从自己的档案读取。
function mergeWeaponUpdate(store, role, roleId, id, bytes, type) {
    try {
        const change = decodeSaved(store, type, bytes.toString('hex'), roleId, id);
        let previous = priorFor(store, role, roleId, id);
        if (previous === null) previous = store.buildDefaultWeaponArchiveRaw(roleId, id);
        if (!previous) return null;
        const old = decodeSaved(store, type, previous, roleId, id).WeaponArchive;
        const next = change.WeaponArchive;
        const bySlot = checkedParts(store, id, old.Parts);
        for (const part of next.Parts) {
            const before = bySlot.get(part.SlotId);
            // 有外观但没有实物字段是只改外观；只有槽号的记录仍按原游戏的卸下操作处理。
            if (!Object.hasOwn(part, 'PartId') && Object.hasOwn(part, 'Ornament') && before?.PartId)
                part.PartId = before.PartId;
            if (before?.PartId && before.PartId === part.PartId && before.Ornament)
                part.Ornament = mergeAppearance(before.Ornament, part.Ornament, 'Info');
            bySlot.set(part.SlotId, part);
        }
        next.Parts = [...bySlot.values()].sort((a, b) => a.SlotId - b.SlotId);
        if (old.Skin) next.Skin = mergeAppearance(old.Skin, next.Skin, 'SkinInfo');
        return Buffer.from(type.encode(change).finish());
    } catch { return null; }
}

module.exports = { scopes, checkedParts, priorFor, decodeSaved, mergeWeaponUpdate };
