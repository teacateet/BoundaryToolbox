'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const scopes = [
    'Muzzle', 'Barrel', 'HandGuard', 'ReceiverUpper', 'Grip', 'SightOptical',
    'Pointer', 'SightIron', 'AmmoStorageDevice', 'Stock',
];

// 只读已导出的原作属性。从武器定义的实际引用找到对应默认资产，包括同名的困难模式变体。
function readDefaults(server, properties) {
    const index = require(path.resolve(server, 'game/definitionIndex.js')).getDefinitionIndex();
    const table = JSON.parse(fs.readFileSync(path.join(server, 'game/definitions/DT_WeaponDefinition.json')))[0].Rows;
    const overrides = {};
    const all = {};
    for (const id of Object.keys(table).sort()) {
        const asset = table[id].DefaultConfig.AssetPathName.split('.')[0];
        assert.match(asset, /^\/Game\/(?:[A-Za-z0-9_-]+\/)+[A-Za-z0-9_-]+$/);
        const file = path.join(properties, asset.slice('/Game/'.length) + '.json');
        const entry = JSON.parse(fs.readFileSync(file))[0];
        assert.equal(entry.Type, 'PBWeaponDefaultConfig');
        assert.equal(entry.Name, path.basename(asset));
        const values = new Map(entry.Properties.EquippedPartIDMap.map(part =>
            [part.Key.split('::').pop().replaceAll('_', ''), part.Value]));
        const weapon = index.getWeapon(id);
        all[id] = {};
        for (const name of scopes) {
            const scope = weapon.slotScopes[name + 'Scope'];
            const part = values.get(name) || 'None';
            // 发行器固定了 definitions 的内容摘要，只需嵌入相对于这个版本的差异。
            const first = scope?.values().next().value || 'None';
            all[id][name] = part;
            if (part !== first) {
                assert.ok(part === 'None' || scope?.has(part), 'Factory part outside scope: ' + id + '/' + name);
                (overrides[id] ??= {})[name + 'Scope'] = part;
            }
        }
    }
    return { all, overrides };
}

function makePatch(overrides) {
    const declaration = 'const WEAPON_ARCHIVE_SLOT_SCOPES = [';
    const table = [
        '// 来自同版 PBWeaponDefaultConfig.EquippedPartIDMap，经出厂属性与受支持的可选表逐槽比较后生成。',
        'const RETAIL_WEAPON_DEFAULT_OVERRIDES = {',
        ...Object.entries(overrides).map(([id, values]) => '    ' + JSON.stringify(id) + ': ' + JSON.stringify(values) + ','),
        '};',
        '',
    ];
    return [
        '--- a/game/loadoutStore.js',
        '+++ b/game/loadoutStore.js',
        '@@ -33,1 +33,' + (table.length + 1) + ' @@',
        ...table.map(line => '+' + line),
        ' ' + declaration,
        '@@ -498,1 +498,2 @@',
        '-                const partId = this.firstFromSet(weaponDef.slotScopes[scopeName], 0);',
        '+                const partId = RETAIL_WEAPON_DEFAULT_OVERRIDES[baseWeaponId]?.[scopeName]',
        '+                    ?? this.firstFromSet(weaponDef.slotScopes[scopeName], 0);',
        '',
    ].join('\n');
}

module.exports = { readDefaults, makePatch, scopes };
if (require.main === module) {
    const [server, properties, mode] = process.argv.slice(2);
    if (!server || !properties || !['--check', '--write'].includes(mode)) {
        throw new Error('Usage: node generate-retail-weapon-defaults.cjs <metaserver> <exported Content> --check|--write');
    }
    const { all, overrides } = readDefaults(server, properties);
    const target = path.join(__dirname, 'retail-weapon-defaults.patch');
    const patch = makePatch(overrides);
    if (mode === '--write') fs.writeFileSync(target, patch);
    else assert.equal(fs.readFileSync(target, 'utf8').replaceAll('\r\n', '\n'), patch);
    console.log(JSON.stringify({ weapons: Object.keys(all).length, changed: Object.keys(overrides).length,
        slots: Object.values(overrides).reduce((sum, value) => sum + Object.keys(value).length, 0) }));
}
