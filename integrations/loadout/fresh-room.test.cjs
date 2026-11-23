'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path'), os = require('node:os');
const { once } = require('node:events');
const { randomUUID, randomBytes, createHash, createHmac } = require('node:crypto');
const { fixture } = require('./fixture.cjs');
const { managed } = require('./fixture-kit.cjs');
const { createRoomSpawnClient } = require('./room-spawn-client.cjs');
const roles = ['PEACE','PROBE','FIXER','SNIPER','FORT','SPIKE'];
const columns = ['PrimaryWeapon','SecondWeapon','LeftPylon','RightPylon','MeleeWeapon','MobilityModule'];
const owner = '76561198140196979', other = '76561197984903891';

test('已登录本机房主没改过军械库时可选六名原厂干员并按生命取得配件，固定房和坏档仍拒绝', {
    skip: !process.env.BRT_QA_ARCHIVE_SOURCE, timeout:60000
}, async () => {
    const source = fs.realpathSync(process.env.BRT_QA_ARCHIVE_SOURCE);
    await fixture(source, async f => {
        fs.cpSync(fs.realpathSync(path.join(source,'node_modules')),path.join(f.dir,'node_modules'),{recursive:true});
        const { appDir, loadoutDir } = managed(f.root, f.dir);
        await f.start(appDir);
        const rpc = await f.login(owner), archive = await rpc.archive(...roles);
        const personal = path.join(loadoutDir,owner + '.json');
        assert.equal(fs.existsSync(personal),false);
        const { LoadoutStore } = require(path.join(appDir,'game/loadoutStore.js'));
        const express = require(path.join(source,'node_modules/express'));
        const app = express(); app.use(express.json());
        const { installRoomSpawn } = require(path.join(appDir,'room-spawn.cjs'));
        const signingKey = randomBytes(32), token = randomBytes(32), staticToken = randomBytes(32).toString('hex');
        const register = installRoomSpawn(app, () => new LoadoutStore(), () => f.weaponUpdate,
            JSON.stringify({fixed:staticToken}), signingKey.toString('hex'));
        const id = randomUUID(), run = randomUUID(), verifier = createHash('sha256').update(token).digest('hex');
        const signature = createHmac('sha256',signingKey).update(['brt-local-room-v1',run,id,owner,verifier].join('\n')).digest('hex');
        let live = true;
        assert.equal(register(id,owner,{owner,verifier,signature:'0'.repeat(64)},run,()=>live),false);
        assert.equal(register(id,owner,{owner,verifier,signature},run,()=>live),true);
        const server = app.listen(0,'127.0.0.1'); await once(server,'listening');
        try {
            const origin = 'http://127.0.0.1:' + server.address().port;
            const local = createRoomSpawnClient({origin,roomId:'pve-'+id,credential:token.toString('hex')});
            const fixed = createRoomSpawnClient({origin,roomId:'fixed',credential:staticToken});
            const next = async () => local.admit(owner);
            const member = await next();
            await assert.rejects(local.admit(other),/wrong-local-player/);
            const neverSeen = await fixed.admit(other), untouched = await fixed.admit(owner);
            await assert.rejects(neverSeen.initialMenu(),/player-profile-missing/);
            await assert.rejects(untouched.initialMenu(),/player-profile-missing/);
            const initial = (await member.initialMenu()).roles;
            assert.equal(initial.length,6);
            assert.equal(archive.length,6);
            assert.deepEqual(initial.map(r=>r.CharacterID).sort(),[...roles].sort());
            let life = 0;
            for (const role of archive) {
                const slots = columns.map(k=>role[k]);
                assert.deepEqual(initial.find(r=>r.CharacterID===role.RoleID).InventoryItems,slots);
                const selection = {role:role.RoleID,primary:slots[0],secondary:slots[1]};
                const spawn = await member.prepare(++life,selection);
                assert.deepEqual([spawn.FirstWeaponPartData.WeaponID,spawn.SecondWeaponPartData.WeaponID,spawn.LeftLauncherData.ID,
                    spawn.RightLauncherData.ID,spawn.MeleeWeaponData.ID,spawn.MobilityModuleData.MobilityModuleID],slots);
                for (const w of [spawn.FirstWeaponPartData,spawn.SecondWeaponPartData]) {
                    if (w.WeaponID==='None') continue;
                    const parts = role.WeaponConfig.Weapons.find(r=>r.WeaponId===w.WeaponID).Parts;
                    assert.equal(parts.length,10);
                    for (const part of parts) assert.equal(w.WeaponPartConfigs[w.WeaponPartSlotTypeArray.indexOf(part.SlotId)].WeaponPartID,part.PartId || 'None');
                }
                assert.deepEqual(await member.prepare(life,selection),spawn);
            }
            assert.equal(fs.existsSync(personal),false);
            fs.mkdirSync(loadoutDir,{recursive:true});
            for (const [body,error] of [['<invalid>',/player-profile-missing/],
                    [JSON.stringify({playerId:other,roles:{}}),/player-profile-missing/],
                    [JSON.stringify({playerId:owner,roles:[]}),/invalid-saved-role/]]) {
                fs.writeFileSync(personal,body);
                const bad = await next();
                await assert.rejects(bad.initialMenu(),error);
                await assert.rejects(bad.prepare(1,{role:archive[0].RoleID,primary:archive[0].PrimaryWeapon,secondary:archive[0].SecondWeapon}),error);
                assert.equal(fs.readFileSync(personal,'utf8'),body);
            }
            live = false;
            await assert.rejects(next(),/room-auth-required/);
        } finally {
            live = false;
            server.closeAllConnections(); await new Promise(done=>server.close(done));
        }
    }, {roleSuccess:200,scratch:process.env.BRT_QA_SCRATCH || os.tmpdir()});
});
