'use strict';
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const {createHash} = require('node:crypto');

const inputsPath = path.resolve(process.argv[2]);
const config = JSON.parse(fs.readFileSync(inputsPath, 'utf8'));
const out = path.resolve(process.argv[3]);
fs.mkdirSync(out, {recursive:true});
const run = fs.mkdtempSync(path.join(out, 'run-'));
const kits = path.dirname(__dirname);
const mainCommit = '74c68ac3e6007c36248c442a281c2b7b831cc53a';
const releaseCommit = 'b3d7f83f3789111d4a460b9662a8dc4e06e6a934';
const releaseBlobs = {
    'Payload.vcxproj':'ae80fa85d92465b277a81da740aa014a25bd9e38',
    'ServerLogic/LateJoinManager.cpp':'8040081b49815a5d7dfc5e80eb8412a785c51704',
    'ServerLogic/LateJoinManager.h':'fd997dc674aa0de84798b19d88fb4a3282da93ca',
    'ServerLogic/ServerLogic.cpp':'a6160f6e96a5b3811b69b04d26c085381ed38fe3',
    'ClientLogic/ClientLogic.cpp':'6ea2268dd97f70d388fae05c433604926f234c4e',
    'Utility/LauncherFix.cpp':'7d7260f1f5401c2815cf1cd365bb6d72328adfd7',
    'Utility/UIFix.cpp':'af1112ffc38387d8be57b05e94b4e9ec59b21659',
    'SDK/UMG_HelmetHUD_MiniMap_functions.cpp':'b489cdac5581edc77ba08c8efc6648558510eadc',
    'SDK/UMG_MinimapNode_functions.cpp':'c093bb8eefaf63adee833dea316c99c5268c622d',
    'SDK/UMG_Prompt_functions.cpp':'05e843b220374b15ba87e596425b2cbd0f41ab3f'
};
function read(file) {return fs.readFileSync(file,'utf8').replace(/\r\n/g,'\n');}
function git(dir,args,expectedSuccess=true) {
    const result=cp.spawnSync('git',['-c','core.autocrlf=false',...args],{cwd:dir,encoding:'utf8'});
    if (result.error) throw result.error;
    assert.equal(result.status === 0,expectedSuccess,'Git '+args.join(' ')+'\n'+result.stdout+result.stderr);
    return (result.stdout+result.stderr).trim();
}
function hash(data) {return createHash('sha256').update(data).digest('hex');}
function gitBlob(content) {
    const bytes=Buffer.from(content,'utf8');
    return createHash('sha1').update('blob '+bytes.length+'\0').update(bytes).digest('hex');
}
function copy(src,dest) {fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,read(src));}
function cpp(name) {return 'SDK/'+name+'_functions.cpp';}
const widgets=['UMG_HelmetHUD_MiniMap','UMG_MinimapNode','UMG_Prompt'].map(cpp);
const matrix=[];
for (const tag of ['v084','main']) {
    const release=tag==='v084', mod=release?'Payload':'ProjectReboundMainDLL';
    const dir=path.join(run,tag), top=path.join(dir,mod);
    const compiledRoot=release?config.releaseRoot:path.join(config.checkout,mod);
    const sdk=release?config.releaseSdk:compiledRoot;
    const parts=release?{
        login:'ServerLogic/LateJoinManager.cpp',tick:'ServerLogic/ServerLogic.cpp',
        ui:'Utility/UIFix.cpp',presentation:'Utility/LauncherFix.cpp',death:'ClientLogic/ClientLogic.cpp'
    }:{
        login:'Server/LateJoin.cpp',tick:'Server/RoundManager.cpp',
        ui:'Client/UIShake.cpp',presentation:'Client/SideMountFixClient.cpp',death:'Client/AutoConnect.cpp'
    };
    fs.mkdirSync(top,{recursive:true});git(dir,['init','--quiet']);
    const baseline={};
    const loginHeader = release ? 'ServerLogic/LateJoinManager.h' : 'Server/LateJoin.h';
    for (const rel of ['Payload.vcxproj',loginHeader,...Object.values(parts),...widgets]) {
        const source=rel.startsWith('SDK/')?path.join(sdk,rel):
            release && config.releaseFiles?.[rel]?path.resolve(path.dirname(inputsPath),config.releaseFiles[rel]):path.join(compiledRoot,rel);
        const data=read(source);
        const expected=release?releaseBlobs[rel]:git(config.checkout,['rev-parse',mainCommit+':'+mod+'/'+rel]);
        assert.equal(gitBlob(data),expected,'wrong clean source '+rel);
        baseline[rel]=expected;
        // SDK 的三个真实 UMG 实现留在已核验的只读基线，由编译器直接引用。
        if (!rel.startsWith('SDK/')) copy(source,path.join(top,rel));
    }
    const uiDir=path.dirname(path.join(top,parts.ui));
    const layout=release?'v084':'split';
    const patches=[
        ['inventory','community-inventory',tag+'-community.patch'],
        ['birth','community-weapon-birth',tag+'-birth.patch'],
        ['grapple','community-grapple',tag+'-community.patch'],
        ['reconnect','inventory-reconnect',tag+'-reconnect.patch'],
        ['first-life','first-life',tag+'.patch'],
        ['presentation','community-presentation',layout+'.patch'],
        ['reticle','community-reticle',layout+'.patch'],
        ['radar','community-radar',layout+'.patch'],
        ['death-menu','community-death-menu',layout+'.patch'],
        ['death-menu-sdk','community-death-menu','sdk-'+layout+'.patch'],
        ['pod-view','community-pod-view',layout+'.patch'],
        ['grapple-input','community-grapple-input',layout+'.patch']
    ].map(([name,module,file])=>({name,relative:module+'/'+file,file:path.join(kits,module,file)}));
    const rejected=[];
    for (const id of ['reconnect','radar']) {
        const fail=git(dir,['apply','--check',patches.find(p=>p.name===id).file],false);
        assert.match(fail,/does not exist|patch does not apply|No such file or directory/);
        rejected.push(id+' before prerequisites');
    }
    const applied=[];
    for (const patch of patches) {
        git(dir,['apply','--check',patch.file]);
        git(dir,['apply','--whitespace=nowarn',patch.file]);
        git(dir,['apply','--reverse','--check',patch.file]);
        applied.push({name:patch.name,patch:patch.relative,sha256:hash(read(patch.file))});
    }
    git(dir,['apply','--check',patches[0].file],false);
    rejected.push('duplicate inventory');
    const localHeaders=[['community-reticle','SparrowReticle.hpp'],['community-radar','MarkedHeadRadar.hpp'],['community-radar','UiJournal.hpp'],
        ['community-death-menu','DeathMenu.hpp'],['community-death-menu','DeathMenu.sdk.hpp'],
        ['community-pod-view','PodView.hpp'],['community-grapple-input','GrappleInput.hpp']];
    if (release) localHeaders.push(['community-death-menu','DeathMenuTravel.hpp'],['community-death-menu','DeathMenu.v084.hpp']);
    for (const [module,h] of localHeaders) copy(path.join(kits,module,h),path.join(uiDir,h));
    // 发行版的原登录源文件通过 ../Libs/json.hpp 使用同一份公开 JSON 头。
    if (release && !fs.existsSync(path.join(compiledRoot,'Libs/json.hpp')))
        copy(path.join(config.checkout,'ProjectReboundMainDLL/Libs/json.hpp'),path.join(top,'Libs/json.hpp'));
    const sources = Object.fromEntries(Object.entries(parts).map(([name,rel])=>[name,read(path.join(top,rel))]));
    const owner=read(path.join(top,'BrtInventory/OwnerLogin.cpp'));
    assert.match(sources.ui,/BrtSparrowReticle::OnCharacter/); assert.match(sources.ui,/BrtHeadRadar::OnCharacter/);
    for (const call of ['BrtPodView::OnCharacter(', 'BrtGrappleInput::OnCharacter(']) {
        const at=sources.ui.indexOf(call);
        assert.ok(at>=0 && at<sources.ui.indexOf('if (!UIFixDebugEnabled)'), 'missing UI event before debug gate: '+call);
    }
    assert.match(sources.login,/BrtPreorderOwnerInventory\(GameMode, PC\)/); assert.equal(sources.login.split('BrtTickOwnerBirth();').length-1,3);
    assert.match(sources.tick,/brt::grapple::Tick\(/);
    assert.match(owner,/BrtWatchOwnerBirth\(/);assert.match(owner,/brt::grapple::BindVerified\(/);assert.match(owner,/ServerPreOrderInventory\(/);
    assert.match(sources.presentation,/EPBHandsState::DeployedLauncherController/);
    if (release) {
        assert.match(sources.ui,/BrtDeathMenuV084::CharacterTick\(/);
        assert.equal(sources.death.split('BrtDeathMenuTravel::Complete(').length-1,2);
    } else {
        assert.match(sources.death,/BrtDeathMenu::Tick\(/); assert.match(sources.death,/BrtDeathMenu::Travel\(/);
    }
    const manifest=read(path.join(top,'Payload.vcxproj'));
    const entries=[...Object.values(parts),'BrtInventory/OwnerLogin.cpp','BrtInventory/Birth.cpp','BrtInventory/RoomHttp.cpp',...widgets];
    const included=[...manifest.matchAll(/<ClCompile\s+Include="([^"]+)"/g)].map(m=>m[1].replaceAll('\\','/'));
    for (const rel of entries) assert.equal(included.filter(x=>x===rel).length,1,'project build item '+rel);
    matrix.push({tag,commit:release?releaseCommit:mainCommit,mod,top,compiledRoot,sdk,parts,baseline,applied,rejected,
        localHeaders:localHeaders.map(([d,f])=>({module:d,file:f,sha256:hash(read(path.join(kits,d,f)))})),entries,
        finalUnits:Object.fromEntries(Object.entries(parts).map(([n,p])=>[p,hash(sources[n])])),widgets});
}
fs.writeFileSync(path.join(run,'matrix.json'),JSON.stringify(matrix,null,2)+'\n');
fs.writeFileSync(path.join(out,'current-run.txt'),run+'\n');
console.log(run);
