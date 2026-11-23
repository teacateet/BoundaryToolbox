'use strict';
const {execFile} = require('node:child_process');
const {promisify} = require('node:util');
const run = promisify(execFile);
// 社区 LoadoutFix::GetPlayerId 发送的示例值，不能当作玩家的身份。
const fixedRequestId = '76561198950613585';
const loop = ip => ip === '127.0.0.1' || ip === '::ffff:127.0.0.1';
const port = p => Number.isInteger(p) && p > 0 && p <= 65535;
const pair = s => !s?.destroyed && loop(s?.localAddress) && loop(s.remoteAddress) &&
  port(s.localPort) && port(s.remotePort) ? [s.remotePort,s.localPort] : null;
const same = (a,b) => a && b && a[0] === b[0] && a[1] === b[1];

async function verifyWithWindows(http, candidates) {
  const helper = process.env.BRT_ARCHIVE_OWNER_HELPER, game = process.env.BRT_ARCHIVE_OWNER_GAME;
  if (!helper || !game) return null;
  const {stdout} = await run(helper, ['--brt-archive-owner',game,...http.map(String),
    ...candidates.flat().map(String)], {windowsHide:true,timeout:2000,maxBuffer:1024,encoding:'utf8'});
  return JSON.parse(stdout);
}

function createClientReader(verify = verifyWithWindows) {
  const bound = new Map();
  let pending = 0, bindings = 0;
  return {
    watch(s) { s.once('close', () => { if (bound.delete(s)) bindings++; }); },
    bind(s) {
      if (bound.delete(s)) bindings++;
      if (s.joinAuthenticated === true && /^76561\d{12}$/.test(s.playerId) && pair(s)) {
        bound.set(s,{s, id:s.playerId, pair:pair(s)});
        bindings++;
      }
    },
    matches(req) {
      return req.method === 'GET' && req.params.playerId === fixedRequestId &&
        req.get('User-Agent') === 'BoundaryDLL/1.0' && !!pair(req.socket);
    },
    async owner(req) {
      const http = pair(req.socket);
      if (!http || !this.matches(req) || pending >= 8 || !bound.size || bound.size > 8) return null;
      const rows = [...bound.values()], initialBindings = bindings;
      pending++;
      try {
        const proof = await verify(http,rows.map(row => row.pair));
        if (initialBindings !== bindings || !same(http,pair(req.socket)) ||
            !Array.isArray(proof?.matches) || !proof.matches.length) return null;
        const ids = new Set();
        for (const i of proof.matches) {
          if (!Number.isInteger(i) || i < 0 || i >= rows.length) return null;
          const row = rows[i];
          if (bound.get(row.s) !== row || row.s.playerId !== row.id || !same(row.pair,pair(row.s))) return null;
          ids.add(row.id);
        }
        // 同一进程的两条会话若不一致，不按“最后一次登录”猜，也不读取示例账号。
        return ids.size === 1 && ids.has(proof.steam) ? proof.steam : null;
      } catch { return null; }
      finally { pending--; }
    },
  };
}

// 社区读方对六个槽按字符串取值；正常 REST 编辑器仍保留自己的嵌套对象形状。
function forCommunity(store, owner) {
  const file = store.load(owner);
  if (file === null) {
    // 普通首次登录可以看出厂装备；损坏或不可读的现有存档不能当作新玩家。
    try { require('node:fs').statSync(store._playerPath(owner)); return null; }
    catch (error) { if (error.code !== 'ENOENT') return null; }
  } else if (!file || file.playerId !== owner || !file.roles || typeof file.roles !== 'object' || Array.isArray(file.roles)) {
    return null;
  }
  const source = file?.roles || {};
  const wanted = new Set(['PEACE','PROBE','SNIPER','FORT','SPIKE','FIXER']);
  for (const key of Object.keys(source)) {
    const {role,canonicalRoleId} = store.getRoleDefinition(key);
    if (role) wanted.add(canonicalRoleId);
  }
  const roles = {};
  for (const id of wanted) {
    if (!store.getRoleDefinition(id).role) continue;
    const key = store.getSavedRoleKey(source,id);
    // 两个别名都与当前干员匹配时，不任意将一个角色档写到另一个名下。
    if (key === null && Object.keys(source).some(k => k.toLowerCase() === id.toLowerCase())) continue;
    const roleData = (key === null ? {} : source[key]) || {};
    const n = store.getNormalizedRoleData(id,roleData);
    const snapshot = store.getRoleLoadoutSnapshot(owner,id);
    roles[id] = {...snapshot,primaryWeapon:n.primaryWeapon,secondaryWeapon:n.secondaryWeapon,
      leftPylon:n.leftPylon,rightPylon:n.rightPylon,meleeWeapon:n.meleeWeapon,mobilityModule:n.mobilityModule,
      WeaponConfig:store.getNativeRoleWeaponConfig(id,roleData)};
  }
  return {playerId:owner,...(file?.updatedAt ? {updatedAt:file.updatedAt} : {}),roles};
}
module.exports = {createClientReader,forCommunity};
