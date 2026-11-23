'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {randomBytes,createHash}=require('node:crypto');
const [sourcePath,oldPath,newPath,outfile]=process.argv.slice(2);
const source=fs.realpathSync(sourcePath), old=fs.realpathSync(oldPath), fresh=fs.realpathSync(newPath);
const use=(root,name)=>require(path.join(root,'integrations/loadout',name+'.cjs'));
const hash=f=>createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const {fixture}=use(fresh,'fixture'),{createRoomSpawnClient}=use(fresh,'room-spawn-client');
async function main(){
    const key=randomBytes(32).toString('hex'), previous=process.env.BRT_SPAWN_ROOMS;
    process.env.BRT_SPAWN_ROOMS=JSON.stringify({migrate:key});
    const result={};
    try {await fixture(source,async f=>{
        fs.symlinkSync(fs.realpathSync(path.join(source,'node_modules')),path.join(f.dir,'node_modules'),process.platform==='win32'?'junction':'dir');
        const staging=fs.mkdtempSync(path.join(f.root,'upgrade-'));
        const primary=use(old,'fixture-kit').managed(f.root,f.dir), upgraded=use(fresh,'fixture-kit').managed(staging,f.dir);
        assert.equal(primary.loadoutDir,upgraded.loadoutDir);
        await f.start(primary.appDir);
        const owner='76561198139410235', role='PEACE';
        const pick={role,primary:'PEACE_RU-AKM',secondary:'PEACE_GSW-DMR'};
        const person=await f.login(owner);
        assert.equal(await person.roleStatus(role,1,pick.primary),200);
        assert.equal(await person.roleStatus(role,2,pick.secondary),200);
        assert.equal(await person.roleStatus(role,7,{SkinItem:'PEACE_ORIGINAL',PaintItem:'PEACE_ORIGINAL_PTBotany'}),200);
        assert.equal(await person.roleStatus(role,9,'ABGNoEntry'),200);
        result.oldAuthenticatedWrite='accepted out-of-scope color';
        person.close();await f.stop();
        const archiveFile=path.join(primary.loadoutDir,owner+'.json'), before=hash(archiveFile);
        await f.start(upgraded.appDir);
        const reconnected=await f.login(owner);
        const member=await createRoomSpawnClient({origin:f.origin(),roomId:'migrate',credential:key}).admit(owner);
        const body=await member.prepare(1,pick);
        assert.equal(body.CharacterData.SpacesuitID,'PEACE_ORIGINAL');
        assert.equal(body.CharacterData.SpacesuitSkinPaintingID,'None');
        assert.equal(body.CharacterData.ArmBadgeID,'ABGNoEntry');
        assert.deepEqual([body.FirstWeaponPartData.WeaponID,body.SecondWeaponPartData.WeaponID],[pick.primary,pick.secondary]);
        assert.equal(hash(archiveFile),before);
        result.afterUpgrade={actor:body.CharacterID,paint:body.CharacterData.SpacesuitSkinPaintingID,
            badge:body.CharacterData.ArmBadgeID,weapons:[pick.primary,pick.secondary],savedBytesPreserved:true};
        assert.equal(await reconnected.roleStatus(role,7,{SkinItem:'PEACE_ORIGINAL',PaintItem:'PEACE_ORIGINAL_PTBotany'}),1);
        assert.equal(hash(archiveFile),before);
        assert.equal(await reconnected.roleStatus(role,7,{SkinItem:'PEACE_ORIGINAL',PaintItem:'PEACE_ORIGINAL_PTMetalGold'}),200);
        assert.equal((await member.prepare(2,pick)).CharacterData.SpacesuitSkinPaintingID,'PEACE_ORIGINAL_PTMetalGold');
        result.nextValidPaint='PEACE_ORIGINAL_PTMetalGold';
    },{scratch:os.tmpdir(),roleSuccess:200});}
    finally {if(previous===undefined)delete process.env.BRT_SPAWN_ROOMS;else process.env.BRT_SPAWN_ROOMS=previous;}
    fs.writeFileSync(outfile,JSON.stringify(result,null,2)+'\n');
    console.log('PASS historical native paint, private birth, immutable archive, positive replacement');
}
main().catch(e=>{console.error(e.message);process.exitCode=1;});
