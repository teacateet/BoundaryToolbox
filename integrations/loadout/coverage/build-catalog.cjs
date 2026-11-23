'use strict';

const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const {createHash} = require('node:crypto');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const load = file => JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const sorted = values => [...new Set(values)].sort();
const normalRoles = ['PEACE','PROBE','SNIPER','FORT','SPIKE','FIXER'];
const slotFields = ['Muzzle','Barrel','HandGuard','ReceiverUpper','Grip','SightOptical',
    'Pointer','SightIron','AmmoStorageDevice','Stock'];
const slotEnums = ['Muzzle','Barrel','HandGuard','Receiver_Upper','Grip','Sight_Optical',
    'Pointer','Sight_Iron','AmmoStorageDevice','Stock'];

function build(source, csv, retailRoles) {
    const sources = {};
    function readRows(root, relative) {
        const contents = fs.readFileSync(path.join(root,relative));
        sources[relative] = hash(contents);
        return JSON.parse(contents.toString('utf8').replace(/^\uFEFF/,''))[0].Rows;
    }
    const core = name => readRows(path.join(source,'game/definitions'),'DT_'+name+'.json');
    const retail = name => readRows(csv,name+'.json');
    const archive = core('WeaponDefinition'), raw = core('RawItemType'), types = core('ItemType');
    const parts = core('WeaponPartDefinition'), chars = core('CharacterDefinition');
    const roleTables = load(retailRoles);
    const roleScopes = Object.entries(roleTables).find(([file])=>file.endsWith('/DT_CharacterDefinition.uasset'))[1];
    const earlyAccess = Object.entries(roleTables).find(([file])=>file.endsWith('/DT_CharacterDefinition_EarlyAccess.uasset'))[1];
    const official = Object.assign({},...['Item/Release/DT_WeaponDefinition_Release',
        'Item/Release/Hardcore/DT_WeaponDefinition_Hardcore', 'Item/Release/Events/DT_WeaponDefinition_Event'].map(retail));
    assert.deepEqual(sorted(Object.keys(archive)),sorted(Object.keys(official)));
    const direct = retail('ItemIDRedirection/DT_WeaponDefinitionRedirection');
    const canonicalPart = retail('ItemIDRedirection/DT_WeaponPartRedirection');
    const suitDefinitions = retail('Skin/Release/DT_WeaponSuitDefinition_Release');
    const suitPaints = retail('Skin/Release/DT_WeaponSuitPaintingDefinition_Release');
    const skinDefinitions = retail('Skin/Release/DT_WeaponPartSkinDefinition_Release');
    const paintTargets = retail('ItemIDRedirection/DT_WeaponPartSlotPaintingRedirection');
    const canonicalPaint=new Map(Object.keys(paintTargets).map(key=>[key.toUpperCase(),key]));
    const canonicalItems=new Map(Object.keys(types).map(key=>[key.toUpperCase(),key]));
    const originalParts=retail('Item/Release/DT_WeaponPartDefinition_Release');
    const partPaints = retail('ItemIDRedirection/DT_WeaponPartSlotPaintingScope');
    const region = {};
    for (const [base, details] of Object.entries(raw))
        for (const scoped of details.RedirectionItemArray) region[scoped] = base;
    const lookup = scoped => archive[scoped] ? scoped : direct[scoped]?.WeaponDefinitionID || region[scoped];
    const roleField = {weaponScope:'WeaponScope',podScope:'PodScope',meleeWeaponScope:'MeleeWeaponScope',
        mobilityScope:'MobilityScope',spaceSuitSkinScope:'SpaceSuitSkinScope',armBadgeScope:'ArmBadgeScope',
        headAccessoryScope:'HeadAccessoryScope'};
    const roles = {};
    for (const role of normalRoles) {
        roles[role] = {};
        for (const [name,field] of Object.entries(roleField)) {
            const expected = roleScopes[role][field];
            assert.deepEqual(expected,chars[role].CustomScope[field],role+'/'+field);
            if (['WeaponScope','PodScope','MobilityScope','MeleeWeaponScope'].includes(field))
                assert.deepEqual(expected,earlyAccess[role][field],role+' EarlyAccess/'+field);
            roles[role][name] = expected;
        }
        const paints=require('./role-paint-scopes.json');
        roles[role].suitPaints = Object.fromEntries(roles[role].spaceSuitSkinScope.map(suit => [suit,paints[suit]]));
        roles[role].unlistedIndexedPaints=Object.fromEntries(roles[role].spaceSuitSkinScope.map(suit=>[suit,
            Object.keys(types).filter(id=>id.startsWith(suit+'_') && types[id].Type==='EPBItemType::CharacterSuitePainting' && !paints[suit].includes(id))]));
    }
    const weapons = {};
    for (const [base,data] of Object.entries(archive)) {
        const scope = data.CustomScope;
        assert.deepEqual(scope,official[base].CustomScope,base);
        const owners = Object.entries(roleScopes).filter(([,cs]) => cs.WeaponScope.some(id => lookup(id) === base)).map(([id])=>id);
        const obj = {owners,primaryRoles:normalRoles.filter(r=>owners.includes(r)),receiver:scope.ReceiverMain};
        const findSlot = (list,id) => list.find(x=>x.Key === 'EPBPartSlotType::'+id)?.Value;
        obj.parts = slotFields.map((name,index) => ({
            scope:name+'Scope', ids:scope[name+'Scope'],
            reticles:Object.fromEntries(scope[name+'Scope'].map(id=>{
                const original = canonicalPart[id]?.WeaponPartDefinitionID || region[id] || id;
                const specials = parts[original]?.CustomScope?.SpecialScope || [];
                assert.deepEqual(specials,originalParts[original]?.CustomScope?.SpecialScope || [],base+'/'+id);
                for (const reticle of specials) assert.equal(types[reticle]?.Type,'EPBItemType::CrossHair');
                return [id,specials];
            }).filter(([,ids])=>ids.length)),
            paints:(findSlot(partPaints[base]?.WeaponPartSlotAndSlotPaintingIDMap || [],slotEnums[index])?.SlotPaintingIDArray || [])
                .map(id=>canonicalPaint.get(id.toUpperCase())).filter(id=>id && types[canonicalItems.get(id.toUpperCase())]?.Type==='EPBItemType::WeaponSlotPainting')
        }));
        obj.suits = {};
        for (const suit of scope.SuitScope) {
            const definition = suitDefinitions[suit];
            if (!definition) continue;
            obj.suits[suit] = { skins:slotEnums.map(n=>findSlot(definition.PartSlotAndSkinIDMap,n)||'None'),
                overrides:slotEnums.map(n=>findSlot(definition.PartSlotAndIDMap,n)||null),
                receiverSkin:findSlot(definition.PartSlotAndSkinIDMap,'ReceiverMain') || 'None',
                receiverOverride:findSlot(definition.PartSlotAndIDMap,'ReceiverMain') || null,
                paints:Object.fromEntries(definition.WeaponSuitPaintingIDArray.filter(id=>suitPaints[id]).map(id=>
                    [id,findSlot(suitPaints[id].SlotAndWeaponPartSkinPaintingIDMap,'ReceiverMain') || null])) };
        }
        obj.ornaments = scope.OrnamentScope;
        // 当前部位同时要求有形状、允许的部位外皮，以及该外皮与部位共同允许的涂装。
        for (const [i,part] of obj.parts.entries()) {
            part.looks=[];
            const identities=new Set();
            for (const details of Object.values(obj.suits)) {
                const skin=details.skins[i], required=details.overrides[i];
                if (!skinDefinitions[skin] || !part.ids.length) continue;
                const grant = new Set(skinDefinitions[skin].SkinPaintingIDArray.map(x=>x.toUpperCase()));
                const accepted=part.paints.filter(id=>grant.has(paintTargets[id].SkinPaintingDefinitionID.toUpperCase()));
                for (const paint of ['',...accepted]) {
                    const pick=required && required!=='None' ? part.ids.find(id=> id===required ||
                        (canonicalPart[id]?.WeaponPartDefinitionID || region[id] || id)===required) : undefined;
                    if (required && required!=='None' && !pick) continue;
                    const key=JSON.stringify([skin,paint,pick || null]);
                    if (!identities.has(key)) {
                        identities.add(key); part.looks.push({skin,paint,part:pick || null});
                    }
                }
            }
        }
        weapons[base] = obj;
    }
    const grant = {};
    for (const [role,data] of Object.entries(roles))
        for (const item of data.weaponScope) {
            const base = lookup(item);
            assert.ok(weapons[base].primaryRoles.includes(role));
            grant[item] = base;
        }
    return { schema:1, normalRoles, slotFields, roles, grants:grant, weapons, sources };
}
if (require.main === module) {
    const [source,csv,retailRoles,output] = process.argv.slice(2);
    if (!output) throw Error('Usage: node build-catalog.cjs <archive-service-source> <retail-CSVConfig-exports> <retail-role-scopes.json> <catalog.json>');
    const result = build(source,csv,retailRoles);
    fs.writeFileSync(output,JSON.stringify(result)+'\n');
    console.log('PASS official/store scopes',Object.keys(result.weapons).length,'weapons',Object.keys(result.grants).length,'role weapons');
}
module.exports = {build};
