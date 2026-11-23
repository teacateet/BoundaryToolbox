'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const {randomBytes,randomUUID,createHash} = require('node:crypto');
const {isDeepStrictEqual} = require('node:util');
const {weaponPlan,readWeapon,normal} = require('./weapon-cases.cjs');
const {checkRoleAppearance,characterView}=require('./role-appearance.cjs');
const catalog = require('./data/catalog.json');
const csvSort = seq => [...seq].sort();
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const disk = p => fs.readFileSync(p);
const roles = catalog.normalRoles;
const nativeName = id => id === 'SNIPER' ? 'Sniper' : id;
const nativeFields = ['PrimaryWeapon','SecondWeapon','MeleeWeapon','MobilityModule','LeftPylon','RightPylon'];
const lookupSlots = ['weaponScope','weaponScope','meleeWeaponScope','mobilityScope','podScope','podScope'];
const roomEquipment = row => [row.FirstWeaponPartData.WeaponID,row.SecondWeaponPartData.WeaponID,
    row.MeleeWeaponData.ID,row.MobilityModuleData.MobilityModuleID,row.LeftLauncherData.ID,row.RightLauncherData.ID];
const configs = row => [row.FirstWeaponPartData,row.SecondWeaponPartData];
const input = process.argv.slice(2);
if (input.length < 2) throw Error('Usage: node all-loadouts.test.cjs <archive-service-source> <toolbox-source> [output.json] [--role-only]');
const source = fs.realpathSync(input[0]), kit = fs.realpathSync(input[1]);
const output = path.resolve(input[2] || 'loadout-all-results.json');
const onlyRole = input.includes('--role-only');
const lib = path.join(kit,'integrations/loadout');
const {fixture} = require(path.join(lib,'fixture.cjs'));
const {managed} = require(path.join(lib,'fixture-kit.cjs'));
const {createRoomSpawnClient} = require(path.join(lib,'room-spawn-client.cjs'));
const report = {schema:1,complete:false,failures:{},assertions:0,counts:{},coverage:{},roleSummary:{},weapons:{},build:{}};
const counted = new Map();
function count(key,delta=1) { report.counts[key]=(report.counts[key]||0)+delta; }
function cover(key,value) { if (!counted.has(key)) counted.set(key,new Set()); counted.get(key).add(value); }
function same(kind,where,actual,expected) {
    ++report.assertions;
    if (isDeepStrictEqual(actual,expected)) return true;
    const entry=report.failures[kind] ||= {count:0,examples:[]};
    entry.count++;
    if (entry.examples.length<3) entry.examples.push({where,actual:actual??null,expected:expected??null});
    return false;
}
function save() {
    for (const [type,set] of counted) report.coverage[type]=set.size;
    fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');
}
const t={same};
const owner='76561198231063043',peer='76561197988188971';
async function exercise(f,secret) {
    fs.symlinkSync(fs.realpathSync(path.join(source,'node_modules')),path.join(f.dir,'node_modules'),
        process.platform==='win32'?'junction':'dir');
    const {appDir,loadoutDir}=managed(f.root,f.dir);
    assert.ok(!fs.existsSync(loadoutDir));
    for (const name of ['index.js','game/loadoutStore.js','room-spawn.cjs','spawn-parts.cjs','receiver-main.json']) {
        const file=path.join(appDir,name);
        if (fs.existsSync(file)) report.build[name]=hash(disk(file));
    }
    await f.start(appDir);
    count('serviceStarts');
    let account=await f.login(owner);
    const control=await f.login(peer);
    count('tcpLogins',2);
    const allNative=async (client, ids=roles)=>Object.fromEntries(
        (await client.archive(...ids.map(nativeName))).map(row=>[row.RoleID.toUpperCase(),row]));
    // 存储创建走经过认证的正常原生写入，使二号玩家保持独立的已知记录。
    for (const role of roles) {
        for (const client of [account,control]) {
            const id=catalog.roles[role].weaponScope[0];
            const rc=await client.roleStatus(nativeName(role),1,id);
            same('seed-native-status',role,rc,200);
        }
    }
    const room=createRoomSpawnClient({origin:f.origin(),roomId:'coverage',credential:secret});
    const connectionId=randomUUID();
    let roomA=await room.admit(owner,connectionId), roomB=await room.admit(peer);
    let life=0,peerLife=0;
    const choice=(role,primary,secondary)=>({role,primary,secondary});
    async function spawn(selection, asPeer=false) {
        const member=asPeer?roomB:roomA;
        const seq=asPeer?++peerLife:++life;
        const result=await member.prepareEnvelope(seq,selection);
        count('privateBirthSnapshots');
        same('authenticated-snapshot',selection.role,[result.life,result.authenticatedSteamId,result.loadout.CharacterID],
            [seq,asPeer?peer:owner,selection.role]);
        return result;
    }
    const initialPeer=await allNative(control);
    const peerBirths={};
    for (const role of roles) {
        const row=initialPeer[role];
        const pick=choice(role,row.PrimaryWeapon,row.SecondWeapon);
        peerBirths[role]=(await spawn(pick,true)).loadout;
    }
    const isolated=async (label,previous)=>{
        const now=await allNative(account);
        for (const [name,last] of Object.entries(previous)) {
            if (name!==label) same('neighbor-role-archive',label+'/'+name,hash(Buffer.from(JSON.stringify(now[name]))),last);
        }
        return Object.fromEntries(Object.entries(now).map(([name,row])=>[name,hash(Buffer.from(JSON.stringify(row)))]));
    };
    const defs=await f.http('GET','/api/definitions/weapons');
    same('all-weapon-definitions','all',csvSort(defs.body.weapons),csvSort(Object.keys(catalog.weapons)));
    for (const [base,meta] of Object.entries(catalog.weapons)) {
        const got=await f.http('GET','/api/definitions/weapons/'+base);
        same('weapon-live-definition',base,got.status,200);
        for (const slot of meta.parts)
            same('weapon-live-slot-options',base+'/'+slot.scope,csvSort(got.body.slotScopes[slot.scope] || []),csvSort(slot.ids));
        report.weapons[base]={normalRoles:meta.primaryRoles,otherDefinedRoles:meta.owners.filter(r=>!roles.includes(r))};
    }
    let neighbors=await isolated('',{}); const finalByRole={}; const finalSettings={}, finalLooks={};
    for (const role of roles) {
        const archiveRole=nativeName(role),allow=catalog.roles[role];
        const live=(await f.http('GET','/api/definitions/roles/'+archiveRole)).body;
        for (const field of Object.keys(allow).filter(x=>x!=='suitPaints'&&x!=='unlistedIndexedPaints'))
            same('role-live-permission',role+'/'+field,csvSort(live[field]),csvSort(allow[field]));
        // 每把六人无权使用的枪也分别经当前角色的真实写入、出生入口检查。
        for (const [base,meta] of Object.entries(catalog.weapons)) {
            if (meta.primaryRoles.includes(role)) continue;
            const name=role+'_'+base;
            for (const op of [1,2]) {
                same('reject-forbidden-weapon-rpc',role+'/'+base+'/'+op,await account.roleStatus(archiveRole,op,name),1);
                count('deniedNativeWeaponSelections');
            }
            const response=await fetch(f.origin()+'/api/brt/v1/rooms/coverage/spawn/prepare',{
                method:'POST',headers:{Authorization:'Bearer '+secret,'Content-Type':'application/json'},
                body:JSON.stringify({connectionId,life:life+1,selection:choice(role,name,'None')})});
            const issue=await response.json();
            same('reject-forbidden-weapon-room',role+'/'+base,[response.status,issue.error],[422,'weapon-outside-selected-role']);
            count('deniedRoomWeaponSelections');
        }
        const getRole=async()=> (await account.archive(archiveRole))[0];
        const writeSlot=async(op,id)=>{
            same('role-equip-ack',role+'/'+op+'/'+id,await account.roleStatus(archiveRole,op,id),200);
            count('nativeRoleWrites');
        };
        for (const op of [1,2]) await writeSlot(op,'None');
        const unarmedNative=await getRole(), unarmed=(await spawn(choice(role,'None','None'))).loadout;
        same('both-weapons-unequipped-native',role,[unarmedNative.PrimaryWeapon,unarmedNative.SecondWeapon],['None','None']);
        same('both-weapons-unequipped-private',role,configs(unarmed).map(x=>[x.WeaponID,x.WeaponPartConfigs.length]),
            [['None',0],['None',0]]);
        cover('role-unarmed',role);
        for (const op of [3,4,5,6]) {
            for (const id of [...allow[lookupSlots[op-1]],'None']) {
                await writeSlot(op,id);
                const native=await getRole();
                const saved=await spawn(choice(role,'None','None'));
                same('equipment-native',role+'/'+op+'/'+id,normal(native[nativeFields[op-1]]),id);
                same('equipment-spawn',role+'/'+op+'/'+id,roomEquipment(saved.loadout)[op-1],id);
                cover('role-slot-equipment-or-clear',role+'/'+op+'/'+id);
            }
        }
        finalLooks[role]=await checkRoleAppearance({role,archiveRole,allowed:allow,client:account,roomSpawn:spawn,
            selection:choice(role,'None','None'),observe:getRole,count,cover,same});
        const guns=allow.weaponScope;
        const prepared=new Map(), roleStarted=life;
        if (!onlyRole) for (let pos=0;pos<guns.length;pos+=2) {
            const group=guns.slice(pos,pos+2);
            const plans=group.map(id=>weaponPlan(id,catalog.grants[id],catalog.weapons[catalog.grants[id]]));
            const lanes=[group[0],group[1]||'None'];
            await writeSlot(1,lanes[0]); await writeSlot(2,lanes[1]);
            let last;
            for (let i=0;i<Math.max(...plans.map(plan=>plan.rounds));i++) {
                for (const plan of plans) {
                    if (i>=plan.rounds) continue;
                    const update=plan.get(i);
                    same('native-weapon-status',role+'/'+plan.id+'/'+i,await account.weaponStatus(archiveRole,update),0);
                    count('nativeWeaponWrites');
                    prepared.set(plan.id,update);
                    cover('role-weapon',plan.id);
                    cover('whole-suit-paint',plan.base+'/'+update.Skin.SkinInfo.Type+'/'+update.Skin.SkinInfo.Id);
                    cover('whole-weapon-charm',plan.base+'/'+update.Skin.WeaponOrnament);
                    for (const part of update.Parts) {
                        if (part.PartId) {
                            cover('base-slot-selectable-part',plan.base+'/'+part.SlotId+'/'+part.PartId);
                            cover('role-gun-slot-selectable-part',plan.id+'/'+part.SlotId+'/'+part.PartId);
                        }
                        const look=part.Ornament;
                        if (look?.SpecialSlot) cover('optic-reticle',part.PartId+'/'+look.SpecialSlot);
                        if (look?.Info?.Type) cover('part-skin',plan.base+'/'+part.SlotId+'/'+look.Info.Type);
                        if (look?.Info?.Id) cover('slot-paint',plan.base+'/'+part.SlotId+'/'+look.Info.Id);
                    }
                }
                const rr=await getRole();
                const pick=choice(role,...lanes);
                last=await spawn(pick);
                const actual=configs(last.loadout);
                for (const [slot,plan] of plans.entries()) {
                    const up=prepared.get(plan.id);
                    readWeapon(t,role+'/'+plan.id+'/'+i,up,rr.WeaponConfig.Weapons.find(w=>w.WeaponId===plan.id),actual[slot]);
                    if (i===0) cover('role-arm-slot',plan.id+'/'+(slot+1));
                    if (!report.build['receiver-main.json']) continue;
                    const {Type,Id}=up.Skin.SkinInfo, suit=plan.gun.suits[Type];
                    // 部分版本将上一代自动出厂档与显式写入同值视为相同，单列默认色。
                    const stock=Type===Object.keys(plan.gun.suits)[0] && Id===plan.base+'_Original_PTOriginal';
                    if (!stock && suit?.paints[Id])
                        same('receiver-authorized-paint',role+'/'+plan.id+'/'+Type+'/'+Id,actual[slot]?.ReceiverMainAppearance,
                            {WeaponPartID:suit.receiverOverride||plan.gun.receiver,WeaponPartSkinID:suit.receiverSkin,
                                WeaponPartSkinPaintingID:suit.paints[Id]});
                }
            }
            const swapped=[lanes[1],lanes[0]];
            await writeSlot(1,swapped[0]); await writeSlot(2,swapped[1]);
            const old=await roomA.prepareEnvelope(life,choice(role,...lanes));
            count('sameLifeReplay');
            same('immutable-existing-life',role+'/'+pos,old,last);
            const moved=(await spawn(choice(role,...swapped))).loadout;
            const native=await getRole();
            same('swapped-native',role+'/'+pos,[native.PrimaryWeapon,native.SecondWeapon],swapped);
            for (const [slot,id] of swapped.entries()) if (id!=='None') {
                readWeapon(t,role+'/'+id+'/swapped',prepared.get(id),native.WeaponConfig.Weapons.find(w=>w.WeaponId===id),configs(moved)[slot]);
                cover('role-arm-slot',id+'/'+(slot+1));
            }
            // 显式卸下曾有选择的所有真实部位，以及整枪皮肤和枪饰。
            for (const id of group) {
                const old=prepared.get(id);
                const clear={WeaponId:id,Parts:old.Parts.map(p=>({SlotId:p.SlotId,PartId:''})),Skin:{SkinInfo:{Type:'',Id:''},WeaponOrnament:''}};
                same('native-weapon-clear',id,await account.weaponStatus(archiveRole,clear),0);
                count('nativeWeaponWrites');
                const empty=(await spawn(choice(role,...swapped))).loadout, raw=await getRole();
                const ix=swapped.indexOf(id);
                readWeapon(t,id+'/clear',clear,raw.WeaponConfig.Weapons.find(w=>w.WeaponId===id),configs(empty)[ix]);
                cover('role-weapon-all-parts-cleared',id);
                same('restore-after-clear',id,await account.weaponStatus(archiveRole,old),0);
                count('nativeWeaponWrites');
            }
        }
        finalByRole[role]=prepared;
        const getNow=await getRole();
        finalSettings[role]=[getNow.PrimaryWeapon,getNow.SecondWeapon];
        neighbors=await isolated(role,neighbors);
        report.roleSummary[role]={authorizedGuns:guns.length,checkedLives:life-roleStarted};
        console.log('Checked',role,'life',life,'checks',report.assertions,'failure groups',Object.keys(report.failures).length);
        save();
    }
    account.close(); account=await f.login(owner); count('tcpLogins');
    roomA=await room.admit(owner); life=0;
    const logged=await allNative(account), reloggedPeer=await allNative(control);
    for(const role of roles) {
        same('roundtrip-player-isolation',role,reloggedPeer[role],initialPeer[role]);
        const initial=initialPeer[role];
        const current=(await spawn(choice(role,initial.PrimaryWeapon,initial.SecondWeapon),true)).loadout;
        same('private-player-isolation',role,current,peerBirths[role]);
        const configurations=finalByRole[role];
        const ids=[...configurations.keys()];
        for(let i=0;i<ids.length;i+=2) {
            const pick=choice(role,ids[i],ids[i+1]||'None'), born=(await spawn(pick)).loadout;
            for (const [slot,id] of [pick.primary,pick.secondary].entries()) if (id!=='None') {
                readWeapon(t,role+'/'+id+'/relogin',configurations.get(id),logged[role].WeaponConfig.Weapons.find(w=>w.WeaponId===id),configs(born)[slot]);
                cover('relogged-gun',id);
            }
        }
        same('persisted-role-settings',role,[logged[role].PrimaryWeapon,logged[role].SecondWeapon],finalSettings[role]);
        const look=finalLooks[role];
        const skin=logged[role].SkinConfig;
        same('role-relogin-skin',role,[skin.SuitData.SkinItem,skin.SuitData.PaintItem,skin.SpecialSlot,skin.Ornament].map(normal),look);
        const sm=(await spawn(choice(role,...finalSettings[role]))).loadout;
        same('role-relogin-character-data',role,characterView(sm),look);
    }
    const af=path.join(loadoutDir,owner+'.json'),bf=path.join(loadoutDir,peer+'.json');
    assert.equal(path.dirname(loadoutDir),path.join(f.dir,'data'));
    const saved=JSON.parse(fs.readFileSync(af,'utf8')),friend=JSON.parse(fs.readFileSync(bf,'utf8'));
    same('persisted-player-identities','disk',[saved.playerId,friend.playerId],[owner,peer]);
    for(const role of roles) {
        const doc=Object.entries(saved.roles).find(([key])=>key.toUpperCase()===role)?.[1];
        for(const [id,last] of finalByRole[role]) {
            const wire=f.weaponUpdate.decode(Buffer.from(doc?._weaponArchives?.[id] || '','hex'));
            const key=role+'/'+id;
            same('saved-native-role-gun',key,[wire.RoleId.toUpperCase(),wire.WeaponArchive?.WeaponId],[role,id]);
            const parts=wire.WeaponArchive?.Parts || [];
            same('saved-native-slot-parts',key,parts.map(p=>[p.SlotId,normal(p.PartId)]),last.Parts.map(p=>[p.SlotId,normal(p.PartId)]));
            cover('persisted-protobuf-gun',id);
        }
    }
    account.close(); control.close(); await f.stop(); await f.start(appDir); count('serviceStarts');
    const cold=await f.login(owner),coldPeer=await f.login(peer); count('tcpLogins',2);
    const restartRoom=createRoomSpawnClient({origin:f.origin(),roomId:'coverage',credential:secret});
    roomA=await restartRoom.admit(owner); roomB=await restartRoom.admit(peer); life=peerLife=0;
    const afterCold=await allNative(cold), afterPeer=await allNative(coldPeer);
    for(const role of roles) {
        same('service-restart-first-player',role,hash(Buffer.from(JSON.stringify(afterCold[role]))),hash(Buffer.from(JSON.stringify(logged[role]))));
        same('service-restart-second-player',role,hash(Buffer.from(JSON.stringify(afterPeer[role]))),hash(Buffer.from(JSON.stringify(initialPeer[role]))));
        const controlRow=afterPeer[role];
        same('private-control-after-restart',role,(await spawn(choice(role,controlRow.PrimaryWeapon,controlRow.SecondWeapon),true)).loadout,peerBirths[role]);
        const ids=[...finalByRole[role].keys()];
        const look=finalLooks[role], coldSkin=afterCold[role].SkinConfig;
        same('role-cold-archive',role,[coldSkin.SuitData.SkinItem,coldSkin.SuitData.PaintItem,coldSkin.SpecialSlot,coldSkin.Ornament].map(normal),look);
        const snapshot=(await spawn(choice(role,...finalSettings[role]))).loadout;
        same('role-cold-room',role,characterView(snapshot),look);
        for(let i=0;i<ids.length;i+=2) {
            const pick=choice(role,ids[i],ids[i+1]||'None'),born=(await spawn(pick)).loadout;
            for(const [slot,id] of [pick.primary,pick.secondary].entries()) if(id!=='None') {
                readWeapon(t,role+'/'+id+'/service-restart',finalByRole[role].get(id),afterCold[role].WeaponConfig.Weapons.find(w=>w.WeaponId===id),configs(born)[slot]);
                cover('cold-restart-gun',id);
            }
        }
    }
    if(!onlyRole) {
        const expected=new Map();
        const want=(kind,value)=>{if(!expected.has(kind))expected.set(kind,new Set());expected.get(kind).add(value);};
        for(const [role,definition] of Object.entries(catalog.roles)) {
            for(const [suit,paints] of Object.entries(definition.suitPaints))
                for(const paint of paints)want('role-model-paint',role+'/'+suit+'/'+paint);
            for(const [key,field] of [['role-badge-or-clear','armBadgeScope'],['role-head-or-clear','headAccessoryScope']])
                for(const id of [...definition[field],'None'])want(key,role+'/'+id);
            for(const id of definition.weaponScope) {
                const base=catalog.grants[id],gun=catalog.weapons[base];
                for(const arm of [1,2])want('role-arm-slot',id+'/'+arm);
                for(const label of ['role-weapon','cold-restart-gun','persisted-protobuf-gun','relogged-gun','role-weapon-all-parts-cleared'])want(label,id);
                for(const charm of gun.ornaments)want('whole-weapon-charm',base+'/'+charm);
                for(const [suit,desc] of Object.entries(gun.suits))
                    for(const paint of Object.keys(desc.paints))want('whole-suit-paint',base+'/'+suit+'/'+paint);
                for(const [idx,slot] of gun.parts.entries()) {
                    for(const part of slot.ids) {
                        want('base-slot-selectable-part',base+'/'+(idx+1)+'/'+part);
                        want('role-gun-slot-selectable-part',id+'/'+(idx+1)+'/'+part);
                        for(const reticle of slot.reticles[part] || [])want('optic-reticle',part+'/'+reticle);
                    }
                    for(const look of slot.looks){
                        if(look.skin)want('part-skin',base+'/'+(idx+1)+'/'+look.skin);
                        if(look.paint)want('slot-paint',base+'/'+(idx+1)+'/'+look.paint);
                    }
                }
            }
            for(const op of [3,4,5,6])for(const id of [...definition[lookupSlots[op-1]],'None'])
                want('role-slot-equipment-or-clear',role+'/'+op+'/'+id);
        }
        for(const [kind,set] of expected) {
            const actual=counted.get(kind)||new Set();
            same('complete-retail-grant-set',kind,csvSort(actual),csvSort(set));
        }
    }
    report.complete=true; save();
}

async function main() {
    const secret=randomBytes(32).toString('hex'), previous=process.env.BRT_SPAWN_ROOMS;
    process.env.BRT_SPAWN_ROOMS=JSON.stringify({coverage:secret});
    try { await fixture(source,f=>exercise(f,secret),{roleSuccess:200,scratch:os.tmpdir()}); }
    finally {
        if (previous===undefined) delete process.env.BRT_SPAWN_ROOMS; else process.env.BRT_SPAWN_ROOMS=previous;
        save();
    }
    console.log('RESULT',report.complete,report.counts,report.coverage,'failure groups',Object.keys(report.failures));
    if (Object.keys(report.failures).length) process.exitCode=1;
}
main().catch(e=>{ console.error(e.message, e.stack?.split('\n').slice(1,5).join('\n')); process.exitCode=1; });
