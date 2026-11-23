'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { fixture } = require('./fixture.cjs');
const { managed } = require('./fixture-kit.cjs');

const dmr = 'PEACE_GSW-DMR', tk47 = 'PEACE_RU-AKM';
const sight3 = 'GSW-DMR_SGT-REFLEX-3X', sight4 = 'GSW-DMR_SGT-ACOG-4X';
const mag30 = 'RU-AKM_MAG-AK-STD', mag75 = 'RU-AKM_MAG-AK-DRUM';

async function readings(f, client, owner) {
    const [role] = await client.archive('PEACE');
    const rest = await f.http('GET', '/api/loadout/' + owner + '/PEACE');
    assert.equal(rest.status, 200);
    function part(weapon, slot) {
        const rpc = role.WeaponConfig.Weapons.find(row => row.WeaponId === weapon);
        const nativePart = rpc.Parts.find(row => row.SlotId === slot).PartId;
        const view = rest.body.weaponConfigs[weapon].parts.find(row => Number(row.slotType) === slot);
        assert.equal(view.weaponPartId, nativePart);
        return nativePart;
    }
    assert.equal(rest.body.primaryWeapon, role.PrimaryWeapon);
    assert.equal(rest.body.secondaryWeapon, role.SecondWeapon);
    return { main: role.PrimaryWeapon, secondary: role.SecondWeapon, sight: part(dmr, 6), magazine: part(tk47, 9) };
}

async function exercise(source, enabled, assets) {
    return fixture(source, async f => {
        fs.cpSync(fs.realpathSync(path.join(source, 'node_modules')), path.join(f.dir, 'node_modules'), { recursive: true });
        const { appDir } = managed(f.root, f.dir, enabled ? [] : ['retail-weapon-defaults.patch']);
        let catalog;
        if (assets) {
            const { readDefaults, scopes } = require('./generate-retail-weapon-defaults.cjs');
            const { all } = readDefaults(appDir, assets);
            const store = require(path.join(appDir, 'game/loadoutStore.js')).getLoadoutStore();
            let slotDifferences = 0;
            const affected = [];
            // 覆盖全部基础枪和困难变体的默认 protobuf。用于一起核对的槽位只取游戏协议所携带的十个。
            for (const [id, expected] of Object.entries(all)) {
                const raw = store.buildDefaultWeaponArchiveRaw('PEACE', id);
                const { WeaponArchive } = f.weaponUpdate.decode(Buffer.from(raw, 'hex'));
                assert.equal(WeaponArchive.WeaponId, id);
                assert.equal(WeaponArchive.Parts.length, scopes.length);
                const diffs = WeaponArchive.Parts.filter(part =>
                    (part.PartId || 'None') !== expected[scopes[part.SlotId - 1]]);
                if (diffs.length) affected.push(id);
                slotDifferences += diffs.length;
            }
            catalog = { weapons: Object.keys(all).length, affected, slotDifferences };
            assert.equal(catalog.weapons, 46);
            assert.equal(slotDifferences, enabled ? 0 : 17);
        }
        await f.start(appDir);
        const owner = 'fixture_factory_parts', peer = 'fixture_factory_peer';
        let a = await f.login(owner);
        await a.equip('PEACE', 1, dmr);
        await a.equip('PEACE', 2, tk47);
        const initial = await readings(f, a, owner);
        assert.deepEqual(initial, { main: dmr, secondary: tk47, sight: enabled ? sight3 : sight4, magazine: mag30 });

        // 已是原版标准配件的武器只改玩家点到的槽。把枪对调后继续按武器身份读取。
        await a.parts('PEACE', { WeaponId: tk47, Parts: [{ SlotId: 9, PartId: mag75 }] });
        await a.parts('PEACE', { WeaponId: dmr, Parts: [{ SlotId: 6, PartId: sight4 }] });
        await a.equip('PEACE', 1, tk47);
        await a.equip('PEACE', 2, dmr);
        const chosen = { main: tk47, secondary: dmr, sight: sight4, magazine: mag75 };
        assert.deepEqual(await readings(f, a, owner), chosen);
        await f.stop();
        await f.start(appDir);
        a = await f.login(owner);
        const persisted = await readings(f, a, owner);
        assert.deepEqual(persisted, chosen);
        const b = await f.login(peer);
        await b.equip('PEACE', 1, tk47);
        await b.equip('PEACE', 2, dmr);
        const separate = await readings(f, b, peer);
        assert.deepEqual(separate, { ...chosen, sight: enabled ? sight3 : sight4, magazine: mag30 });
        return { initial, persisted, separate, catalog };
    }, { roleSuccess: 200 });
}

module.exports = { exercise };
if (require.main === module) {
    const [source, mode, assets] = process.argv.slice(2);
    if (!source || !['baseline', 'candidate'].includes(mode)) {
        throw new Error('Usage: node retail-weapon-defaults.cjs <metaserver> baseline|candidate [exported Content]');
    }
    exercise(path.resolve(source), mode === 'candidate', assets).then(result => console.log(JSON.stringify(result)))
        .catch(error => { console.error(error); process.exitCode = 1; });
}
