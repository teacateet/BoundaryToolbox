'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {randomBytes, randomUUID, createHash} = require('node:crypto');
const {fixture} = require('./fixture.cjs');
const {managed} = require('./fixture-kit.cjs');
const {createRoomSpawnClient} = require('./room-spawn-client.cjs');

if (!process.argv[2]) throw Error('Usage: node room-spawn.test.cjs <archive-service-source> [output-directory]');
const src = path.resolve(process.argv[2]);
const out = process.argv[3] && path.resolve(process.argv[3]);
if (out) fs.mkdirSync(out,{recursive:true});
const a = '76561198017477547', b = '76561198174518407', unseen = '76561198245648873';
const tk = 'PEACE_RU-AKM', dmr = 'PEACE_GSW-DMR', probe = 'PROBE_GSW-DMR';
const sight4 = 'GSW-DMR_SGT-ACOG-4X', sight3 = 'GSW-DMR_SGT-REFLEX-3X';
const drum = 'RU-AKM_MAG-AK-DRUM', std = 'RU-AKM_MAG-AK-STD';
const fields = config => Object.fromEntries(config.WeaponPartSlotTypeArray.map((id,i)=>[id,config.WeaponPartConfigs[i]]));
const selection = {role:'PEACE', primary:tk,secondary:dmr};
const h = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const red = randomBytes(32).toString('hex'), blue = randomBytes(32).toString('hex');
process.env.BRT_SPAWN_ROOMS = JSON.stringify({red, blue});

async function run(f, candidate) {
    fs.cpSync(fs.realpathSync(path.join(src,'node_modules')),path.join(f.dir,'node_modules'),{recursive:true});
    const {appDir,loadoutDir} = managed(f.root,f.dir,candidate?[]:['room-spawn.patch']);
    await f.start(appDir);
    let alice = await f.login(a), bob = await f.login(b);
    for (const peer of [alice,bob]) {
        await peer.equip('PEACE',1,tk);
        await peer.equip('PEACE',2,dmr);
    }
    await alice.equip('PEACE',5,'PEACE_TAC-EMP');
    await alice.equip('PEACE',6,'');
    await alice.equip('PEACE',4,'FCM-BOOST');
    await alice.equip('PROBE',5,'MISSILE-HIVE');
    await alice.equip('PROBE',6,'INFO-PROBE');
    await alice.parts('PEACE', {WeaponId:dmr, Parts:[{SlotId:6,PartId:sight4}]});
    const rawSight = (await alice.archive('PEACE'))[0].WeaponConfig.Weapons.find(w=>w.WeaponId===tk)
        .Parts.find(row=>row.SlotId===6)?.PartId;
    assert.ok(rawSight);
    const look = {Info:{Type:'fixture-part-skin',Id:'fixture-part-paint'}, SpecialSlot:'fixture-part-special',Ornament:'fixture-origin-extra'};
    await alice.parts('PEACE', {WeaponId:tk, Parts:[{SlotId:6,PartId:rawSight,Ornament:look}],
        Skin:{SkinInfo:{Type:'fixture-weapon-skin',Id:'fixture-weapon-paint'},WeaponOrnament:'WO-SUN'}});
    await alice.parts('PEACE', {WeaponId:tk, Parts:[{SlotId:9,PartId:drum}]});
    const before = (await alice.archive('PEACE'))[0].WeaponConfig.Weapons.find(w=>w.WeaponId===tk);
    assert.equal(before.Parts.find(row=>row.SlotId===9).PartId,drum);
    if (!candidate) {
        assert.equal(before.Parts.find(row=>row.SlotId===6), undefined);
        assert.equal((await f.http('POST','/api/brt/v1/rooms/red/spawn/admit',{})).status,404);
        return { differentSingleSlotUpdatesLostPriorSight:true, spawnRouteAbsent:true };
    }
    assert.equal(before.Parts.find(row=>row.SlotId===6).Skin.SuitData.PaintItem,look.Info.Id);
    assert.equal(before.Skin.Ornament,'WO-SUN');
    async function raw(room, key, op, body) {
        return fetch(f.origin()+'/api/brt/v1/rooms/'+room+'/spawn/'+op,{method:'POST',
            headers:{'Content-Type':'application/json',Authorization:'Bearer '+key},body:JSON.stringify(body)});
    }
    const room = createRoomSpawnClient({origin:f.origin(),roomId:'red',credential:red});
    const secondRoom = createRoomSpawnClient({origin:f.origin(),roomId:'blue',credential:blue});
    const cid = randomUUID(), cidB = randomUUID();
    assert.equal((await raw('red',blue,'admit',{connectionId:cid,authenticatedSteamId:a})).status,403);
    assert.equal((await f.http('POST','/api/brt/v1/rooms/red/spawn/admit',{connectionId:cid,authenticatedSteamId:a})).status,403);
    const publicGate = await f.http('POST','/connectServer',{playerId:a,platform:'steam'});
    assert.equal((await raw('red',publicGate.body.gateToken,'admit',{connectionId:cid,authenticatedSteamId:a})).status,403);
    const pa=await room.admit(a,cid), pb=await room.admit(b,cidB), ghost=await room.admit(unseen);
    assert.equal((await raw('blue',blue,'prepare',{connectionId:cid,life:1,selection})).status,403);
    assert.equal((await raw('red',red,'prepare',{playerId:b,connectionId:cid,life:1,selection})).status,400);
    await assert.rejects(ghost.prepare(1,selection), /player-profile-missing/);
    const completeMenu=await pb.initialMenu();
    const roleList=['PEACE','PROBE','FIXER','SNIPER','FORT','SPIKE'];
    const nativeMenu=await bob.archive(...roleList);
    assert.deepEqual(completeMenu.roles.map(row=>row.CharacterID).sort(),[...roleList].sort());
    for (const row of nativeMenu) {
        assert.deepEqual(completeMenu.roles.find(r=>r.CharacterID===row.RoleID).InventoryItems,
            [row.PrimaryWeapon,row.SecondWeapon,row.LeftPylon,row.RightPylon,row.MeleeWeapon,row.MobilityModule]);
    }
    const one=await pa.prepare(1,selection), oneB=await pb.prepare(1,selection);
    assert.equal(fields(one.FirstWeaponPartData)[9].WeaponPartID,drum);
    assert.equal(fields(one.SecondWeaponPartData)[6].WeaponPartID,sight4);
    assert.equal(fields(oneB.FirstWeaponPartData)[9].WeaponPartID,std);
    assert.equal(fields(oneB.SecondWeaponPartData)[6].WeaponPartID,sight3);
    assert.equal(one.FirstWeaponPartData.WeaponPartConfigs.length,10);
    assert.deepEqual([one.LeftLauncherData.ID,one.RightLauncherData.ID,one.MobilityModuleData.MobilityModuleID],
        ['PEACE_TAC-EMP','None','PEACE_FCM-BOOST']);
    assert.deepEqual(fields(one.FirstWeaponPartData)[6],{WeaponPartID:rawSight,WeaponPartSkinID:look.Info.Type,
        WeaponPartSpecialSkinID:look.SpecialSlot,WeaponPartSkinPaintingID:look.Info.Id,ArchiveOrnament:look.Ornament});
    assert.equal(one.FirstWeaponPartData.OrnamentID,'WO-SUN');
    await assert.rejects(pa.prepare(1,{...selection,primary:dmr}),/stale-or-conflicting-life/);
    await assert.rejects(pa.prepare(4,selection),/stale-or-conflicting-life/);
    await assert.rejects(pa.prepare(2,{...selection,primary:probe}),/weapon-outside-selected-role/);
    const saved = path.join(loadoutDir,a+'.json'), beforeReject = h(saved);
    assert.equal(await alice.weaponStatus('PEACE',{WeaponId:tk,Parts:[{SlotId:6,PartId:drum}]}),1);
    assert.equal(await alice.weaponStatus('PEACE',{WeaponId:tk,Parts:[{SlotId:9,PartId:std},{SlotId:9,PartId:drum}]}),1);
    assert.equal(h(saved),beforeReject);
    await alice.parts('PEACE',{WeaponId:tk,Parts:[{SlotId:9,PartId:std}]});
    assert.deepEqual(await pa.prepare(1,selection),one);
    const two=await pa.prepare(2,{role:'PROBE',primary:probe,secondary:'None'});
    assert.equal(two.CharacterID,'PROBE');
    assert.equal(two.SecondWeaponPartData.WeaponID,'None');
    assert.deepEqual([two.LeftLauncherData.ID,two.RightLauncherData.ID],['PROBE_MISSILE-HIVE','PROBE_INFO-PROBE']);
    const three=await pa.prepare(3,{role:'Peace',primary:dmr,secondary:tk});
    assert.equal(fields(three.SecondWeaponPartData)[9].WeaponPartID,std);
    assert.equal(fields(three.SecondWeaponPartData)[6].WeaponPartSkinPaintingID,look.Info.Id);
    await alice.parts('PEACE',{WeaponId:tk,Parts:[{SlotId:6,PartId:''}]});
    const clear=await pa.prepare(4,selection);
    assert.equal(fields(clear.FirstWeaponPartData)[6].WeaponPartID,'None');
    assert.equal(fields(clear.FirstWeaponPartData)[6].WeaponPartSkinID,'None');
    const alt=await secondRoom.admit(b,cid);
    assert.equal(fields((await alt.prepare(1,selection)).FirstWeaponPartData)[9].WeaponPartID,std);
    const replacement = await room.admit(a);
    await assert.rejects(pa.prepare(5,selection), /inactive-member/);
    await assert.rejects(room.admit(b,cid), /connection-already-bound/);
    await replacement.leave();
    await assert.rejects(replacement.prepare(1,selection),/inactive-member/);
    await f.stop();
    await f.start(appDir);
    alice=await f.login(a);
    const persisted=(await alice.archive('PEACE'))[0].WeaponConfig.Weapons.find(w=>w.WeaponId===tk);
    assert.equal(persisted.Parts.find(p=>p.SlotId===9).PartId,std);
    assert.equal(persisted.Parts.find(p=>p.SlotId===6).PartId,'');
    assert.equal((await raw('red',red,'prepare',{connectionId:cid,life:1,selection})).status,403);
    const clean=await createRoomSpawnClient({origin:f.origin(),roomId:'red',credential:red}).admit(a);
    const restart=await clean.prepare(1,selection);
    assert.equal(fields(restart.FirstWeaponPartData)[6].WeaponPartID,'None');
    assert.deepEqual(fields(restart.SecondWeaponPartData)[6],fields(one.SecondWeaponPartData)[6]);
    assert.equal(Object.hasOwn(restart.FirstWeaponPartData,'WeaponClassID'),false);
    const structured = await f.http('PUT','/api/loadout/'+a,{roles:{PEACE:{weaponConfigs:{[tk]:{weaponClassId:'fixture-approved-tk-class'}}}}});
    assert.equal(structured.status,200);
    const storedClass = await clean.prepare(2,selection);
    assert.equal(storedClass.FirstWeaponPartData.WeaponClassID,'fixture-approved-tk-class');
    assert.equal(Object.hasOwn(storedClass.SecondWeaponPartData,'WeaponClassID'),false);
    if (out) fs.writeFileSync(path.join(out,'spawn-example.json'),JSON.stringify(one,null,2)+'\n');
    return { sourceKind:JSON.parse(fs.readFileSync(path.join(appDir,'profile-location.json'))).sourceKind,
        nativeDeltaKeptSightAndPaint:true, repeatedLifeUnchanged:true, checkedRooms:2, checkedAccounts:3,
        tk47MagazineA:fields(one.FirstWeaponPartData)[9].WeaponPartID, tk47MagazineB:fields(oneB.FirstWeaponPartData)[9].WeaponPartID,
        peerAOptic:fields(one.SecondWeaponPartData)[6].WeaponPartID,peerBOptic:fields(oneB.SecondWeaponPartData)[6].WeaponPartID,
        persistedAfterRestart:true, supportedScopes:10, lobbyGateRejected:true, realConsumer:'room-spawn-client.cjs',
        secondLife:two.CharacterID, clearedRightLauncher:one.RightLauncherData.ID, explicitClassIdVerified:true, playerGameRun:false };
}
(async()=>{
    const options={roleSuccess:200,scratch:process.env.BRT_TEST_TMP || require('node:os').tmpdir()};
    const before=await fixture(src,f=>run(f,false),options);
    const after=await fixture(src,f=>run(f,true),options);
    const result={status:'PASS',before,after};
    if (out) fs.writeFileSync(path.join(out,'receipt.json'),JSON.stringify(result,null,2)+'\n'); console.log(JSON.stringify(result));
})().catch(error=>{console.error(error);process.exitCode=1;});
