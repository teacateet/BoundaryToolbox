'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { fixture } = require('./fixture.cjs');
const { verifyProbeSecondary } = require('./probe-secondary.cjs');
const { deploy } = require('./deploy.cjs');

const player = 'toolbox_fixture_A';
const role = 'PEACE';
const primary = 'PEACE_GSW-AR';
const secondary = 'PEACE_RU-AKM';
const extended = 'GSW-AR_MAG-AR-EXT';
const drum = 'RU-AKM_MAG-AK-DRUM';
const route = '/api/loadout/' + player;
const gun = (view, id) => view.WeaponConfig.Weapons.find(weapon => weapon.WeaponId === id);
const nativePart = (weapon, slot) => weapon?.Parts.find(part => part.SlotId === slot)?.PartId;
const viewPart = (view, id, slot) => view.weaponConfigs?.[id]?.parts.find(part => Number(part.slotType) === slot)?.weaponPartId;
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
async function get(f, url) {
    const response = await f.http('GET', url);
    assert.equal(response.status, 200);
    return response.body;
}
async function put(f, url, roles) {
    const result = await f.http('PUT', url, { roles });
    assert.equal(result.status, 200, JSON.stringify(result.body));
    assert.deepEqual(result.body.validation, { valid: true, errors: [], warnings: [] });
}

async function sample(source, candidate) {
    return fixture(source, async f => {
        if (candidate) assert.equal(deploy('apply', f.dir).state, 'installed');
        await f.start();
        const client = await f.login(player);
        const definitions = await get(f, '/api/definitions/roles/' + role);
        const pods = definitions.podScope.filter(id => id && id !== 'None');
        assert.ok(pods.length >= 2);
        const weaponDefinition = await get(f, '/api/definitions/weapons/GSW-AR');
        const otherMag = weaponDefinition.slotScopes.AmmoStorageDeviceScope.find(id => id && id !== 'None' && id !== extended);
        assert.ok(otherMag && weaponDefinition.slotScopes.AmmoStorageDeviceScope.includes(extended));
        const optic = weaponDefinition.slotScopes.SightOpticalScope.find(id => id && id !== 'None');
        assert.ok(optic);
        const weaponConfigs = {
            [secondary]: { parts: [{ slotType: 9, weaponPartId: drum }] },
            [primary]: { parts: [{ slotType: 9, weaponPartId: extended }] },
        };
        const input = { primaryWeapon: primary, secondaryWeapon: secondary, weaponConfigs,
            leftLauncher: { id: pods[0] }, rightLauncher: { id: pods[1] },
            inventory: { slots: [{ slotType: 'LeftPod', itemId: pods[0] }, { slotType: 4, itemId: pods[1] }] },
            characterData: { fixtureLabel: 'A' } };
        await put(f, route, { [role]: input });
        const [first] = await client.archive(role);
        await client.equip(role, 5, '');
        await client.equip(role, 6, pods[0]);
        const skin = gun(first, primary).Skin;
        await client.parts(role, { WeaponId: primary, Parts: [{ SlotId: 9, PartId: otherMag }, { SlotId: 6, PartId: optic }],
            Skin: { SkinInfo: { Type: skin.SuitData.SkinItem, Id: skin.SuitData.PaintItem },
                WeaponOrnament: skin.Ornament } });
        const [next] = await client.archive(role);
        const single = await get(f, route + '/' + role);
        const full = (await get(f, route)).roles[role];
        assert.equal(next.LeftPylon, 'None');
        assert.equal(next.RightPylon, pods[0]);
        assert.equal(nativePart(gun(next, primary), 9), otherMag);
        const observations = {
            explicitWeaponOrder: first.PrimaryWeapon === primary && first.SecondWeapon === secondary,
            nativePrimaryPart: nativePart(gun(first, primary), 9) === extended,
            nativeSecondaryPart: nativePart(gun(first, secondary), 9) === drum,
            roleLeftUnequipped: single.leftLauncher.id === next.LeftPylon,
            fullRightEquipped: full.rightLauncher.id === next.RightPylon,
            nativeEditInBothRestReads: [single, full].every(view => viewPart(view, primary, 9) === otherMag),
            inventoryLeftAndRight: single.inventory.slots[0].itemId === 'None' && full.inventory.slots[1].itemId === pods[0],
        };
        for (const [name, value] of Object.entries(observations)) assert.equal(value, candidate, name);
        const readings = { firstPrimary: first.PrimaryWeapon, firstSecondary: first.SecondWeapon,
            firstPrimaryMag: nativePart(gun(first, primary), 9), firstSecondaryMag: nativePart(gun(first, secondary), 9),
            nextNativeMag: otherMag, nextRestMag: viewPart(single, primary, 9),
            nextNativePods: [next.LeftPylon, next.RightPylon], nextRestPods: [single.leftLauncher.id, full.rightLauncher.id] };
        if (!candidate) return { observations, readings };

        const checks = [];
        function checked(name, callback) { callback(); checks.push(name); }
        const disk = path.join(f.dir, 'data/loadouts', player + '.json');
        const previousSecondaryArchive = single._weaponArchives[secondary];
        await client.equip(role, 1, secondary);
        await client.equip(role, 2, primary);
        const switched = (await get(f, route)).roles[role];
        checked('native equip operations 1 and 2 select the same weapons in whole REST', () => {
            assert.equal(switched.primaryWeapon, secondary);
            assert.equal(switched.secondaryWeapon, primary);
            assert.equal(switched._weaponArchiveRaw, previousSecondaryArchive);
        });
        await put(f, route, { [role]: { loadoutSnapshot: { weaponConfigs: {
            [primary]: { parts: [{ slotType: 'EPBPartSlotType::AmmoStorageDevice', weaponPartId: extended }] },
        } } } });
        await put(f, route, { [role]: { leftPylon: pods[1] } });
        const [merged] = await client.archive(role);
        const final = await get(f, route + '/' + role);
        checked('partial structured part and flat pylon writes preserve other slots and snapshot fields', () => {
            assert.equal(merged.PrimaryWeapon, secondary);
            assert.equal(merged.SecondWeapon, primary);
            assert.equal(nativePart(gun(merged, primary), 9), extended);
            assert.equal(nativePart(gun(merged, primary), 6), optic);
            assert.equal(nativePart(gun(merged, secondary), 9), drum);
            assert.equal(merged.LeftPylon, pods[1]);
            assert.equal(merged.RightPylon, pods[0]);
            assert.deepEqual(final.characterData, input.characterData);
            assert.deepEqual(final.inventory.slots.map(slot => slot.itemId), [pods[1], pods[0]]);
            assert.equal(final._weaponArchives[secondary], previousSecondaryArchive);
            assert.deepEqual(gun(merged, primary).Skin, skin);
        });
        const diskBeforeRead = digest(fs.readFileSync(disk));
        await get(f, route);
        await get(f, route + '/' + role);
        await client.archive(role);
        checked('reads leave isolated JSON byte-identical', () => assert.equal(digest(fs.readFileSync(disk)), diskBeforeRead));
        const otherRole = (await get(f, '/api/definitions/roles')).roles.find(id => id !== role);
        const otherBefore = await get(f, route + '/' + otherRole);
        const invalid = await f.http('PUT', route, { roles: {
            [otherRole]: { leftPylon: 'None' }, [role]: { weaponConfigs: { [primary]: { parts: [{ slotType: 1, weaponPartId: extended }] } } },
        } });
        const otherAfter = await get(f, route + '/' + otherRole);
        checked('rejected wrong-slot part changes neither cache nor disk for either role', () => {
            assert.equal(invalid.status, 400);
            assert.deepEqual(otherAfter, otherBefore);
            assert.equal(digest(fs.readFileSync(disk)), diskBeforeRead);
        });
        const isolated = await f.login('toolbox_fixture_B');
        const otherRoute = '/api/loadout/toolbox_fixture_B';
        const [defaults] = await isolated.archive(role);
        await put(f, otherRoute, { [role]: { weaponConfigs: { [primary]: { parts: [
            { slotType: 'SightOptical', weaponPartId: optic },
        ] } } } });
        const [partialOnly] = await isolated.archive(role);
        checked('editing one parked weapon on a fresh profile preserves the default equipped pair', () => {
            assert.equal(partialOnly.PrimaryWeapon, defaults.PrimaryWeapon);
            assert.equal(partialOnly.SecondWeapon, defaults.SecondWeapon);
            assert.equal(nativePart(gun(partialOnly, primary), 6), optic);
        });
        await isolated.equip(role, 6, '');
        const [secondPlayer] = await isolated.archive(role);
        checked('second player writes an independent save', () => {
            assert.equal(secondPlayer.RightPylon, 'None');
            assert.equal(digest(fs.readFileSync(disk)), diskBeforeRead);
            assert.ok(fs.existsSync(path.join(path.dirname(disk), 'toolbox_fixture_B.json')));
        });
        await f.stop();
        await f.start();
        const fresh = await f.login(player);
        const [reopened] = await fresh.archive(role);
        const reopenedSingle = await get(f, route + '/' + role);
        const reopenedFull = (await get(f, route)).roles[role];
        checked('fresh process and new gate session return identical native and both REST views', () => {
            assert.deepEqual(reopened, merged);
            assert.deepEqual(reopenedSingle, final);
            assert.deepEqual(reopenedFull, final);
            assert.equal(digest(fs.readFileSync(disk)), diskBeforeRead);
        });
        await fresh.equip(role, 1, '');
        const cleared = await get(f, route + '/' + role);
        await put(f, route, { [role]: cleared });
        const [roundTrip] = await fresh.archive(role);
        checked('GET to PUT retains explicit unequip and parked per-weapon bytes', () => {
            assert.equal(roundTrip.PrimaryWeapon, 'None');
            assert.equal(roundTrip.SecondWeapon, primary);
            assert.equal(nativePart(gun(roundTrip, secondary), 9), drum);
            assert.equal(cleared._weaponArchiveRaw, '');
        });
        return { observations, readings, checks, fixtureDiskSha256: diskBeforeRead };
    });
}

async function upgrade(source) {
    return fixture(source, async f => {
        await f.start();
        await put(f, route, { [role]: { weaponConfigs: {
            [primary]: { parts: [{ slotType: 9, weaponPartId: extended }] },
            [secondary]: { parts: [{ slotType: 9, weaponPartId: drum }] },
        } } });
        await f.stop();
        const disk = path.join(f.dir, 'data/loadouts', player + '.json');
        const oldBytes = fs.readFileSync(disk);
        const store = path.join(f.dir, 'game/loadoutStore.js');
        const original = fs.readFileSync(store);
        const installed = deploy('apply', f.dir);
        assert.deepEqual(deploy('apply', f.dir), installed);
        assert.deepEqual(fs.readFileSync(disk), oldBytes);
        await f.start();
        const client = await f.login(player);
        const [before] = await client.archive(role);
        const legacy = await get(f, route + '/' + role);
        assert.equal(nativePart(gun(before, primary), 9), extended);
        assert.equal(nativePart(gun(before, secondary), 9), drum);
        assert.equal(viewPart(legacy, primary, 9), extended);
        assert.deepEqual(fs.readFileSync(disk), oldBytes);
        await client.equip(role, 1, primary);
        const [after] = await client.archive(role);
        assert.equal(nativePart(gun(after, primary), 9), extended);
        assert.equal(nativePart(gun(after, secondary), 9), drum);
        await f.stop();
        const savedBytes = fs.readFileSync(disk);
        assert.equal(deploy('rollback', f.dir).state, 'ready');
        assert.equal(deploy('rollback', f.dir).state, 'ready');
        assert.deepEqual(fs.readFileSync(store), original);
        assert.deepEqual(fs.readFileSync(disk), savedBytes);
        fs.appendFileSync(store, '\n// fixture for an independently updated service\n');
        const drifted = fs.readFileSync(store);
        assert.equal(deploy('status', f.dir).state, 'unsupported');
        assert.throws(() => deploy('apply', f.dir), /differs/);
        assert.throws(() => deploy('rollback', f.dir), /differs/);
        assert.deepEqual(fs.readFileSync(store), drifted);
        return { legacyNativeRead: true, legacyReadIsByteIdentical: true, legacyReequip: true,
            idempotentInstallAndRollback: true, restoredStoreSha256: digest(original), isolatedSaveSurvivesRollback: true,
            driftedCodePreserved: true };
    });
}

async function main() {
    const source = process.argv[2];
    const flag = process.argv[3];
    if (!source || (flag && flag !== '--receipt')) throw new Error('Usage: node verify.cjs <metaserver-directory> [--receipt]');
    const baseline = await sample(source, false);
    const repaired = await sample(source, true);
    const deployment = await upgrade(source);
    const probeSecondary = await verifyProbeSecondary(source);
    const result = { status: 'PASS', utc: new Date().toISOString(), node: process.version, source: path.resolve(source),
        service: deploy('status', source), baseline, repaired, deployment, probeSecondary };
    if (flag) fs.writeFileSync(path.join(__dirname, 'verification.json'), JSON.stringify(result, null, 2) + '\n');
    console.log(JSON.stringify(result, null, 2));
}
if (require.main === module) main().catch(error => { console.error(error.stack); process.exitCode = 1; });
