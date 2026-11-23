'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { fixture } = require('./fixture.cjs');
const { managed } = require('./fixture-kit.cjs');

async function verifyProbeSecondary(source) {
    return fixture(source, async f => {
        fs.cpSync(fs.realpathSync(path.join(source, 'node_modules')), path.join(f.dir, 'node_modules'), { recursive: true });
        const { appDir, loadoutDir } = managed(f.root, f.dir);
        assert.equal(loadoutDir, path.join(fs.realpathSync(f.dir), 'data', 'loadouts'));
        const owner = 'fixture_probe_secondary', file = path.join(loadoutDir, owner + '.json');
        const dmr = 'PROBE_GSW-DMR', ar = 'PROBE_GSW-AR', fop = 'PROBE_GSW-FOP', drum = 'GSW-AR_MAG-AR-DRUM';
        await f.start(appDir);
        let client = await f.login(owner);
        // 单独改装备选枪，不代表把它装进副槽；使用正常 RPC 生成仅有逐枪数据的旧档形状。
        await client.parts('PROBE', { WeaponId: ar, Parts: [{ SlotId: 9, PartId: drum }] });
        const before = fs.readFileSync(file);
        assert.equal(Object.hasOwn(JSON.parse(before).roles.PROBE, 'secondaryWeapon'), false);
        async function check(expected, spelling = 'PROBE') {
            const [row] = await client.archive(spelling);
            assert.equal(row.SecondWeapon, expected);
            assert.equal(row.PrimaryWeapon, dmr);
            const gun = row.WeaponConfig.Weapons.find(w => w.WeaponId === ar);
            assert.equal(gun?.Parts.find(p => p.SlotId === 9)?.PartId, drum);
            const route = '/api/loadout/' + owner;
            const full = await f.http('GET', route), single = await f.http('GET', route + '/' + spelling);
            assert.equal(full.status, 200);
            assert.equal(single.status, 200);
            for (const view of [full.body.roles.PROBE, single.body]) {
                assert.equal(view.secondaryWeapon, expected);
                assert.equal(view.weaponConfigs[ar].parts.find(p => p.slotType === 9)?.weaponPartId, drum);
            }
        }
        await check(fop);
        await check(fop, 'Probe');
        assert.deepEqual(fs.readFileSync(file), before);
        const [peace] = await client.archive('PEACE');
        assert.equal(peace.SecondWeapon, 'PEACE_RU-APS');
        await client.equip('PROBE', 2, ar);
        await check(ar);
        await f.stop();
        await f.start(appDir);
        client = await f.login(owner);
        await check(ar);
        await client.equip('PROBE', 2, 'None');
        await check('None');
        const peer = await f.login('fixture_probe_secondary_peer');
        assert.equal((await peer.archive('PROBE'))[0].SecondWeapon, fop);
        return { missing: fop, equippedAfterRestart: ar, cleared: 'None', retainedPart: drum };
    }, { roleSuccess: 200 });
}

module.exports = { verifyProbeSecondary };
if (require.main === module) verifyProbeSecondary(process.argv[2])
    .then(result => console.log(JSON.stringify(result))).catch(err => { console.error(err); process.exitCode = 1; });
