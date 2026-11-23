'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { fixture } = require('./fixture.cjs');
const { managed } = require('./fixture-kit.cjs');

const names = ['PEACE', 'PROBE', 'Sniper', 'FORT', 'SPIKE', 'FIXER'];
const slots = row => [row.PrimaryWeapon, row.SecondWeapon];

async function observe(source, retailRoleFile) {
    const factory = JSON.parse(fs.readFileSync(retailRoleFile, 'utf8'));
    const expected = Object.fromEntries(names.map(role => {
        const { FirstWeaponID, SecondWeaponID } = factory[role.toUpperCase()].explicit;
        return [role, [FirstWeaponID, SecondWeaponID]];
    }));
    return fixture(source, async f => {
        fs.cpSync(fs.realpathSync(path.join(source, 'node_modules')), path.join(f.dir, 'node_modules'), { recursive: true });
        const old = managed(f.root, f.dir, ['retail-role-weapons.patch']);
        const updatedRoot = fs.mkdtempSync(path.join(f.root, 'upgrade-'));
        const current = managed(updatedRoot, f.dir);
        assert.equal(old.loadoutDir, current.loadoutDir);
        const owner = 'factory_slot_old_profile';
        const ownerFile = path.join(old.loadoutDir, owner + '.json');
        const readAll = async client => Object.fromEntries((await client.archive(...names))
            .map(row => [row.RoleID, slots(row)]));
        await f.start(old.appDir);
        const oldClient = await f.login(owner);
        const previous = await readAll(oldClient);
        const mismatches = names.reduce((count, name) =>
            count + previous[name].filter((id, i) => id !== expected[name][i]).length, 0);
        assert.equal(mismatches, 6);
        // 同一份老档兼具显式选枪、明确卸下、仅选一侧、仅改过配件等形状。
        await oldClient.equip('Sniper', 1, 'SNIPER_GSW-DMR');
        await oldClient.equip('Sniper', 2, 'SNIPER_RU-MOSIN');
        await oldClient.equip('PEACE', 1, 'None');
        await oldClient.equip('FIXER', 2, 'FIXER_GSW-SMG');
        await oldClient.equip('FORT', 2, 'None');
        await oldClient.parts('PEACE', { WeaponId: 'PEACE_RU-AKM', Parts: [{ SlotId: 9, PartId: 'RU-AKM_MAG-AK-DRUM' }] });
        await f.stop();
        const originalBytes = fs.readFileSync(ownerFile);
        await f.start(current.appDir);
        const a = await f.login(owner);
        const fresh = await f.login('factory_slot_new_profile');
        const after = await readAll(fresh);
        assert.deepEqual(after, expected);
        const preserved = await readAll(a);
        assert.deepEqual(preserved, { ...expected, Sniper: ['SNIPER_GSW-DMR', 'SNIPER_RU-MOSIN'],
            PEACE: ['None', expected.PEACE[1]], FIXER: [expected.FIXER[0], 'FIXER_GSW-SMG'],
            FORT: [expected.FORT[0], 'None'] });
        const [peace] = await a.archive('PEACE');
        const parked = peace.WeaponConfig.Weapons.find(weapon => weapon.WeaponId === 'PEACE_RU-AKM');
        assert.equal(parked.Parts.find(slot => slot.SlotId === 9).PartId, 'RU-AKM_MAG-AK-DRUM');
        for (const name of ['Sniper', 'PEACE', 'FIXER', 'FORT']) {
            const rest = await f.http('GET', '/api/loadout/' + owner + '/' + name);
            assert.equal(rest.status, 200);
            assert.deepEqual([rest.body.primaryWeapon, rest.body.secondaryWeapon], preserved[name]);
        }
        assert.deepEqual(fs.readFileSync(ownerFile), originalBytes);
        return { previous, after, preserved, missingSlotsChanged: mismatches, oldPlayerBytesUnchanged: true };
    }, { roleSuccess: 200 });
}

module.exports = { observe };
if (require.main === module) observe(...process.argv.slice(2))
    .then(result => console.log(JSON.stringify(result)))
    .catch(error => { console.error(error); process.exitCode = 1; });
