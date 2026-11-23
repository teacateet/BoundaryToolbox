'use strict';

const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { fixture } = require('./fixture.cjs');
const { managed, extractRelease } = require('./fixture-kit.cjs');

const patch = 'native-equip-appearance.patch';
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const roles = ['PEACE', 'PROBE', 'Sniper', 'FORT', 'FIXER', 'SPIKE'];
const gun = (role, id) => role.WeaponConfig.Weapons.find(w => w.WeaponId === id);
const equipment = row => [row.PrimaryWeapon, row.SecondWeapon];
const look = row => [row.SkinConfig.SuitData.SkinItem, row.SkinConfig.SuitData.PaintItem,
    row.SkinConfig.SpecialSlot, row.SkinConfig.Ornament];
const gold = { SkinInfo: { Type: 'SkinDMROriginal', Id: 'GSW-DMR_Original_PTMetalGold' }, WeaponOrnament: 'WO-SUN' };
const tiger = { SkinItem: 'PEACE_TIGER', PaintItem: 'PEACE_TIGER_PTTigerSergeyev' };
const probeGold = { SkinItem: 'PROBE_ORIGINAL', PaintItem: 'PROBE_ORIGINAL_PTMetalGold' };

function sourceFor(f, source, origin) {
    if (origin === 'release') return extractRelease(f.root, process.env.PROFILE_TEST_RELEASE);
    if (origin === 'main') {
        const main = process.env.PROFILE_TEST_MAIN || 'C:/BrtWork/SDK/BoundaryMetaServer-main';
        const dir = path.join(f.root, 'main');
        fs.mkdirSync(path.join(dir, 'game'), { recursive: true });
        for (const rel of ['index.js', 'game/proto', 'game/definitions']) {
            fs.cpSync(path.join(main, rel), path.join(dir, rel), { recursive: true });
        }
        fs.cpSync(fs.realpathSync(process.env.PROFILE_TEST_MAIN_MODULES || path.join(main, 'node_modules')),
            path.join(dir, 'node_modules'), { recursive: true });
        return dir;
    }
    fs.cpSync(fs.realpathSync(path.join(source, 'node_modules')), path.join(f.dir, 'node_modules'), { recursive: true });
    return f.dir;
}

async function scenario(source, origin, fixed) {
    const roleSuccess = fixed ? 200 : 0;
    return fixture(source, async f => {
        const selected = sourceFor(f, source, origin);
        const { appDir, loadoutDir } = managed(f.root, selected, fixed ? [] : [patch]);
        assert.ok(!fs.existsSync(loadoutDir));
        const store = fs.readFileSync(path.join(appDir, 'game/loadoutStore.js'));
        assert.equal(store.includes('nativeWeaponId(roleId, id)'), fixed);
        const account = 'fixture_armory_identity', peerId = 'fixture_armory_peer';
        const file = path.join(loadoutDir, account + '.json');
        await f.start(appDir);
        const a = await f.login(account);
        const initial = await a.archive(...roles);
        const [peace, probe] = initial;
        const ids = ['PEACE_RU-SVD', 'PEACE_GSW-DMR'];
        const [roleDef, partDef] = await Promise.all(['roles/PEACE', 'weapons/GSW-DMR'].map(async resource => {
            const { status, body } = await f.http('GET', '/api/definitions/' + resource);
            assert.equal(status, 200);
            return body;
        }));
        assert.ok(ids.every(id => roleDef.weaponScope.includes(id) && !equipment(peace).includes(id)));
        assert.ok(partDef.slotScopes.OrnamentScope.includes(gold.WeaponOrnament));
        assert.ok(roleDef.spaceSuitSkinScope.includes(tiger.SkinItem));
        // 历史大厅记录确实提交了带前缀的武器 ID。主副换位不能靠武器表顺序实现。
        await a.equip('PEACE', 1, ids[0]);
        await a.equip('PEACE', 2, ids[1]);
        const [scoped] = await a.archive('PEACE');
        assert.deepEqual(equipment(scoped), ids);
        // 换成同两把枪的基础定义 ID，但指定相反的主副槽。
        await a.equip('PEACE', 1, 'GSW-DMR');
        await a.equip('PEACE', 2, 'RU-SVD');
        const [byBase] = await a.archive('PEACE');
        assert.deepEqual(equipment(byBase), fixed ? [ids[1], ids[0]] : ids);
        assert.deepEqual([byBase.LeftPylon, byBase.RightPylon], [peace.LeftPylon, peace.RightPylon]);
        const makeWeapon = (role, id, wireId) => ({ WeaponId: wireId,
            Parts: gun(role, id).Parts.map(p => ({ SlotId: p.SlotId, PartId: p.PartId })), Skin: gold });
        await a.parts('PEACE', makeWeapon(scoped, ids[1], 'GSW-DMR'));
        await a.parts('PROBE', makeWeapon(probe, 'PROBE_GSW-DMR', 'PROBE_GSW-DMR'));
        const [painted, paintedProbe] = await a.archive('PEACE', 'PROBE');
        assert.equal(gun(painted, ids[1]).Skin.Ornament === 'WO-SUN', fixed);
        assert.deepEqual(gun(painted, ids[0]), gun(scoped, ids[0]));
        assert.equal(gun(paintedProbe, 'PROBE_GSW-DMR').Skin.SuitData.PaintItem, gold.SkinInfo.Id);
        assert.equal(gun(paintedProbe, 'PROBE_GSW-DMR').Skin.Ornament, 'WO-SUN');
        assert.deepEqual(equipment(paintedProbe), equipment(probe));

        assert.equal(await a.roleStatus('PEACE', 7, tiger), roleSuccess);
        assert.equal(await a.roleStatus('PROBE', 7, probeGold), roleSuccess);
        await a.equip('PROBE', 10, 'HOGoatHorn');
        await a.equip('PROBE', 9, 'ABGNoEntry');
        const [actor, accessories] = await a.archive('PEACE', 'PROBE');
        assert.deepEqual(look(actor), [tiger.SkinItem, tiger.PaintItem, '', '']);
        assert.deepEqual(look(accessories), [probeGold.SkinItem, probeGold.PaintItem,
            fixed ? 'ABGNoEntry' : '', fixed ? 'HOGoatHorn' : '']);
        if (fixed) {
            assert.equal(await a.roleStatus('PEACE', 7, { SkinItem: tiger.SkinItem }), 200);
            assert.deepEqual(look((await a.archive('PEACE'))[0]), look(actor));
            const old = { SkinItem: 'PEACE_TOK-DOS-01', PaintItem: 'PEACE_TOK-DOS-01_PTOriginal' };
            assert.equal(await a.roleStatus('PEACE', 7, old), 200);
            assert.deepEqual(look((await a.archive('PEACE'))[0]), [old.SkinItem, old.PaintItem, '', '']);
            assert.equal(await a.roleStatus('PEACE', 7, { SkinItem: tiger.SkinItem }), 200);
            assert.deepEqual(look((await a.archive('PEACE'))[0]), [tiger.SkinItem, '', '', '']);
            assert.equal(await a.roleStatus('PEACE', 7, { PaintItem: tiger.PaintItem }), 200);
            assert.deepEqual(look((await a.archive('PEACE'))[0]), look(actor));
            await a.equip('PEACE', 1, 'GSW-PCC');
            const [parked] = await a.archive('PEACE');
            assert.equal(parked.PrimaryWeapon, 'PEACE_GSW-PCC');
            assert.equal(gun(parked, ids[1]).Skin.Ornament, 'WO-SUN');
            await a.equip('PEACE', 1, ids[1]);
            assert.deepEqual(gun((await a.archive('PEACE'))[0], ids[1]), gun(painted, ids[1]));
            await a.equip('PROBE', 10, '');
            assert.deepEqual(look((await a.archive('PROBE'))[0]), [probeGold.SkinItem, probeGold.PaintItem, 'ABGNoEntry', '']);
            await a.equip('PROBE', 10, 'HOGoatHorn');
            await a.equip('PROBE', 9, 'None');
            assert.deepEqual(look((await a.archive('PROBE'))[0]), [probeGold.SkinItem, probeGold.PaintItem, '', 'HOGoatHorn']);
            await a.equip('PROBE', 9, 'ABGNoEntry');
        }
        const noPaint = { SkinItem: tiger.SkinItem, PaintItem: '' };
        assert.equal(await a.roleStatus('PEACE', 7, noPaint), roleSuccess);
        const [afterClear] = await a.archive('PEACE');
        assert.equal(afterClear.SkinConfig.SuitData.PaintItem === '', fixed);
        assert.equal(await a.roleStatus('PEACE', 7, tiger), roleSuccess);
        if (fixed) {
            const beforeReject = fs.readFileSync(file);
            assert.equal(await a.roleStatus('PEACE', 1, 'PROBE_GSW-DMR'), 1);
            assert.equal(await a.roleStatus('PROBE', 10, 'HOTigerHatSergey'), 1);
            assert.equal(await a.roleStatus('PEACE', 9, 'HOGoatHorn'), 1);
            assert.equal(await a.roleStatus('PROBE', 7, tiger), 1);
            assert.equal(await a.roleStatus('PEACE', 7, { ...tiger, PaintItem: probeGold.PaintItem }), 1);
            assert.equal(await a.weaponStatus('PEACE', { WeaponId: 'PROBE_GSW-DMR' }), 1);
            assert.deepEqual(fs.readFileSync(file), beforeReject);
        }
        const expected = await a.archive(...roles);
        assert.deepEqual(expected.slice(2), initial.slice(2));
        a.close();
        const reentry = await f.login(account);
        assert.deepEqual(await reentry.archive(...roles), expected);
        const disk = fs.readFileSync(file);
        const peer = await f.login(peerId);
        assert.deepEqual(await peer.archive(...roles), initial);
        await peer.equip('PEACE', 1, 'PEACE_GSW-CDP');
        const peerBefore = await peer.archive(...roles);
        assert.equal(peerBefore[0].PrimaryWeapon, 'PEACE_GSW-CDP');
        assert.deepEqual(fs.readFileSync(file), disk);
        await f.stop();
        await f.start(appDir);
        const reopened = await f.login(account);
        assert.deepEqual(await reopened.archive(...roles), expected);
        assert.deepEqual(await (await f.login(peerId)).archive(...roles), peerBefore);
        assert.deepEqual(fs.readFileSync(file), disk);
        const diskRole = JSON.parse(disk).roles.PEACE;
        assert.equal(!!diskRole._weaponArchives['GSW-DMR'], !fixed);
        let legacy;
        if (fixed) {
            await f.stop();
            // 旧服务遗留的基础键与角色键可能并存。载入旧档只读投影，明确选择的精确键优先。
            const legacyId = 'fixture_armory_legacy', legacyFile = path.join(loadoutDir, legacyId + '.json');
            const encode = (weaponId, ornament, roleId = 'PEACE') => Buffer.from(f.weaponUpdate.encode({
                RoleId: roleId, WeaponArchive: { WeaponId: weaponId, Skin: { WeaponOrnament: ornament } },
            }).finish()).toString('hex');
            const oldRole = { primaryWeapon: 'GSW-DMR', secondaryWeapon: 'RU-SVD',
                _weaponArchives: { 'GSW-DMR': encode('GSW-DMR', 'WO-SUN'),
                    'RU-SVD': encode('RU-SVD', 'WO-SUN') } };
            oldRole._weaponArchives[ids[0]] = encode(ids[0], 'WO-EARTH');
            oldRole._weaponArchives['PEACE_GSW-PCC'] = encode('PROBE_GSW-DMR', 'WO-SUN', 'PROBE');
            const oldBytes = Buffer.from(JSON.stringify({ playerId: legacyId, roles: { PEACE: oldRole } }));
            fs.writeFileSync(legacyFile, oldBytes, { flag: 'wx' });
            await f.start(appDir);
            const oldClient = await f.login(legacyId);
            const [oldView] = await oldClient.archive('PEACE');
            assert.deepEqual(equipment(oldView), [ids[1], ids[0]]);
            assert.equal(gun(oldView, ids[1]).Skin.Ornament, 'WO-SUN');
            assert.equal(gun(oldView, ids[0]).Skin.Ornament, 'WO-EARTH');
            assert.notEqual(gun(oldView, 'PEACE_GSW-PCC')?.Skin.Ornament, 'WO-SUN');
            assert.deepEqual(fs.readFileSync(legacyFile), oldBytes);
            legacy = { baseSlotAndArchiveRestored: true, exactArchivePreferred: true, readsPreservedDisk: true };
        }
        return { source: JSON.parse(fs.readFileSync(path.join(appDir, 'profile-location.json'))).sourceKind,
            entrySha256: hash(fs.readFileSync(path.join(appDir, 'index.js'))), storeSha256: hash(store),
            scopedEquipment: equipment(scoped), baseEquipment: equipment(byBase),
            weaponOrnamentOnBaseWrite: gun(painted, ids[1]).Skin.Ornament,
            roleAppearance: look(accessories), suitWithoutPaint: afterClear.SkinConfig.SuitData.PaintItem,
            reopenedAndRestarted: true, otherRolesAndAccountIsolated: true, legacy };
    }, { roleSuccess });
}

async function main() {
    const [source, flag] = process.argv.slice(2);
    assert.ok(source && (!flag || flag === '--receipt'), 'Usage: node native-equip.cjs <local-archive-source> [--receipt]');
    const result = { node: process.version, patchSha256: hash(fs.readFileSync(path.join(__dirname, patch))) };
    result.control = await scenario(source, 'local', false);
    result.fixed = [];
    for (const kind of ['release', 'main', 'local']) result.fixed.push(await scenario(source, kind, true));
    result.status = 'PASS';
    const bytes = JSON.stringify(result, null, 2) + '\n';
    if (flag) fs.writeFileSync(path.join(__dirname, 'native-equip-proof.json'), bytes);
    process.stdout.write(bytes);
}

main().catch(error => { console.error(error); process.exitCode = 1; });
