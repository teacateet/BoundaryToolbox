'use strict';
const {normal} = require('./weapon-cases.cjs');

function characterView(payload) {
    const raw=payload?.CharacterData;
    if (!raw) return null;
    // 与官方默认角色资产的具名字段比较。不能用档案 proto 的槽名称推算 UE 外观类枚举。
    return ['SpacesuitID','SpacesuitSkinPaintingID','ArmBadgeID','HeadAccessoryID']
        .map(key=>Object.hasOwn(raw,key)?raw[key]:null);
}

async function checkRoleAppearance(run) {
    const {role,archiveRole,allowed,client,roomSpawn,selection,observe,count,cover,same}=run;
    const suits=Object.entries(allowed.suitPaints).flatMap(([suit,paints])=>paints.map(paint=>[suit,paint]));
    const badges=[...allowed.armBadgeScope,''];
    const ornaments=[...allowed.headAccessoryScope,''];
    const last={};
    for (let step=0;step<Math.max(suits.length,badges.length,ornaments.length);step++) {
        const theme=suits[step%suits.length], badge=badges[step%badges.length], ornament=ornaments[step%ornaments.length];
        if (step<suits.length) {
            same('role-model-paint-write',role+'/'+step,await client.roleStatus(archiveRole,7,{SkinItem:theme[0],PaintItem:theme[1]}),200);
            cover('role-model-paint',role+'/'+theme.join('/')); count('nativeRoleAppearanceWrites');
            last.theme=theme;
        }
        if (step<badges.length) {
            same('role-badge-write',role+'/'+step,await client.roleStatus(archiveRole,9,badge),200);
            cover('role-badge-or-clear',role+'/'+normal(badge)); count('nativeRoleAppearanceWrites');
            last.badge=badge;
        }
        if (step<ornaments.length) {
            same('role-head-write',role+'/'+step,await client.roleStatus(archiveRole,10,ornament),200);
            cover('role-head-or-clear',role+'/'+normal(ornament)); count('nativeRoleAppearanceWrites');
            last.ornament=ornament;
        }
        await inspect(step);
    }
    const before=await observe();
    for (const [suit,ids] of Object.entries(allowed.unlistedIndexedPaints)) {
        for (const paint of ids) {
            same('reject-unlisted-but-well-typed-role-paint',role+'/'+suit+'/'+paint,
                await client.roleStatus(archiveRole,7,{SkinItem:suit,PaintItem:paint}),1);
            count('deniedIndexedRolePaints');
        }
    }
    same('rejected-role-paints-preserve-archive',role,(await observe()).SkinConfig,before.SkinConfig);
    if (role==='SPIKE') {
        const literal='Spike_DRAGONBLUE_PTDragonSpike';
        same('retail-spelling-accepted',role,await client.roleStatus(archiveRole,7,{SkinItem:'SPIKE_DRAGONBLUE',PaintItem:literal}),200);
        const readback=(await observe()).SkinConfig.SuitData;
        same('retail-spelling-normalized',role,readback.PaintItem,'SPIKE_DRAGONBLUE_PTDragonSpike');
        count('nativeRoleAppearanceWrites');
    }
    // 明确取下涂装、徽章和头饰，再对每个角色保留一个可辨认的实际选择供重登验证。
    for (const [index,values] of [[0,[suits[0][0],'','','']],[1,[
        suits[Math.min(1,suits.length-1)][0],suits[Math.min(1,suits.length-1)][1],
        allowed.armBadgeScope.find(x=>x!=='ABGOriginal') || '',allowed.headAccessoryScope.find(x=>x!=='HONONE') || '']]]) {
        const [model,paint,badge,head]=values;
        same('role-skin-restore',role,await client.roleStatus(archiveRole,7,{SkinItem:model,PaintItem:paint}),200);
        same('role-badge-restore',role,await client.roleStatus(archiveRole,9,badge),200);
        same('role-head-restore',role,await client.roleStatus(archiveRole,10,head),200);
        count('nativeRoleAppearanceWrites',3);
        last.theme=[model,paint]; last.badge=badge; last.ornament=head;
        await inspect('end'+index);
    }
    return [last.theme[0],last.theme[1],last.badge,last.ornament].map(normal);

    async function inspect(step) {
        const expected=[last.theme[0],last.theme[1],last.badge,last.ornament].map(normal);
        const row=await observe(),skin=row.SkinConfig, label=role+'/'+step;
        same('role-native-skin',label,[skin.SuitData.SkinItem,skin.SuitData.PaintItem,skin.SpecialSlot,skin.Ornament].map(normal),expected);
        const result=await roomSpawn(selection),snapshot=characterView(result.loadout);
        same('role-spawn-character-data',label,snapshot,expected);
    }
}
module.exports={checkRoleAppearance,characterView};
