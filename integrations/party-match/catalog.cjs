'use strict';
const {randomInt} = require('node:crypto');

const retailModes = new Set(['DMTN','Rush','Purge','CapFlag','Conquest','WarehouseRush',
  'NavalBoarding','GoldenGun','Hardcore','Dusty','Rush_PVE_Easy','Rush_PVE_Normal','Rush_PVE_Hard']);
const slug = s => typeof s === 'string' && /^[A-Za-z][A-Za-z0-9_]{0,39}$/.test(s);
const key = (mode,map) => mode+'/'+map;

// 原表给出候选组合；只有房主的实际开房实现声明可用时才展示。
function catalog(table, capabilities) {
  const rows = table.find(t => t.Name === 'DT_GameModeDefinition')?.Rows;
  if (!rows || !(capabilities instanceof Set)) throw Error('Missing room capabilities');
  const pairs = [];
  for (const [mode,row] of Object.entries(rows)) {
    if (!retailModes.has(mode)) continue;
    for (const item of row.MapsForMatch || []) {
      const map = item.RowName === 'warehouse' ? 'Warehouse' : item.RowName;
      if (slug(map) && capabilities.has(key(mode,map)) && !pairs.some(p=>p.mode===mode && p.map===map))
        pairs.push(Object.freeze({mode,map}));
    }
  }
  return Object.freeze(pairs);
}
function select(pairs, mode, map) {
  if ((mode !== null && !slug(mode)) || (map !== null && !slug(map))) throw Error('Invalid choice');
  const subset = pairs.filter(p => (mode === null || p.mode === mode) && (map === null || p.map === map));
  if (!subset.length) throw Error('Unsupported choice');
  return subset[randomInt(subset.length)];
}
const cardPrefix = 'BRT_PARTY_';
function selectorCards(pairs) {
  if (!pairs.length) return [];
  const rows = [{id:cardPrefix+'Any',mode:null,map:null,label:'随机地图 · 随机模式'}];
  for (const mode of new Set(pairs.map(p => p.mode)))
    rows.push({id:cardPrefix+'Mode_'+mode,mode,map:null,label:mode+' · 随机地图'});
  for (const map of new Set(pairs.map(p => p.map)))
    rows.push({id:cardPrefix+'Map_'+map,mode:null,map,label:map+' · 随机模式'});
  for (const {mode,map} of pairs)
    rows.push({id:cardPrefix+'Pair_'+mode+'_'+map,mode,map,label:map+' · '+mode});
  return rows;
}
module.exports = {catalog,select,selectorCards,slug,key};
