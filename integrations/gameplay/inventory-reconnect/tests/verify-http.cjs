'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {once}=require('node:events'), {randomBytes,randomUUID,createHash,createHmac}=require('node:crypto');
const run=require('node:util').promisify(require('node:child_process').execFile);
const integration=path.resolve(__dirname,'../../../loadout');
const {fixture}=require(path.join(integration,'fixture.cjs')), {managed}=require(path.join(integration,'fixture-kit.cjs'));
const {createRoomSpawnClient}=require(path.join(integration,'room-spawn-client.cjs'));
const [scratch, exe, src, baseline] = process.argv.slice(2);
if(!scratch || !exe || !src || !baseline)throw Error('usage: node verify-http.cjs work-dir read.exe archive-source original-room-spawn.cjs');
const owner='76561198123363522', stranger='76561198059932077';
const normalize=s=>s.replaceAll('\r\n','\n');

async function exercise(original) {
    return fixture(src,async f=>{
        fs.cpSync(fs.realpathSync(path.join(src,'node_modules')),path.join(f.dir,'node_modules'),{recursive:true});
        const {appDir}=managed(f.root,f.dir);
        const route=path.join(appDir,'room-spawn.cjs');
        if(original)fs.copyFileSync(baseline,route);
        else assert.equal(normalize(fs.readFileSync(route,'utf8')),normalize(fs.readFileSync(path.join(integration,'room-spawn.cjs'),'utf8')));
        const {installRoomSpawn}=require(route);
        const {LoadoutStore}=require(path.join(appDir,'game/loadoutStore.js'));
        await f.start(appDir);
        const rpc=await f.login(owner);
        const express=require(path.join(src,'node_modules/express')),app=express();app.use(express.json());
        const signKey=randomBytes(32), key=randomBytes(32), bad=randomBytes(32).toString('hex'), game=randomUUID();
        const register=installRoomSpawn(app,()=>new LoadoutStore(),()=>f.weaponUpdate,JSON.stringify({fixed:bad}),signKey.toString('hex'));
        const verifier=createHash('sha256').update(key).digest('hex');
        let live=true;
        const sign=()=>{
            const id=randomUUID(), signature=createHmac('sha256',signKey).update(['brt-local-room-v1',game,id,owner,verifier].join('\n')).digest('hex');
            assert(register(id,owner,{owner,verifier,signature},game,()=>live));return 'pve-'+id;
        };
        let room=sign();
        const service=app.listen(0,'127.0.0.1');await once(service,'listening');
        try {
            const port=service.address().port;
            const read=async (op,id,previous='',who=owner,secret=key.toString('hex'))=>{
                const result=await run(exe,[String(port),op,id,who,previous],{windowsHide:true,timeout:5000,
                    env:{...process.env,BRT_LOCAL_SPAWN_ROOM:room,BRT_LOCAL_SPAWN_TOKEN:secret}});
                return result.stdout ? JSON.parse(result.stdout) : null;
            };
            const deny=(promise,status)=>assert.rejects(promise,e=>e.stderr.includes('HTTP '+status));
            const first=randomUUID(),second=randomUUID(),third=randomUUID();
            if(original) {
                assert.equal((await read('old',first)).roles.length,6);
                await deny(read('old',second),409);
                console.log(JSON.stringify({variant:'baseline',firstRoles:6,reconnectInSameRoom:409}));
                return;
            }
            await deny(read('read',first,'',stranger),403);
            await deny(read('read',first,'',owner,bad),403);
            const initial=await read('read',first);
            assert.equal(initial.roles.length,6);assert.equal(initial.authenticatedSteamId,owner);
            const choose=rows=>rows.find(r=>r.CharacterID==='PROBE').InventoryItems;
            assert.equal(choose(initial.roles)[2],'PROBE_MISSILE-HIVE');
            await rpc.equip('PROBE',5,'PROBE_MISSILE-SPARROW');
            assert.deepEqual(await read('read',first),initial);
            await deny(read('read',second),409);await deny(read('read',second,randomUUID()),409);
            await deny(read('ack',second),409);await deny(read('ack',first,'',stranger),403);
            await read('ack',first);await read('ack',first);
            await deny(read('read',first),409);await deny(read('old',first),409);
            const client=createRoomSpawnClient({origin:'http://127.0.0.1:'+port,roomId:room,credential:key.toString('hex')});
            const active=await client.admit(owner);
            const equipment=choose((await active.initialMenu()).roles);
            const selection={role:'PROBE',primary:equipment[0],secondary:equipment[1]};
            for(const n of [1,2])assert.equal((await active.prepare(n,selection)).LeftLauncherData.ID,'PROBE_MISSILE-SPARROW');
            await deny(read('read',first),409);
            const restored=await read('read',second,first);
            assert.equal(restored.roomId,room);assert.equal(restored.connectionId,second);
            const columns=['PrimaryWeapon','SecondWeapon','LeftPylon','RightPylon','MeleeWeapon','MobilityModule'];
            const archive=await rpc.archive(...restored.roles.map(r=>r.CharacterID));
            for(const role of archive)assert.deepEqual(restored.roles.find(r=>r.CharacterID===role.RoleID).InventoryItems,columns.map(c=>role[c]));
            assert.equal(choose(restored.roles)[2],'PROBE_MISSILE-SPARROW');
            await deny(read('ack',first),409);await deny(read('read',first),409);
            await deny(read('read',third,first),409);
            await read('ack',second);
            assert.equal((await active.prepare(3,selection)).FirstWeaponPartData.WeaponID,selection.primary);
            for(const n of [4,5])await active.prepare(n,selection);
            await active.leave();
            await deny(read('read',second,first),409);
            const thirdMenu=await read('read',third,second);
            assert.equal(thirdMenu.connectionId,third);await read('ack',third);
            room=sign();
            const liveFlow=await run(path.join(path.dirname(exe),'flow.exe'),[String(port),owner],{windowsHide:true,timeout:7000,
                env:{...process.env,BRT_LOCAL_SPAWN_ROOM:room,BRT_LOCAL_SPAWN_TOKEN:key.toString('hex')}});
            const connectionHarness=JSON.parse(liveFlow.stdout);
            assert.equal(connectionHarness.detachedTransportAllowsReconnect,true);
            room=sign();
            const newClient=createRoomSpawnClient({origin:'http://127.0.0.1:'+port,roomId:room,credential:key.toString('hex')});
            const prior=await newClient.admit(owner);await prior.initialMenu();await prior.prepare(1,selection);
            await deny(read('read',randomUUID()),409);
            live=false;await deny(read('read',randomUUID()),403);
            console.log(JSON.stringify({variant:'formal',firstAndRetrySameSnapshot:true,replacedConnectedController:false,
                roles:restored.roles.length,archiveMatchedSlots:36,firstLeft:choose(initial.roles)[2],reconnectedLeft:choose(restored.roles)[2],
                parallelBirths:5,lateFirstReadRejected:true,stalePredecessorRejected:true,acksIdempotent:true,connectionHarness,retailReconnectObserved:false}));
        } finally {service.closeAllConnections();await new Promise(r=>service.close(r));}
    },{roleSuccess:200,scratch});
}
(async()=>{await exercise(true);await exercise(false);})().catch(error=>{console.error(error);process.exitCode=1});
