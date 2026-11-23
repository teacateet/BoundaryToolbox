'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { randomBytes } = require('node:crypto');
const { fixture } = require('./fixture.cjs');
const { managed } = require('./fixture-kit.cjs');
const { createRoomSpawnClient } = require('./room-spawn-client.cjs');
const sample = require('./probe-minifarm-sample.json');

async function verify(source, scratch, receiptPath) {
    const roomKey = randomBytes(32).toString('hex');
    process.env.BRT_SPAWN_ROOMS = JSON.stringify({ appearance: roomKey });
    const result = await fixture(source, async f => {
        fs.cpSync(fs.realpathSync(path.join(source, 'node_modules')), path.join(f.dir, 'node_modules'), { recursive:true });
        const { appDir } = managed(f.root, f.dir);
        await f.start(appDir);
        const account = '76561198030700723';
        let rpc = await f.login(account);
        const { role, primary, secondary } = sample;
        await rpc.equip(role, 1, primary);
        await rpc.equip(role, 2, secondary);
        for (const w of sample.weapons) await rpc.parts(role, w);
        const recorded = sample.weapons.find(w => w.WeaponId === secondary);
        const optic = recorded.Parts.find(p => p.SlotId === 6);
        const room = () => createRoomSpawnClient({ origin:f.origin(), roomId:'appearance', credential:roomKey });
        let member = await room().admit(account);
        const selection = { role, primary, secondary };
        const read = async life => (await member.prepare(life, selection)).SecondWeaponPartData;
        const fromScope = w => w.WeaponPartConfigs[w.WeaponPartSlotTypeArray.indexOf(6)];
        const send = edit => rpc.parts(role, { WeaponId:secondary, ...edit });
        const first = await read(1);
        const originalSight = fromScope(first);
        assert.equal(originalSight.WeaponPartID, optic.PartId);
        assert.notEqual(originalSight.WeaponPartSkinPaintingID, 'None');
        const originalPaint = first.ArchiveAppearance.PaintItem;
        const originalSuit = first.ArchiveAppearance.SkinItem;
        await send({ Skin:{ WeaponOrnament:'WO-SUN' } });
        await send({ Parts:[{ SlotId:6, Ornament:{ SpecialSlot:'' } }] });
        const partial = await read(2);
        assert.equal(partial.OrnamentID, 'WO-SUN');
        assert.equal(partial.ArchiveAppearance.PaintItem, originalPaint);
        assert.equal(partial.ArchiveAppearance.SkinItem, originalSuit);
        assert.deepEqual(fromScope(partial), { ...originalSight, WeaponPartSpecialSkinID:'None' });
        await send({ Skin:{ SkinInfo:{ Id:'' } } });
        await send({ Parts:[{ SlotId:6, PartId:optic.PartId, Ornament:{ Info:{ Id:'' } } }] });
        await f.stop();
        await f.start(appDir);
        rpc = await f.login(account);
        member = await room().admit(account);
        const cleared = await read(1);
        assert.equal(cleared.OrnamentID, 'WO-SUN');
        assert.equal(cleared.ArchiveAppearance.PaintItem, 'None');
        assert.equal(cleared.ArchiveAppearance.SkinItem, originalSuit);
        assert.equal(fromScope(cleared).WeaponPartSkinPaintingID, 'None');
        assert.equal(fromScope(cleared).WeaponPartSkinID, originalSight.WeaponPartSkinID);
        await send({ Parts:[{ SlotId:6 }] });
        const empty = await read(2);
        assert.equal(fromScope(empty).WeaponPartID, 'None');
        assert.equal(fromScope(empty).WeaponPartSkinID, 'None');
        assert.equal(await rpc.roleStatus(role, 7, {SkinItem:'PROBE_ORIGINAL', PaintItem:'PROBE_ORIGINAL_PTMetalGold'}), 200);
        await rpc.equip(role, 9, 'ABGNoEntry');
        await rpc.equip(role, 10, 'HOGoatHorn');
        const characterLife = await member.prepareEnvelope(3, selection);
        assert.deepEqual(characterLife.loadout.CharacterData, {SpacesuitID:'PROBE_ORIGINAL',
            SpacesuitSkinPaintingID:'PROBE_ORIGINAL_PTMetalGold', ArmBadgeID:'ABGNoEntry', HeadAccessoryID:'HOGoatHorn'});
        await rpc.equip(role, 10, 'None');
        await f.stop();
        await f.start(appDir);
        await f.login(account);
        member = await room().admit(account);
        const clearHead = await member.prepareEnvelope(1, selection);
        assert.deepEqual(clearHead.loadout.CharacterData, {...characterLife.loadout.CharacterData, HeadAccessoryID:'None'});
        if (receiptPath) {
            const saved = path.join(path.dirname(receiptPath), 'character-http');
            fs.mkdirSync(saved, {recursive:true});
            for (const [id, item] of Object.entries({characterLife,clearHead})) {
                fs.writeFileSync(path.join(saved, id+'.json'), JSON.stringify(item,null,2)+'\n');
            }
        }
        return { status:'PASS', source:'native protobuf and authenticated room service', weapon:secondary,
            ornamentOnlyKeptSuitAndPaint:true, reticleOnlyKeptOpticAndPaint:true,
            explicitPaintClearsSurvivedRestart:true, slotOnlyUnequipped:true,
            roleSuitPaintBadgeHeadSurvivedRestart:true, headUnequippedWithoutResettingSuit:true, playerGameRun:false };
    }, { roleSuccess:200, scratch });
    if (receiptPath) fs.writeFileSync(receiptPath, JSON.stringify(result, null, 2) + '\n');
    console.log(JSON.stringify(result));
}
if (require.main === module) verify(path.resolve(process.argv[2]), path.resolve(process.argv[3] || require('node:os').tmpdir()), process.argv[4])
    .catch(error => { console.error(error); process.exitCode = 1; });
