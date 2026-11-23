'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { fixture } = require('./fixture.cjs');

async function replay(source, installedApp, roleSuccess) {
    const app = fs.realpathSync(installedApp);
    return fixture(source, async f => {
        const copy = path.join(f.root, 'player-app'), save = path.join(f.root, 'fixture-loadouts');
        fs.mkdirSync(copy);
        for (const name of ['index.js', 'game', 'node_modules', 'native-menu.cjs', 'ready-room.cjs', 'session-progress.cjs', 'package.json']) {
            fs.cpSync(path.join(app, name), path.join(copy, name), { recursive: true, dereference: true });
        }
        fs.writeFileSync(path.join(copy, 'profile-location.json'), JSON.stringify({ loadoutDir: save }), { flag: 'wx' });
        const account = 'fixture_sniper_secondary';
        const id = 'SNIPER_RU-MOSIN';
        const hash = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
        assert.equal(hash(path.join(copy, 'index.js')), hash(path.join(app, 'index.js')));
        await f.start(copy);
        const a = await f.login(account);
        // 全新档案的默认枪和玩家点击前的档案不同，先重建日志中可见的两个槽位。
        await a.equip('Sniper', 1, 'SNIPER_GSW-PSR');
        await a.equip('Sniper', 2, 'SNIPER_RU-SVD');
        const roster = ['PEACE', 'PROBE', 'Sniper', 'FORT', 'FIXER', 'SPIKE'];
        const readSniper = async client => (await client.archive(...roster)).filter(r => r.RoleID === 'Sniper');
        async function checkRest(secondary) {
            const route = '/api/loadout/' + account;
            const full = await f.http('GET', route);
            const single = await f.http('GET', route + '/Sniper');
            assert.equal(full.status, 200);
            assert.equal(single.status, 200);
            const row = full.body.roles.Sniper;
            assert.ok(row, 'Expected exact Sniper key in the full response');
            for (const role of [row, single.body]) {
                assert.equal(role.roleId, 'Sniper');
                assert.equal(role.primaryWeapon, 'SNIPER_GSW-PSR');
                assert.equal(role.secondaryWeapon, secondary);
            }
        }
        const [start] = await readSniper(a);
        assert.equal(start.SecondWeapon, 'SNIPER_RU-SVD');
        await checkRest(start.SecondWeapon);
        for (let i = 0; i < 4; i++) await a.equip('Sniper', 2, id);
        const role = JSON.parse(fs.readFileSync(path.join(save, account + '.json'))).roles.Sniper;
        assert.equal(role.secondaryWeapon, id);
        function check([r]) {
            assert.equal(r.RoleID, 'Sniper');
            assert.equal(r.PrimaryWeapon, start.PrimaryWeapon);
            assert.equal(r.SecondWeapon, id);
            assert.equal(r.WeaponConfig.Weapons.filter(w => w.WeaponId === id).length, 1);
        }
        check(await readSniper(a));
        await checkRest(id);
        a.close();
        await f.stop();
        await f.start(copy);
        check(await readSniper(await f.login(account)));
        await checkRest(id);
        return { entrySha256: hash(path.join(copy, 'index.js')), before: start.SecondWeapon, after: id,
            roleStatusCode: roleSuccess, writesAcknowledged: 4, sameSessionRead: true, afterRestartRead: true,
            fullAndRoleRestVerified: true, gameUiVerified: false };
    }, { roleSuccess });
}

if (require.main === module) {
    const [source, app, mode, ...rest] = process.argv.slice(2);
    if (!source || !app || rest.length || (mode && mode !== '--legacy-zero')) {
        throw new Error('Usage: node sniper-secondary.cjs <stock-service> <installed-managed-app> [--legacy-zero]');
    }
    replay(source, app, mode ? 0 : 200).then(x => console.log(JSON.stringify(x))).catch(e => { console.error(e); process.exitCode = 1; });
}
