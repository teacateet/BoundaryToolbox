'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { randomBytes } = require('node:crypto');
const { fixture } = require('./fixture.cjs');
const { managed } = require('./fixture-kit.cjs');
const { createRoomSpawnClient } = require('./room-spawn-client.cjs');
const sample = require('./probe-minifarm-sample.json');
const catalog = require('./receiver-main.json');

const expected = {
    primary: { WeaponPartID: '5001002', WeaponPartSkinID: 'PartOri', WeaponPartSkinPaintingID: 'GSW-DMR_ReceiverMain_PTMetalGold' },
    secondary: { WeaponPartID: '5001009', WeaponPartSkinID: 'PartOri', WeaponPartSkinPaintingID: 'GSW-FOP_ReceiverMain_PTElectroRed' }
};
const choice = { role: sample.role, primary: sample.primary, secondary: sample.secondary };
const noReceiver = config => assert.equal(Object.hasOwn(config, 'ReceiverMainAppearance'), false);

async function verifyAllowedColors(source, player, room) {
    const index = require(path.join(source,'game/definitionIndex.js')).getDefinitionIndex();
    const seen = new Set(), roles = ['PEACE','PROBE','FIXER','SNIPER','FORT','SPIKE'];
    let life = 0;
    for (const role of roles) {
        const definition = index.getRole(role);
        for (const id of definition.weaponScope) {
            const base = index.resolveBaseWeaponId(id);
            if (seen.has(base)) continue;
            seen.add(base);
            const weapon = catalog.weapons[base];
            assert.ok(weapon,base);
            await player.equip(role,1,id);
            for (const suitId of weapon.suits) {
                const suit = catalog.suits[suitId];
                for (const [paint, receiverPaint] of Object.entries(suit.paints)) {
                    await player.parts(role,{WeaponId:id,Skin:{SkinInfo:{Type:suitId,Id:paint}}});
                    const result = await room.prepareEnvelope(++life,{role,primary:id,secondary:'None'});
                    assert.deepEqual(result.loadout.FirstWeaponPartData.ReceiverMainAppearance,
                        {WeaponPartID:weapon.part,WeaponPartSkinID:suit.skin,WeaponPartSkinPaintingID:receiverPaint},id+'/'+paint);
                }
            }
        }
    }
    assert.equal(seen.size,19);
    assert.equal(life,271);
    return { guns:seen.size, colors:life };
}

async function verify(source, directory) {
    const key = randomBytes(32).toString('hex');
    const previous = process.env.BRT_SPAWN_ROOMS;
    process.env.BRT_SPAWN_ROOMS = JSON.stringify({ minifarm: key });
    try {
        await fixture(source, async f => {
            // 测试来源仅需读取原依赖。发行器自行核验其摘要并创建隔离应用。
            fs.symlinkSync(fs.realpathSync(path.join(source,'node_modules')), path.join(f.dir,'node_modules'),
                process.platform === 'win32' ? 'junction' : 'dir');
            const { appDir } = managed(f.root, f.dir);
            assert.deepEqual(JSON.parse(fs.readFileSync(path.join(appDir,'receiver-main.json'))),catalog);
            await f.start(appDir);
            const player = '76561198125571485', peerId = '76561198030041405';
            const api = await f.login(player), peer = await f.login(peerId);
            const room = createRoomSpawnClient({ origin: f.origin(), roomId: 'minifarm', credential: key });
            const first = await room.admit(player), second = await room.admit(peerId);
            for (const p of [api, peer]) {
                await p.equip(sample.role, 1, sample.primary);
                await p.equip(sample.role, 2, sample.secondary);
            }
            for (const gun of sample.weapons) await api.parts(sample.role,gun);
            const real = await first.prepareEnvelope(1,choice);
            assert.deepEqual(real.loadout.FirstWeaponPartData.ReceiverMainAppearance,expected.primary);
            assert.deepEqual(real.loadout.SecondWeaponPartData.ReceiverMainAppearance,expected.secondary);
            assert.deepEqual(await first.prepareEnvelope(1,choice),real);
            for (const config of [real.loadout.FirstWeaponPartData,real.loadout.SecondWeaponPartData]) {
                assert.deepEqual(config.WeaponPartSlotTypeArray,[1,2,3,4,5,6,7,8,9,10]);
            }
            const empty = await second.prepareEnvelope(1,choice);
            noReceiver(empty.loadout.FirstWeaponPartData);
            noReceiver(empty.loadout.SecondWeaponPartData);
            const [dmr,fop] = [sample.primary,sample.secondary].map(id=>sample.weapons.find(gun=>gun.WeaponId===id));
            async function change(gun, skinInfo) {
                await peer.parts(sample.role,{WeaponId:gun.WeaponId,Parts:[],Skin:{SkinInfo:skinInfo}});
            }
            // 同一个机匣颜色在别把枪的套装里没有合法关系。
            await change(dmr,{Type:dmr.Skin.SkinInfo.Type,Id:fop.Skin.SkinInfo.Id});
            await change(fop,dmr.Skin.SkinInfo);
            const cross = await second.prepareEnvelope(2,choice);
            noReceiver(cross.loadout.FirstWeaponPartData);
            noReceiver(cross.loadout.SecondWeaponPartData);
            await change(dmr,{Type:'None',Id:'None'});
            await change(fop,{Type:fop.Skin.SkinInfo.Type,Id:'unlisted-paint'});
            const invalid = await second.prepareEnvelope(3,choice);
            noReceiver(invalid.loadout.FirstWeaponPartData);
            noReceiver(invalid.loadout.SecondWeaponPartData);
            const idOnly = '76561198162619544';
            const old = await f.login(idOnly);
            await old.equip(sample.role,1,sample.primary);
            await old.parts(sample.role,{WeaponId:dmr.WeaponId,Parts:[dmr.Parts[1]]});
            const legacy = await (await room.admit(idOnly)).prepareEnvelope(1,{...choice,secondary:'None'});
            noReceiver(legacy.loadout.FirstWeaponPartData);
            noReceiver(legacy.loadout.SecondWeaponPartData);
            assert.equal(legacy.loadout.FirstWeaponPartData.WeaponPartConfigs[1].WeaponPartSkinPaintingID,
                dmr.Parts[1].Ornament.Info.Id);
            // 客户端只发送整枪枪饰和特殊槽时不能显式选择或清除原厂色。
            await old.parts(sample.role,{WeaponId:dmr.WeaponId,Skin:{WeaponOrnament:'WO-SUN'}});
            await old.parts(sample.role,{WeaponId:dmr.WeaponId,Skin:{SpecialSlot:'fixture-special'}});
            const originalClient = await room.admit(idOnly);
            const unsigned = await originalClient.prepareEnvelope(1,{...choice,secondary:'None'});
            noReceiver(unsigned.loadout.FirstWeaponPartData);
            const restore = {Type:'SkinDMROriginal',Id:'GSW-DMR_Original_PTOriginal'};
            await old.parts(sample.role,{WeaponId:dmr.WeaponId,Skin:{SkinInfo:restore}});
            const restored = await originalClient.prepareEnvelope(2,{...choice,secondary:'None'});
            assert.deepEqual(restored.loadout.FirstWeaponPartData.ReceiverMainAppearance,
                {WeaponPartID:'5001002',WeaponPartSkinID:'PartOri',WeaponPartSkinPaintingID:'GSW-DMR_ReceiverMain_PTOriginal'});
            await old.parts(sample.role,{WeaponId:dmr.WeaponId,Parts:[dmr.Parts[1]]});
            assert.deepEqual((await originalClient.prepareEnvelope(3,{...choice,secondary:'None'})).loadout
                .FirstWeaponPartData.ReceiverMainAppearance,restored.loadout.FirstWeaponPartData.ReceiverMainAppearance);
            const tiger = catalog.weapons['GSW-MG-HC'];
            assert.ok(tiger.suits.includes('SkinMGTiger'));
            assert.equal(tiger.part,'5001006_HC');
            assert.equal(catalog.suits.SkinMGTiger.skin,'MGTigerRCVR');
            assert.ok(catalog.weapons['GSW-MG'].suits.includes('SkinMGTiger'));
            assert.equal(catalog.weapons['GSW-MG'].part,'5001006');
            assert.equal(catalog.suits.SkinMGTiger.paints.SkinMGTiger_PTTiger,'GSW-MG_PTTiger');
            const eventGun = catalog.weapons['EVENT-TP82'];
            assert.equal(eventGun.part,'5001021');
            assert.deepEqual(eventGun.suits,['Event-TP82_Original']);
            assert.deepEqual(Object.values(catalog.suits[eventGun.suits[0]].paints),['EVENT-TP82_ReceiverMain_PTOriginal']);
            await assert.rejects(originalClient.prepareEnvelope(4,{role:'PROBE',primary:'PROBE_EVENT-TP82',secondary:'None'}));
            await assert.rejects(originalClient.prepareEnvelope(4,{role:'PROBE',primary:'PROBE_GSW-MG-HC',secondary:'None'}));
            const batchId = '76561198094854023';
            const archive = await f.login(batchId), authenticated = await room.admit(batchId);
            const all = await verifyAllowedColors(source,archive,authenticated);
            const result = { primary: expected.primary, secondary: expected.secondary,
                untouched: ['unconfigured-profile','cross-weapon-paint','cross-weapon-suit','explicit-none',
                    'unknown-paint','parts-only-archive','unequipped-slot','ornament-only','special-only'],
                selectedOriginalColor:restored.loadout.FirstWeaponPartData.ReceiverMainAppearance,
                variant:tiger.part, activityReceiver:eventGun.part, unrelatedRoleCannotSelectBoth:true, playerSelections:all };
            if (directory) {
                fs.mkdirSync(directory, {recursive:true});
                for (const [file, data] of Object.entries({ 'primaryLife.json': real, 'peer-cross.json': cross,
                    'parts-only.json': legacy, 'result.json': result }))
                    fs.writeFileSync(path.join(directory, file), JSON.stringify(data, null, 2) + '\n');
            }
            console.log(JSON.stringify(result));
        }, {roleSuccess:200,scratch:process.env.BRT_TEST_TMP || os.tmpdir()});
    } finally {
        if (previous===undefined) delete process.env.BRT_SPAWN_ROOMS;
        else process.env.BRT_SPAWN_ROOMS = previous;
    }
}
if (require.main === module) {
    if (!process.argv[2]) throw Error('Usage: node receiver-main.test.cjs <archive-service-source> [output-directory]');
    verify(path.resolve(process.argv[2]),process.argv[3]&&path.resolve(process.argv[3])).catch(error=>{
        console.error(error); process.exitCode=1;
    });
}
