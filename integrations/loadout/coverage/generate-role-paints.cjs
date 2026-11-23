'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const [service,pakFile,rawFile,destination]=process.argv.slice(2);
if(!destination)throw Error('Usage: node generate-role-paints.cjs <server-source> <retail-role-scopes.json> <retail-equipment-class-refs.json> <output>');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const pak=read(pakFile),skin=read(rawFile), actual=skin.DT_SpaceSuitSkinDefinition, launch=skin.DT_SpaceSuitSkinDefinition_Release;
const roles=Object.entries(pak).find(([p])=>p.endsWith('/DT_CharacterDefinition.uasset'))[1];
const items=read(path.join(service,'game/definitions/DT_ItemType.json'))[0].Rows;
const aliases=new Map();
for(const [id,record] of Object.entries(items))if(record.Type==='EPBItemType::CharacterSuitePainting'){
    assert.ok(!aliases.has(id.toUpperCase()),id);aliases.set(id.toUpperCase(),id);
}
const out={};
for(const role of ['PEACE','PROBE','SNIPER','FORT','SPIKE','FIXER']){
    for(const key of roles[role].SpaceSuitSkinScope){
        const allowed=actual[key].SpaceSuitSkinPaintingIDArray;
        assert.deepEqual(allowed,launch[key].SpaceSuitSkinPaintingIDArray);
        const normalized=allowed.map(value=>{const name=aliases.get(value.toUpperCase());assert.ok(name,value);return name;});
        assert.equal(normalized.length,new Set(normalized).size);
        if(out[key])assert.deepEqual(out[key],normalized);
        out[key]=normalized;
    }
}
fs.writeFileSync(destination,JSON.stringify(out,null,2)+'\n');
console.log('Joined retail suit and ItemType spelling:',Object.keys(out).length,'suits,',Object.values(out).flat().length,'legal paints');
