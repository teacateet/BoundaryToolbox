'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { fixture } = require('./fixture.cjs');
const { managed } = require('./fixture-kit.cjs');

const lobbyRoles = ['PEACE', 'PROBE', 'Sniper', 'FORT', 'FIXER', 'SPIKE'];
const patchFile = path.join(__dirname, 'native-role-write.patch');
const patchBytes = fs.readFileSync(patchFile);
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const selectGun = (role, id) => role.WeaponConfig.Weapons.find(gun => gun.WeaponId === id);
const selectPart = (gun, slot) => gun.Parts.find(part => part.SlotId === slot)?.PartId;

function application(f, source, candidate) {
    fs.cpSync(fs.realpathSync(path.join(source, 'node_modules')), path.join(f.dir, 'node_modules'), { recursive: true });
    // 此组单独复现角色大小写写入；新装备写入器另由 native-equip.cjs 覆盖。
    const disabled = ['native-equip-appearance.patch'];
    if (!candidate) disabled.push('native-role-write.patch');
    const { appDir, loadoutDir } = managed(f.root, f.dir, disabled);
    const location = JSON.parse(fs.readFileSync(path.join(appDir, 'profile-location.json')));
    assert.equal(location.source, fs.realpathSync(f.dir));
    assert.equal(location.loadoutDir, path.join(location.source, 'data', 'loadouts'));
    assert.equal(loadoutDir, location.loadoutDir);
    assert.ok(!fs.existsSync(loadoutDir));
    const code = ['index.js', 'game/loadoutStore.js'].map(rel => fs.readFileSync(path.join(appDir, rel)));
    assert.equal(code[0].toString().split('store.getRoleForNativeUpdate(').length - 1, candidate ? 2 : 0);
    return { appDir, loadoutDir, hashes: { entry: sha(code[0]), store: sha(code[1]) } };
}

async function definition(f, resource) {
    const { status, body } = await f.http('GET', '/api/definitions/' + resource);
    assert.equal(status, 200);
    return body;
}

async function lobby(client) {
    const roles = await client.archive(...lobbyRoles);
    assert.deepEqual(roles.map(role => role.RoleID), lobbyRoles);
    return roles;
}

// 原生改枪请求提交整把枪的槽位。保留其余槽位，以独立响应协议检查实际变更。
function editPart(role, weaponId, slotId, allowed) {
    const gun = selectGun(role, weaponId);
    assert.ok(gun);
    const old = selectPart(gun, slotId);
    const next = allowed.find(id => id && id !== 'None' && id !== old);
    assert.ok(next && old);
    const parts = gun.Parts.map(part => ({ SlotId: part.SlotId, PartId: part.SlotId === slotId ? next : part.PartId }));
    return { request: { WeaponId: weaponId, Parts: parts }, slotId, next, old };
}

function changed(role, edit) {
    return selectPart(selectGun(role, edit.request.WeaponId), edit.slotId) === edit.next;
}

async function sameCase(f, client) {
    const [base] = await client.archive('PEACE');
    const primary = 'PEACE_GSW-AR', secondary = 'PEACE_RU-AKM';
    const { weaponScope, podScope } = await definition(f, 'roles/PEACE');
    assert.ok(weaponScope.includes(primary) && weaponScope.includes(secondary));
    const left = podScope.find(id => id && id !== 'None' && id !== base.LeftPylon);
    const right = podScope.find(id => id && id !== 'None' && id !== left && id !== base.RightPylon);
    assert.ok(left && right);
    const primaryEdit = editPart(base, primary, 9, (await definition(f, 'weapons/GSW-AR')).slotScopes.AmmoStorageDeviceScope);
    const secondaryEdit = editPart(base, secondary, 9, (await definition(f, 'weapons/RU-AKM')).slotScopes.AmmoStorageDeviceScope);
    await client.equip('PEACE', 1, primary);
    await client.equip('PEACE', 2, secondary);
    await client.equip('PEACE', 5, left);
    await client.equip('PEACE', 6, right);
    await client.parts('PEACE', primaryEdit.request);
    await client.parts('PEACE', secondaryEdit.request);
    const [both] = await lobby(client);
    assert.deepEqual([both.PrimaryWeapon, both.SecondWeapon, both.LeftPylon, both.RightPylon], [primary, secondary, left, right]);
    assert.ok(changed(both, primaryEdit) && changed(both, secondaryEdit));
    await client.equip('PEACE', 5, '');
    const [clear] = await lobby(client);
    assert.deepEqual([clear.LeftPylon, clear.RightPylon], ['None', right]);
    assert.deepEqual(clear.WeaponConfig, both.WeaponConfig);
    return { both: [left, right], cleared: [clear.LeftPylon, clear.RightPylon],
        weapons: [primary, secondary], magazines: [primaryEdit.next, secondaryEdit.next] };
}

async function casePair(f, client, first, second, candidate) {
    const [base] = await client.archive(first);
    const { weaponScope, podScope } = await definition(f, 'roles/Sniper');
    const mosin = 'SNIPER_RU-MOSIN';
    assert.ok(weaponScope.includes(mosin));
    const [left, right] = podScope.filter(id => id !== 'None' && id !== base.LeftPylon && id !== base.RightPylon);
    assert.ok(right && left && right !== left);
    await client.equip(first, 2, mosin);
    await client.equip(first, 5, left);
    const [equipped] = await client.archive(first);
    assert.deepEqual([equipped.SecondWeapon, equipped.LeftPylon], [mosin, left]);
    const edit = editPart(equipped, mosin, 6, (await definition(f, 'weapons/RU-MOSIN')).slotScopes.SightOpticalScope);
    await client.parts(second, edit.request);
    const [afterWeapon] = await client.archive(first);
    assert.equal(changed(afterWeapon, edit), candidate, 'alternate-case weapon update');
    await client.equip(second, 6, right);
    const [a, b] = await client.archive(first, second);
    assert.deepEqual([a.RoleID, b.RoleID], [first, second]);
    for (const role of [a, b]) {
        const combined = role.SecondWeapon === mosin && role.LeftPylon === left && role.RightPylon === right && changed(role, edit);
        assert.equal(combined, candidate, 'both spellings read the same native edits');
    }
    assert.deepEqual([a.LeftPylon, b.RightPylon], [left, right]);
    assert.equal(a.RightPylon, candidate ? right : base.RightPylon);
    assert.equal(b.LeftPylon, candidate ? left : base.LeftPylon);
    const six = await lobby(client);
    assert.deepEqual(six[2], first === 'Sniper' ? a : b);
    return { first, second, visibleAfterWeapon: changed(afterWeapon, edit), target: [left, right, edit.next],
        returned: [a, b].map(role => [role.LeftPylon, role.RightPylon, selectPart(selectGun(role, mosin), 6)]) };
}

async function sample(source, candidate) {
    return fixture(source, async f => {
        const { appDir, loadoutDir, hashes } = application(f, source, candidate);
        const profileA = 'native_lobby_fixture_A', profileB = 'native_lobby_fixture_B';
        const file = id => path.join(loadoutDir, id + '.json');
        const disk = id => fs.readFileSync(file(id));
        await f.start(appDir);
        const a = await f.login(profileA);
        const initial = await lobby(a);
        const same = await sameCase(f, a);
        const pairs = [await casePair(f, a, 'Sniper', 'SNIPER', candidate)];
        const savedA = disk(profileA);
        const roleKeys = Object.keys(JSON.parse(savedA).roles).sort();
        assert.deepEqual(roleKeys, (candidate ? ['PEACE', 'Sniper'] : ['PEACE', 'Sniper', 'SNIPER']).sort());
        const beforeRestart = await lobby(a);
        const b = await f.login(profileB);
        assert.deepEqual(await lobby(b), initial);
        pairs.push(await casePair(f, b, 'SNIPER', 'Sniper', candidate));
        const savedB = disk(profileB);
        assert.deepEqual(disk(profileA), savedA);
        const fresh = await f.login(profileA);
        assert.deepEqual(await lobby(fresh), beforeRestart);
        await f.stop();
        await f.start(appDir);
        const reopened = await f.login(profileA);
        assert.deepEqual(await lobby(reopened), beforeRestart);
        assert.deepEqual(disk(profileA), savedA);
        assert.deepEqual(disk(profileB), savedB);
        return { receipt: { hashes, same, pairs, roleKeys, isolatedAccounts: true, sameAfterLoginAndRestart: true },
            savedA };
    });
}

async function legacySplit(source, savedA) {
    return fixture(source, async f => {
        const { appDir, loadoutDir } = application(f, source, true);
        const player = 'native_lobby_fixture_A';
        const file = path.join(loadoutDir, player + '.json');
        // 原样使用对照组两种角色名经原生 RPC 生成的合成旧档。
        fs.mkdirSync(loadoutDir, { recursive: true });
        fs.writeFileSync(file, savedA, { flag: 'wx' });
        await f.start(appDir);
        const client = await f.login(player);
        const [title, upper, ambiguous] = await client.archive('Sniper', 'SNIPER', 'sNiPeR');
        const empty = await f.login('native_lobby_fixture_empty');
        const [defaults] = await empty.archive('sNiPeR');
        assert.deepEqual(ambiguous, defaults);
        assert.deepEqual(fs.readFileSync(file), savedA);
        assert.notEqual(title.LeftPylon, upper.LeftPylon);
        assert.notEqual(title.RightPylon, upper.RightPylon);
        await client.equip('Sniper', 5, '');
        const [changedTitle, intactUpper] = await client.archive('Sniper', 'SNIPER');
        assert.equal(changedTitle.LeftPylon, 'None');
        assert.deepEqual(intactUpper, upper);
        const edit = editPart(upper, 'SNIPER_RU-MOSIN', 6,
            (await definition(f, 'weapons/RU-MOSIN')).slotScopes.SightOpticalScope);
        await client.parts('SNIPER', edit.request);
        const [intactTitle, changedUpper] = await client.archive('Sniper', 'SNIPER');
        assert.deepEqual(intactTitle, changedTitle);
        assert.ok(changed(changedUpper, edit));
        assert.deepEqual(Object.keys(JSON.parse(fs.readFileSync(file)).roles).sort(), ['PEACE', 'SNIPER', 'Sniper']);
        return { exactWritesIndependent: true, ambiguousReadUsesDefaults: true, readsPreserveOriginalBytes: true };
    });
}

async function main() {
    const [source, option] = process.argv.slice(2);
    assert.ok(source && (!option || option === '--receipt'), 'Usage: node native-lobby.cjs <server-source> [--receipt]');
    const { receipt: control, savedA } = await sample(source, false);
    const { receipt: fixed } = await sample(source, true);
    assert.deepEqual(control.same, fixed.same);
    const legacy = await legacySplit(source, savedA);
    const result = { status: 'PASS', node: process.version, patchSha256: sha(patchBytes), control, fixed, legacy };
    const json = JSON.stringify(result, null, 2) + '\n';
    if (option) fs.writeFileSync(path.join(__dirname, 'native-lobby-proof.json'), json);
    process.stdout.write(json);
}

main().catch(error => { console.error(error); process.exitCode = 1; });
