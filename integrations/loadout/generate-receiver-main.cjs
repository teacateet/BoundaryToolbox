'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const receiver = 'EPBPartSlotType::ReceiverMain';
const valid = value => typeof value === 'string' && value.length > 0 && value !== 'None';
function record(file) {
    const data = JSON.parse(fs.readFileSync(file));
    assert.ok(Array.isArray(data) && data.length === 1, file);
    return data[0];
}
function slot(entries) {
    if (!Array.isArray(entries)) return { invalid: true };
    const found = entries.filter(entry => entry.Key === receiver);
    if (found.length === 0) return {};
    if (found.length !== 1 || !valid(found[0].Value)) return { invalid: true };
    return { value: found[0].Value };
}
function readDefault(content, weapon) {
    const reference = weapon.DefaultConfig?.AssetPathName;
    if (typeof reference !== 'string') return;
    const match = /^\/Game\/((?:[A-Za-z0-9_-]+\/)*([A-Za-z0-9_-]+))\.([A-Za-z0-9_-]+)$/.exec(reference);
    if (!match || match[2] !== match[3] || weapon.DefaultConfig.SubPathString) return;
    const file = path.join(content, match[1] + '.json');
    if (!fs.existsSync(file)) return;
    const asset = record(file);
    if (asset.Type !== 'PBWeaponDefaultConfig' || asset.Name !== match[2]) return;
    return slot(asset.Properties?.EquippedPartIDMap).value;
}
function nameIndex(rows) {
    const names = new Map();
    for (const name of Object.keys(rows)) {
        const key = name.toLowerCase();
        assert.ok(!names.has(key), 'ambiguous FName: ' + name);
        names.set(key, name);
    }
    return names;
}
function coreAppearance(weapons, regular, variants, current, overrideId, skinDefinition) {
    const row = variants[current.PropertyConfigID];
    const base = regular[overrideId];
    // 套装引用普通机匣时，比对原生枪类、两种视角的实际模型和皮肤资产，再使用变体自己的机匣 ID。
    const sameClass = Object.values(weapons).some(other =>
        other.PropertyConfigID === overrideId && other.CustomScope.ReceiverMain === overrideId &&
        other.WeaponClass.AssetPathName === current.WeaponClass.AssetPathName);
    return sameClass && row && base && skinDefinition?.Mesh1PSkinInfo?.AssetPathName?.startsWith('/Game/') &&
        skinDefinition.Mesh3PSkinInfo?.AssetPathName?.startsWith('/Game/') &&
        ['Mesh1P', 'Mesh3P'].every(key => row[key]?.ObjectPath && row[key].ObjectPath === base[key]?.ObjectPath);
}
function generate(server, content) {
    const def = name => record(path.join(server, 'game/definitions', name + '.json')).Rows;
    const src = name => record(path.join(content, 'GameData/CSVConfig', name + '.json')).Rows;
    const weapons = def('DT_WeaponDefinition');
    const skinDir = path.join(content, 'GameData/CSVConfig/Skin/Release');
    // 零售总表的 ParentTables 只有 Release、Event。KOL 包存在但不属于该总表。
    const suits = { ...record(path.join(skinDir, 'DT_WeaponSuitDefinition_Release.json')).Rows,
        ...record(path.join(skinDir, 'DT_WeaponSuitDefinition_Event.json')).Rows };
    const paints = record(path.join(skinDir, 'DT_WeaponSuitPaintingDefinition_Release.json')).Rows;
    const suitNames = nameIndex(suits), paintNames = nameIndex(paints);
    const standard = src('Item/Config/DT_WeaponConfig');
    const hardcore = src('Item/Config/DT_WeaponConfig_Hardcore');
    const partSkins = src('Skin/Release/DT_WeaponPartSkinDefinition_Release');
    const characterRows = def('DT_CharacterDefinition');
    const redirections = src('ItemIDRedirection/DT_WeaponDefinitionRedirection');
    const hostRoles = new Set(['PEACE', 'PROBE', 'FIXER', 'SNIPER', 'FORT', 'SPIKE']);
    const catalog = { weapons: {}, suits: {} };
    const skipped = [], coverage = {}, usedSuits = new Set();
    const suitPaints = new Set(), usedPaints = new Set();
    for (const suit of Object.values(suits)) for (const id of suit.WeaponSuitPaintingIDArray) {
        const value = paintNames.get(id.toLowerCase());
        if (value) suitPaints.add(value);
    }
    let combinations = 0;
    for (const [weaponId, weapon] of Object.entries(weapons).sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0)) {
        const scope = weapon.CustomScope;
        const receiverId = readDefault(content, weapon);
        const roles = Object.entries(characterRows).filter(([, row]) => row.CustomScope?.WeaponScope?.some(id =>
            redirections[id]?.WeaponDefinitionID === weaponId)).map(([role]) => role);
        const entry = coverage[weaponId] = { receiverId, roles, currentRoomRoles: roles.filter(role=>hostRoles.has(role)), suits:{} };
        if (!valid(receiverId) || receiverId !== scope?.ReceiverMain) {
            skipped.push([weaponId, 'unconfirmed-default']);
            continue;
        }
        const allowed = [];
        for (const selectedSuit of [...new Set(scope.SuitScope)].sort()) {
            const suitId = suitNames.get(selectedSuit.toLowerCase()) ?? selectedSuit;
            const suit = suits[suitId];
            if (!suit) { skipped.push([weaponId, suitId, 'missing-suit']); continue; }
            const override = slot(suit.PartSlotAndIDMap);
            const skin = slot(suit.PartSlotAndSkinIDMap).value;
            // 套装若指定机匣，须属于这把枪的 ReceiverMain 合法范围。
            const inherited = !!override.value && override.value !== scope.ReceiverMain;
            if (override.invalid || !valid(skin) ||
                (inherited && !coreAppearance(weapons, standard, hardcore, weapon, override.value, partSkins[skin]))) {
                skipped.push([weaponId, suitId, 'unconfirmed-suit-receiver']);
                continue;
            }
            const receiverPaints = {};
            for (const selectedPaint of [...new Set(suit.WeaponSuitPaintingIDArray)].sort()) {
                const paintId = paintNames.get(selectedPaint.toLowerCase()) ?? selectedPaint;
                const paint = slot(paints[paintId]?.SlotAndWeaponPartSkinPaintingIDMap).value;
                if (valid(paint)) { receiverPaints[paintId] = paint; usedPaints.add(paintId); }
                else skipped.push([weaponId, suitId, paintId, 'unconfirmed-paint']);
            }
            if (!Object.keys(receiverPaints).length) continue;
            const suitData = { skin, paints: receiverPaints };
            if (Object.hasOwn(catalog.suits, suitId)) assert.deepEqual(catalog.suits[suitId], suitData);
            else catalog.suits[suitId] = suitData;
            allowed.push(suitId);
            usedSuits.add(suitId);
            combinations += Object.keys(receiverPaints).length;
            entry.suits[suitId] = { receiverSource: inherited ? 'same-render-assets-variant-receiver' :
                override.value ? 'suit-explicit-matches-scope' : 'weapon-default', originalSuitPart: override.value ?? null,
                colors: Object.keys(receiverPaints).length };
        }
        if (allowed.length) catalog.weapons[weaponId] = { part: receiverId, suits: allowed };
    }
    const outOfScope = Object.keys(suits).filter(s=>!usedSuits.has(s));
    return { catalog, counts: { sourceWeapons: Object.keys(weapons).length, weapons: Object.keys(catalog.weapons).length,
        suits: usedSuits.size, combinations, retailSuitRows: Object.keys(suits).length, retailPaintRows: paintNames.size,
        paintingsReferencedByAnySuit: suitPaints.size, paintingsReferencedByAllowedSuit: usedPaints.size,
        hostRoleWeapons: Object.values(coverage).filter(r=>r.currentRoomRoles.length).length },
        coverage, outOfScope, skipped,
        unreferencedPaintRows: Object.keys(paints).filter(id=>!suitPaints.has(id)),
        suitPaintRowsOutsideWeapons: [...suitPaints].filter(id=>!usedPaints.has(id)),
        missingSuitPaintRows: Object.fromEntries(Object.entries(suits).flatMap(([id,s])=>{
            const missing=s.WeaponSuitPaintingIDArray.filter(p=>!paintNames.has(p.toLowerCase()));
            return missing.length ? [[id,missing]] : [];
        })) };
}
module.exports = { generate };
if (require.main === module) {
    const [server, content, mode, auditPath] = process.argv.slice(2);
    if (!server || !content || !['--check','--write'].includes(mode))
        throw Error('Usage: node generate-receiver-main.cjs <archive-server> <exported Content> --check|--write');
    const { catalog, ...audit } = generate(server, content);
    const target = path.join(__dirname, 'receiver-main.json');
    const data = JSON.stringify(catalog, null, 2) + '\n';
    if (mode === '--write') fs.writeFileSync(target, data);
    else assert.equal(fs.readFileSync(target, 'utf8').replaceAll('\r\n', '\n'), data);
    if (auditPath) fs.writeFileSync(auditPath, JSON.stringify(audit, null, 2) + '\n');
    console.log(JSON.stringify({ ...audit.counts, bytes: Buffer.byteLength(data), skipped: audit.skipped }));
}
