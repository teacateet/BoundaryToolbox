'use strict';
const assert = require('node:assert/strict');
const path = require('node:path');
const {once} = require('node:events');
const {randomUUID,randomBytes,createHash,createHmac} = require('node:crypto');
const {installRoomSpawn} = require('../room-spawn.cjs');
const {createRoomSpawnClient} = require('../room-spawn-client.cjs');

// 参数是已由工具箱准备过的档案服务目录，仅使用该版装备定义、protobuf 与依赖。
// 测试数据全部在内存中。服务占用随机回环端口，不启动大厅或游戏。
async function main(appDir) {
    const location = require(path.join(appDir,'profile-location.json'));
    const dependency = name => require(require.resolve(name,{paths:[appDir,location.source]}));
    const {LoadoutStore} = require(path.join(appDir,'game/loadoutStore.js'));
    const store = new LoadoutStore(), profiles = new Map();
    const a='76561198077762513', b='76561198253731653', role='PEACE', dmr='PEACE_GSW-DMR', akm='PEACE_RU-AKM';
    for (const [id,gun] of [[a,dmr],[b,akm]]) profiles.set(id,{playerId:id,roles:{PEACE:{primaryWeapon:gun,secondaryWeapon:'None'}}});
    store.load = id => profiles.get(id) || null;
    store._playerPath = store.save = () => {throw Error('Synthetic room must not touch any player file');};
    const weapon = dependency('protobufjs').loadSync(path.join(appDir,'game/proto/Request/UpdateWeaponArchiveV2Request.proto'))
        .lookupType('ProjectBoundary.UpdateWeaponArchiveV2Request');
    const express=dependency('express'), app=express(); app.use(express.json());
    const key=randomBytes(32), fixed=randomBytes(32).toString('hex'), signer=randomBytes(32);
    const choice=randomUUID(), run=randomUUID(), roomId='pve-'+choice, credential=key.toString('hex');
    const register=installRoomSpawn(app,()=>store,()=>weapon,JSON.stringify({fixed}),signer.toString('hex'));
    const verifier=createHash('sha256').update(key).digest('hex');
    const signature=createHmac('sha256',signer).update(['brt-local-room-v1',run,choice,a,verifier].join('\n')).digest('hex');
    let live=true;
    assert.equal(register(choice,a,{owner:a,verifier,signature},run,()=>live),true);
    const server=app.listen(0,'127.0.0.1'); await once(server,'listening');
    try {
        const origin='http://127.0.0.1:'+server.address().port;
        const room=createRoomSpawnClient({origin,roomId,credential}), connectionId=randomUUID();
        const selected=await room.admit(a,connectionId);
        const manual={role,primary:dmr,secondary:'None'};
        const first=await selected.prepareSaved(1,role);
        assert.deepEqual([first.FirstWeaponPartData.WeaponID,first.SecondWeaponPartData.WeaponID],[dmr,'None']);
        profiles.get(a).roles.PEACE.primaryWeapon=akm;
        assert.deepEqual(await selected.prepareSaved(1,'Peace'),first);
        await assert.rejects(selected.prepare(1,manual),/stale-or-conflicting-life/);
        for(const partial of [{role,primary:akm},{role,secondary:'None'},{role,playerId:b},null])
            await assert.rejects(selected.prepare(2,partial),/invalid-fields/);
        const second=await selected.prepareSaved(2,role);
        assert.equal(second.FirstWeaponPartData.WeaponID,akm);
        assert.equal((await selected.prepare(3,manual)).FirstWeaponPartData.WeaponID,dmr);
        profiles.get(a).roles.PEACE.primaryWeapon='PROBE_GSW-FOP';
        await assert.rejects(selected.prepareSaved(4,role),/invalid-saved-weapon-slot/);
        profiles.get(a).roles.PEACE.primaryWeapon='None';
        assert.equal((await selected.prepareSaved(4,role)).FirstWeaponPartData.WeaponID,'None');
        await assert.rejects(room.admit(b),/wrong-local-player/);
        const fixedRoom=createRoomSpawnClient({origin,roomId:'fixed',credential:fixed});
        const other=await fixedRoom.admit(b);
        assert.equal((await other.prepareSaved(1,role)).FirstWeaponPartData.WeaponID,akm);
        async function lookup(id,token,player) {
            return fetch(origin+'/api/brt/v1/rooms/'+id+'/spawn/birth-plan',{method:'POST',
                headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},
                body:JSON.stringify({authenticatedSteamId:player,connectionId,life:12,selection:{role}})});
        }
        const preflight=await lookup(roomId,credential,a); assert.equal(preflight.status,200);
        assert.equal((await preflight.json()).loadout.FirstWeaponPartData.WeaponID,'None');
        assert.equal((await lookup(roomId,credential,b)).status,403);
        assert.equal((await lookup(roomId,fixed,a)).status,403);
        assert.equal((await lookup('fixed',fixed,b)).status,403);
        await selected.leave();
        await assert.rejects(selected.prepareSaved(5,role),/inactive-member/);
        live=false; assert.equal((await lookup(roomId,credential,a)).status,403);
    } finally {server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
    console.log('PASS: saved/manual selection, per-life retries, clear slots, invalid saved gun, isolated accounts and signed-room reads');
}
if(!process.argv[2]) throw Error('Usage: node protocol.test.cjs <managed-profile-runtime-app>');
main(path.resolve(process.argv[2])).catch(error=>{console.error(error);process.exitCode=1;});
