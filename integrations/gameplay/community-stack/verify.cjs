'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {createHash} = require('node:crypto');
const run = path.resolve(process.argv[2]);
const output = path.resolve(process.argv[3]);
const load = file => JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const digest = data => createHash('sha256').update(data).digest('hex');
const source = file => fs.readFileSync(file,'utf8').replace(/\r\n/g,'\n');
const matrix = load(path.join(run,'matrix.json'));
function symbols(file) {
    const parsed = {defined:new Set(), needed:new Set()};
    for (const line of source(file).split('\n')) {
        const match = /\b(UNDEF|SECT[0-9A-F]+)\s+.*?External\s+\|\s+(\?\S+)/.exec(line);
        if (match) parsed[match[1]==='UNDEF'?'needed':'defined'].add(match[2]);
    }
    return parsed;
}
function expectSymbol(table, kind, prefix) {
    const found = [...table[kind]].filter(full => full.startsWith('?'+prefix));
    assert.ok(found.length, 'Missing '+kind+' symbol '+prefix);
    return found;
}
const receipts=matrix.map(row => {
    const release = row.tag === 'v084';
    const dir=path.join(run,'objects-'+row.tag);
    const archive=load(path.join(dir,'library.json'));
    const omissionControl=load(path.join(dir,'export-control.json'));
    assert.equal(omissionControl.presentInFull,true);
    assert.equal(omissionControl.presentInOmission,false);
    assert.equal(omissionControl.positiveMembers,archive.members.length);
    assert.equal(omissionControl.omissionMembers,archive.members.length-1);
    assert.equal(digest(fs.readFileSync(path.join(dir,archive.library))),archive.sha256);
    const objectSources=Object.fromEntries(row.entries.map(rel=>[path.basename(rel,'.cpp'),rel]));
    const all=Object.fromEntries(Object.keys(objectSources).map(n=>[n,symbols(path.join(dir,n+'.obj.symbols'))]));
    const entries=Object.fromEntries(Object.entries(row.parts).map(([key,rel])=>[key,path.basename(rel,'.cpp')]));
    assert.deepEqual(Object.keys(all).sort(),archive.members.map(n=>path.basename(n,'.obj')).sort());
    const businessChecks=[];
    function check(object,kind,names) {
        for (const name of names) {
            expectSymbol(all[object],kind,name);
            businessChecks.push({object,symbol:name,role:kind});
        }
    }
    const edges=[];
    function link(caller,callee,name,objectTable=all) {
        const needed=expectSymbol(objectTable[caller],'needed',name);
        const supplied=needed.filter(symbol=>objectTable[callee]?.defined.has(symbol));
        assert.ok(supplied.length,'No ABI-matching definition for '+name+' in '+callee);
        return {caller,callee,name,decorated:supplied};
    }
    check(entries.ui,'defined',['HandleUICharacterClientEvent@@','OnCharacter@BrtSparrowReticle@@','OnCharacter@BrtHeadRadar@@',
        'OnCharacter@BrtPodView@@','Reconcile@BrtPodView@@','OnCharacter@BrtGrappleInput@@']);
    check(entries.ui,'needed',['SetRenderOpacity@UWidget@','GetRenderOpacity@UWidget@']);
    check(entries.ui,'needed',['GetCurrentMobilityModule@APBCharacter@','GetKeysFromActionName@UPBOptions@',
        'WasInputKeyJustPressed@APlayerController@','WasInputKeyJustReleased@APlayerController@','GetIsAllowedCatch@APBCharacter@']);
    check(entries.presentation,'defined',['HandleLauncherClientEvent@@']);
    check(entries.presentation,'needed',['K2_ASingleAmmoReloaded@APBLauncher@','GetOwnerPawn@APBAttachables@','IsAlive@APBCharacter@']);
    check(entries.tick,'defined',['NoteServerGameTick@@','Tick@grapple@brt@@']);
    check(entries.tick,'needed',['GetCurrentMobilityModule@APBCharacter@','GetCoolingDownPercent@APBCatchingModule@','GetDeaths@APBPlayerState@']);
    check('OwnerLogin','defined',['BindVerified@grapple@brt@@','BrtTickPendingOwnerReconnect@@']);
    check('OwnerLogin','needed',['GetDefaultIDStr@APBPlayerState@','ServerPreOrderInventory@APBPlayerController@']);
    check('Birth','needed',['InitWeapon@APBWeapon@','GetWeaponArray@APBCharacter@']);
    const deathOwner=release?entries.ui:entries.death;
    check(deathOwner,'needed',['OpenInGameSelectRoleMenu@APBPlayerController@']);
    if (release) {
        check(entries.ui,'defined',['CharacterTick@BrtDeathMenuV084@@']);
        check(entries.death,'defined',['Begin@BrtDeathMenuTravel@@','Complete@BrtDeathMenuTravel@@']);
    } else {
        check(entries.death,'defined',['Tick@BrtDeathMenu@@','Travel@BrtDeathMenu@@']);
    }
    for (const [caller,callee,func] of [
        [entries.login,'OwnerLogin','BrtPreorderOwnerInventory@@'],
        [entries.login,'OwnerLogin','BrtTickPendingOwnerReconnect@@'],
        [entries.login,'Birth','BrtTickOwnerBirth@@'],
        ['OwnerLogin','Birth','BrtWatchOwnerBirth@@'],
        ['OwnerLogin','RoomHttp','ReconnectMenu@RoomHttp@'],['OwnerLogin','RoomHttp','MenuSubmitted@RoomHttp@'],
        [entries.ui,'UMG_HelmetHUD_MiniMap_functions','K2_AddWidgetToCanvasPanel@UUMG_HelmetHUD_MiniMap_C@'],
        [entries.ui,'UMG_MinimapNode_functions','UpdateNode@UUMG_MinimapNode_C@'],
        [deathOwner,'UMG_Prompt_functions','BP_OnHandleBackAction@UUMG_Prompt_C@']
    ]) edges.push(link(caller,callee,func));
    const missingPrompt={...all,UMG_Prompt_functions:{defined:new Set(),needed:new Set()}};
    assert.throws(()=>link(deathOwner,'UMG_Prompt_functions','BP_OnHandleBackAction@UUMG_Prompt_C@',missingPrompt),
        /No ABI-matching definition/);
    for (const part of row.applied) {
        assert.equal(digest(source(path.resolve(__dirname,'..',part.patch))),part.sha256,'Patch changed after compile');
    }
    for (const [rel,expected] of Object.entries(row.finalUnits)) {
        assert.equal(digest(source(path.join(row.top,rel))),expected,'Business source changed after prepare');
    }
    for (const h of row.localHeaders) {
        const localFile=path.join(path.dirname(path.join(row.top,row.parts.ui)),h.file);
        assert.equal(digest(source(localFile)),h.sha256,'Build header changed');
        assert.equal(digest(source(path.resolve(__dirname,'..',h.module,h.file))),h.sha256,'Maintenance header changed');
    }
    return {version:row.tag,upstreamCommit:row.commit,patches:row.applied,upstreamGitBlobs:row.baseline,
        maintenanceHeaders:row.localHeaders,finalSourceSHA256:row.finalUnits,features:['inventory','birth','grapple-observation','reconnect','first-life',
            'side-mount-reload','sparrow-reticle','marked-head-radar','death-menu','pod-view','grapple-input-observation'],compiledSources:row.entries,
        archive,omissionControl,entryChecks:businessChecks,matchingConsumerProviderSymbols:edges,
        rejectedControls:[...row.rejected,'missing prompt implementation']};
});
assert.equal(receipts.length,2);
const report={scope:'Combined ordinary community business and UMG source objects in two static libraries.',
    runtime:'No game session was run.',versions:receipts};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');
for(const r of receipts) console.log('PASS '+r.version+': '+r.compiledSources.length+' selected sources, '+r.patches.length+
    ' patches, '+r.matchingConsumerProviderSymbols.length+' exact object symbol matches, '+r.rejectedControls.length+' negative controls.');
