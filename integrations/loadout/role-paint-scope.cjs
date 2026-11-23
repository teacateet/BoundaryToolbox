'use strict';

const scopes=require('./role-paint-scopes.json');
// UE 的 FName 不区分大小写。保留原版 ItemType 表中的规范拼写供档案与出生包使用。
const values = new Map(Object.entries(scopes).map(([suit, paints]) =>
    [suit.toUpperCase(), new Map(paints.map(value => [value.toUpperCase(),value]))]));
function canonicalCharacterPaint(suit, paint) {
    if (typeof suit!=='string' || typeof paint!=='string') return null;
    const grants=values.get(suit.toUpperCase());
    if (!grants) return null;
    if (!paint || paint.toUpperCase()==='NONE') return '';
    return grants.get(paint.toUpperCase()) ?? null;
}
module.exports={canonicalCharacterPaint};
