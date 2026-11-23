'use strict';

const uniq = values => [...new Set(values)];
const normal = value => value && value !== 'None' ? value : 'None';

function weaponPlan(id, base, gun) {
    const suits = Object.entries(gun.suits);
    const finishes = suits.flatMap(([suit, details])=>Object.keys(details.paints).map(paint=>({suit,paint})));
    if (!finishes.length) throw Error('Missing retail weapon suit '+base);
    const parts = gun.parts.map((slot,i)=>{
        const resolved = slot.ids.flatMap(part=>{
            const reticles = slot.reticles[part] || [];
            return (reticles.length ? reticles : ['']).map(reticle=>({part,reticle}));
        });
        const looks=slot.looks;
        const alternate=slot.ids.map((part,k)=> ({part,reticle:(slot.reticles[part] || [])[0] || ''}));
        const variations=resolved.map(item=>({...item,look:looks.find(look=>!look.part || look.part===item.part)}));
        for(const [j,look] of looks.entries()) {
            const selected=look.part ? alternate.find(item=>item.part===look.part) : alternate[j%alternate.length];
            if(selected) variations.push({...selected,look});
        }
        return variations;
    });
    const rounds = Math.max(finishes.length,gun.ornaments.length,
        ...parts.map(slot=>slot.length));
    return { id,base,gun,rounds,
        get(i) {
            const {suit,paint} = finishes[i%finishes.length];
            const charm = gun.ornaments.length ? gun.ornaments[i%gun.ornaments.length] : '';
            const Parts = parts.map((slot,index)=>{
                const SlotId = index+1;
                if (!slot.length) return {SlotId,PartId:''};
                const {part,reticle,look} = slot[i%slot.length];
                const skin=look?.skin||'',paint=look?.paint||'';
                return {SlotId,PartId:part, Ornament:{Info:{Type:skin,Id:paint},SpecialSlot:reticle}};
            });
            return {WeaponId:id,Parts,Skin:{SkinInfo:{Type:suit,Id:paint},WeaponOrnament:charm}};
        }
    };
}

function nativeLook(row) {
    return { SkinItem:normal(row?.SuitData?.SkinItem), PaintItem:normal(row?.SuitData?.PaintItem),
        SpecialSlot:normal(row?.SpecialSlot),Ornament:normal(row?.Ornament) };
}
function requestLook(row) {
    return {SkinItem:normal(row?.Info?.Type ?? row?.SkinInfo?.Type), PaintItem:normal(row?.Info?.Id ?? row?.SkinInfo?.Id),
        SpecialSlot:normal(row?.SpecialSlot),Ornament:normal(row?.Ornament ?? row?.WeaponOrnament)};
}
function spawnLook(part) {
    return {SkinItem:part.WeaponPartSkinID,PaintItem:part.WeaponPartSkinPaintingID,
        SpecialSlot:part.WeaponPartSpecialSkinID,Ornament:part.ArchiveOrnament};
}
function readWeapon(t, lane, request, native, actual) {
    const expectedLook = requestLook(request.Skin);
    t.same('weapon-native-identity',lane,native?.WeaponId,request.WeaponId);
    t.same('weapon-native-look',lane,nativeLook(native?.Skin),expectedLook);
    t.same('weapon-spawn-identity',lane,actual?.WeaponID,request.WeaponId);
    t.same('weapon-spawn-look',lane,actual?.ArchiveAppearance,expectedLook);
    t.same('weapon-charm',lane,actual?.OrnamentID,expectedLook.Ornament);
    t.same('part-slot-order',lane,actual?.WeaponPartSlotTypeArray,[1,2,3,4,5,6,7,8,9,10]);
    const slots = new Map((actual?.WeaponPartSlotTypeArray || []).map((key,i)=>[key,actual.WeaponPartConfigs[i]]));
    const before = new Map((native?.Parts || []).map(part=>[part.SlotId,part]));
    for (const part of request.Parts) {
        const position = lane+'/'+part.SlotId;
        const source = before.get(part.SlotId), published = slots.get(part.SlotId);
        t.same('part-native-item',position,normal(source?.PartId),normal(part.PartId));
        t.same('part-spawn-item',position,published?.WeaponPartID,normal(part.PartId));
        t.same('part-native-look',position,nativeLook(source?.Skin),requestLook(part.Ornament));
        t.same('part-spawn-look',position,published && spawnLook(published),requestLook(part.Ornament));
    }
}
module.exports = {weaponPlan,readWeapon,normal};
