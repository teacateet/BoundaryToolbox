'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { randomBytes } = require('node:crypto');
const { fixture } = require('./fixture.cjs');
const { managed } = require('./fixture-kit.cjs');
const { createRoomSpawnClient } = require('./room-spawn-client.cjs');
const sample = require('./probe-minifarm-sample.json');

async function probeSpawnCase(source) {
    const key = randomBytes(32).toString('hex');
    const prior = process.env.BRT_SPAWN_ROOMS;
    process.env.BRT_SPAWN_ROOMS = JSON.stringify({minifarm:key});
    try {
        return await fixture(source, async f => {
            fs.cpSync(fs.realpathSync(path.join(source, 'node_modules')), path.join(f.dir, 'node_modules'), {recursive:true});
            const { appDir } = managed(f.root, f.dir);
            await f.start(appDir);
            const ids = ['76561197987927242', '76561198086047094'];
            const first = await f.login(ids[0]), peer = await f.login(ids[1]);
            for (const [slot, value] of [[1,sample.primary],[2,sample.secondary],[4,sample.mobility],[5,sample.left],[6,sample.right]])
                await first.equip(sample.role, slot, value);
            for (const gun of sample.weapons) await first.parts(sample.role, gun);
            await peer.equip('PROBE',1,sample.primary);
            const choice = {role:sample.role, primary:sample.primary,secondary:sample.secondary};
            const room = createRoomSpawnClient({origin:f.origin(),roomId:'minifarm',credential:key});
            const one = await room.admit(ids[0]), two = await room.admit(ids[1]);
            const primaryLife = await one.prepareEnvelope(1,choice);
            const peerLife = await two.prepareEnvelope(1,choice);
            assert.deepEqual(await one.prepareEnvelope(1,choice),primaryLife);
            const kit = primaryLife.loadout;
            for (const [field, gun] of [['FirstWeaponPartData',sample.weapons.find(w=>w.WeaponId===sample.primary)],
                ['SecondWeaponPartData',sample.weapons.find(w=>w.WeaponId===sample.secondary)]]) {
                const output = kit[field];
                assert.equal(output.WeaponID,gun.WeaponId);
                assert.equal(output.ArchiveAppearance.PaintItem,gun.Skin.SkinInfo.Id);
                assert.equal(output.ArchiveAppearance.SkinItem,gun.Skin.SkinInfo.Type);
                assert.equal(output.OrnamentID,gun.Skin.WeaponOrnament);
                assert.equal(Object.hasOwn(output,'WeaponClassID'),false);
                assert.deepEqual(output.WeaponPartSlotTypeArray,[1,2,3,4,5,6,7,8,9,10]);
                for (const part of gun.Parts) {
                    const row = output.WeaponPartConfigs[output.WeaponPartSlotTypeArray.indexOf(part.SlotId)];
                    assert.equal(row.WeaponPartID,part.PartId || 'None');
                    assert.equal(row.WeaponPartSkinID,part.Ornament?.Info?.Type || 'None');
                    assert.equal(row.WeaponPartSkinPaintingID,part.Ornament?.Info?.Id || 'None');
                    assert.equal(row.WeaponPartSpecialSkinID,part.Ornament?.SpecialSlot || 'None');
                }
                assert.notEqual(peerLife.loadout[field].ArchiveAppearance.PaintItem,output.ArchiveAppearance.PaintItem);
            }
            assert.deepEqual([kit.LeftLauncherData.ID, kit.RightLauncherData.ID, kit.MobilityModuleData.MobilityModuleID],
                ['PROBE_MISSILE-SPARROW','PROBE_INFO-PROBE','PROBE_FCM-GRAPPLE']);
            await first.equip('PROBE',6,'');
            const retry = await one.prepareEnvelope(1,choice);
            assert.deepEqual(retry,primaryLife);
            const respawn = await one.prepareEnvelope(2,{...choice,secondary:'None'});
            assert.equal(respawn.loadout.RightLauncherData.ID,'None');
            assert.equal(respawn.loadout.SecondWeaponPartData.WeaponID,'None');
            assert.deepEqual(await two.prepareEnvelope(1,choice),peerLife);
            return {primaryLife,peerLife,respawn,retry};
        },{roleSuccess:200,scratch:process.env.BRT_TEST_TMP || require('node:os').tmpdir()});
    } finally {
        if (prior === undefined) delete process.env.BRT_SPAWN_ROOMS;
        else process.env.BRT_SPAWN_ROOMS = prior;
    }
}
module.exports={probeSpawnCase};
if (require.main === module) (async()=>{
    const [source,out] = process.argv.slice(2);
    if (!source || !out) throw Error('Usage: node probe-spawn-case.cjs <archive-service-source> <empty-output-directory>');
    const records = await probeSpawnCase(path.resolve(source));
    fs.mkdirSync(out,{recursive:true});
    for(const [label,record] of Object.entries(records)) fs.writeFileSync(path.join(out,label+'.json'),JSON.stringify(record,null,2)+'\n',{flag:'wx'});
    console.log('PASS: 2 stock-compatible accounts, 3 match lives, 20 saved part slots, repeated request.');
})().catch(error=>{console.error(error);process.exitCode=1;});
