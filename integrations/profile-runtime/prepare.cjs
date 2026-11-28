'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const indexFile = 'index.js';
const storeFile = 'game/loadoutStore.js';
const indexSha = '3961c933f8a3396fb01d9bb7e190db2312ce5a4c2320ea78348aed84df5ce10d';
const storeSha = 'c85d1cc7a0426b8c42a22ef4c6fe60562ce58e3fdb3c41debebbd987bfae866e';
const projectedSha = '76f0a64ddc6ce588b0df2b01778810d0b13050124599d94285dd5ec50a4a4620';
const releaseIndex = '14ae33e2731895279f39fdfaf6295cd59bc2e2a8deea780e166dd5665d01ad89';
const releaseStore = '69864e5d193f06fcc1054220f4d1a0bc1170bf040d9ed2026fe34b562148cde1';
const releaseProto = '130c0bff91571ee4e563474a96e590d04ca9fdd22c2c99087cdfb4101ab6ef4c';
const releaseCode = 'c55dd8e4995215590fb6557d98e5e7637c282f4daa2abaf6de5031ad4dce67b0';
const mainIndex = 'de98c06ce2ba56d4c35312d634883cce99df7b659ccca4bbef49b4f748c60f9a';
const mainProto = 'e58089a7143c528b09213d6567e2ea7848733954f80b2481b79b66e85fc952d7';
const archiveProtos = [
  'game/proto/Request/UpdateRoleArchiveV2Request.proto',
  'game/proto/Request/UpdateWeaponArchiveV2Request.proto',
  'game/proto/Response/GetPlayerArchiveV2Response.proto',
];
const support = [
  ['game/definitionIndex.js', '67ecfb27713497cc6277e3710cbe5cc5827c5001a5915c60d127f67c79fbc4bd'],
  ['game/proto', '325f85f9b74982cf8817cd5c4b2a4c8e6f3ee6aa5ef3f300d62f727e823b7538'],
  ['game/definitions', '4f346a82976f9009d4205eb658634b44b2780a7590d79afb3673e45ed58a0286'],
  ['node_modules', '728d610e966a8d86ea3ffbdd53f814168db0b76541c1c81344d46d20df531eff',
    '3d72ab3cebba5df379a9219c15390d5f2404778626983d352b30e213d9adff1b'],
];
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
class Unsupported extends Error {}

// 新服务必须原子启用。Windows 可能暂时占用刚写好的目录，保留一次性准备流程。
function publishApplication(stage, destination) {
  const pauses = [25, 50, 100, 200, 400];
  const sleeper = new Int32Array(new SharedArrayBuffer(4));
  for (let attempt = 0; ; attempt++) {
    try {
      fs.renameSync(stage, destination);
      return;
    } catch (error) {
      // 另一启动器已经完成同一版本。调用方随即核对整棵应用目录的内容摘要。
      if (fs.existsSync(destination)) return;
      if (process.platform !== 'win32' || !['EBUSY', 'EPERM', 'EACCES'].includes(error.code) || attempt >= pauses.length)
        throw error;
      Atomics.wait(sleeper, 0, 0, pauses[attempt]);
    }
  }
}

function digest(files) {
  const hash = crypto.createHash('sha256');
  for (const name of [...files.keys()].sort()) hash.update(name + '\0' + sha(files.get(name)) + '\n');
  return hash.digest('hex');
}

function directory(dir) {
  if (!fs.lstatSync(dir).isDirectory()) throw new Error('Expected a regular directory: ' + dir);
  return fs.realpathSync(dir);
}

function readTree(root, accept, prefix = '') {
  const files = new Map();
  directory(path.join(root, prefix));
  for (const entry of fs.readdirSync(path.join(root, prefix), { withFileTypes: true })) {
    const name = prefix + entry.name;
    if (accept && entry.name.startsWith('.')) continue;
    if (entry.isDirectory()) {
      for (const pair of readTree(root, accept, name + '/')) files.set(...pair);
    } else if (!entry.isFile()) {
      throw new Error('Unexpected linked or non-regular entry: ' + path.join(root, name));
    } else if (!accept || accept.test(name)) {
      files.set(name, fs.readFileSync(path.join(root, name)));
    }
  }
  return files;
}

function readFile(root, rel) {
  const file = path.join(root, rel);
  if (!fs.lstatSync(file).isFile()) throw new Error('Expected a regular file: ' + file);
  return fs.readFileSync(file);
}

function single(text, before, after) {
  const start = text.indexOf(before);
  if (start < 0 || text.indexOf(before, start + 1) >= 0) {
    throw new Error('Patch context differs: ' + before.trim().slice(0, 90));
  }
  return text.slice(0, start) + after + text.slice(start + before.length);
}

// 随包补丁逐块校验完整上下文与行数，部署时使用普通文件接口。
function hunks(bytes, targets = [indexFile, storeFile]) {
  const lines = bytes.toString('utf8').replaceAll('\r\n', '\n').split('\n');
  const result = [];
  let file;
  for (let i = 0; i < lines.length; i++) {
    if (lines[i].startsWith('+++ b/')) file = lines[i].slice(6);
    if (!lines[i].startsWith('@@ ')) continue;
    const header = /^@@ -\d+,(\d+) \+\d+,(\d+) @@/.exec(lines[i]);
    if (!header || !targets.includes(file)) throw new Error('Unsupported patch header');
    const before = [], after = [];
    while (before.length < Number(header[1]) || after.length < Number(header[2])) {
      const line = lines[++i];
      if (!line || ![' ', '-', '+'].includes(line[0])) throw new Error('Incomplete patch: ' + file);
      if (line[0] !== '+') before.push(line.slice(1));
      if (line[0] !== '-') after.push(line.slice(1));
    }
    if (before.length !== +header[1] || after.length !== +header[2]) throw new Error('Invalid patch counts');
    result.push({ file, before: before.join('\n') + '\n', after: after.join('\n') + '\n' });
  }
  if (!result.length) throw new Error('Empty profile patch');
  return result;
}

function patch(blob, changes, reverse = false) {
  let text = blob.toString('utf8');
  const crlf = text.includes('\r\n');
  text = text.replaceAll('\r\n', '\n');
  for (const h of reverse ? changes.slice().reverse() : changes) {
    text = reverse ? single(text, h.after, h.before) : single(text, h.before, h.after);
  }
  return Buffer.from(crlf ? text.replaceAll('\n', '\r\n') : text);
}

function original(source, name, raw, expected, patches) {
  let variants = [raw];
  // 识别同一基线上曾独立安装的已知补丁；任一还原结果都必须匹配原始 SHA。
  for (const group of [...patches].reverse()) {
    const changes = group.filter(h => h.file === name);
    if (!changes.length) continue;
    for (const value of variants.slice()) {
      try { variants.push(patch(value, changes, true)); } catch { /* 该副本未应用这一补丁 */ }
    }
  }
  // Git for Windows 可在应用补丁时转换整份文件的行尾，仍以重建原件的完整摘要判定。
  const normalized = variants.flatMap(value => {
    const lf = value.toString('utf8').replaceAll('\r\n', '\n');
    return [value, Buffer.from(lf), Buffer.from(lf.replaceAll('\n', '\r\n'))];
  });
  const base = normalized.find(value => sha(value) === expected);
  if (base) return base;
  if (name === storeFile && normalized.some(value => sha(value) === projectedSha)) {
    const backupName = storeFile + '.boundary-toolbox-v1.orig';
    if (fs.existsSync(path.join(source, backupName))) {
      const backup = readFile(source, backupName);
      if (sha(backup) === storeSha) return backup;
    }
    throw new Unsupported('Previously deployed loadout store requires original ' + backupName + ' SHA-256 ' + storeSha);
  }
  throw new Unsupported(name + ' SHA-256 ' + sha(raw) + ' (expected base ' + expected + ')');
}

function loadoutLocation(dir) {
  try {
    if (!fs.statSync(dir).isDirectory()) throw new Error('Profile storage is not a directory: ' + dir);
    return fs.realpathSync(dir);
  } catch (error) {
    if (error.code !== 'ENOENT') throw error;
    if (fs.lstatSync(dir, { throwIfNoEntry: false })) throw new Error('Broken profile storage link: ' + dir);
    return path.join(loadoutLocation(path.dirname(dir)), path.basename(dir));
  }
}

function contains(parent, child) {
  const rel = path.relative(parent, child);
  return !rel || (rel !== '..' && !rel.startsWith('..' + path.sep) && !path.isAbsolute(rel));
}

function write(root, name, bytes) {
  const file = path.join(root, name);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, bytes);
}

function lf(bytes) {
  return Buffer.from(bytes.toString('utf8').replaceAll('\r\n', '\n'));
}

function releaseSources(kit) {
  const raw = require('node:zlib').gunzipSync(readFile(kit, 'release-084-code.json.gz'), { maxOutputLength: 180000 });
  const files = new Map(Object.entries(JSON.parse(raw)).map(([name, body]) => [name, Buffer.from(body)]));
  if (digest(files) !== releaseCode) throw new Error('Embedded V0.8.4 source inventory differs');
  return files;
}

function bridgeRelease(files, kit) {
  const changes = hunks(readFile(kit, 'release-084.patch'), [indexFile, storeFile, ...archiveProtos]);
  for (const name of new Set(changes.map(h => h.file))) {
    files.set(name, patch(files.get(name), changes.filter(h => h.file === name)));
  }
  const protos = new Map([...files].filter(([name]) => name.startsWith('game/proto/'))
    .map(([name, bytes]) => [name.slice('game/proto/'.length), bytes]));
  if (sha(files.get(indexFile)) !== indexSha || sha(files.get(storeFile)) !== storeSha ||
      digest(protos) !== support[1][1]) throw new Error('V0.8.4 bridge did not reproduce the verified archive service');
}

// 三个已核验来源在此处均为同一份单人队伍模板。按完整上下文修改托管副本。
function bindSoloParty(entry) {
  const listener = 'const server = net.createServer((socket) => {';
  const bind = '        socket.playerId = gateSessions.get(RPCPath);';
  const create = '      else if(RPCPath === "/party.party/Create"){';
  entry = single(entry, 'let PartyPresence = "InMatching";', 'const initialPartyPresence = "InMatching";');
  entry = single(entry, listener, listener + '\n  let PartyPresence;');
  entry = single(entry, bind, bind + '\n        PartyPresence = initialPartyPresence;');
  entry = single(entry, create, [
    '      else if (!socket.playerId && ["/party.party/Create", "/party.party/SetPresence", "/party.party/QueryPresence"].includes(RPCPath)) {',
    '        socket.write(WrapMessageAndSerialize(MessageId, RPCPath, Buffer.alloc(0)));',
    '      }',
    create,
  ].join('\n'));
  entry = single(entry, 'PartyMembers: [TEMP_USER_ID]', 'PartyMembers: [socket.playerId]');
  return single(entry, 'UserId: TEMP_USER_ID,\n          Status: PartyPresence',
    'UserId: socket.playerId,\n          Status: PartyPresence');
}

function prepare(sourcePath, homePath, kit) {
  const source = fs.realpathSync(sourcePath);
  const root = directory(homePath);
  if (contains(source, root) || contains(root, source)) throw new Error('Profile application and source must have separate roots');
  const groups = ['playlist.patch', 'role-read-case.patch', 'native-role-write.patch', 'rest-role-key.patch',
    'session-progress.patch', 'ready-room.patch', 'region-catalog.patch', 'region-qos.patch',
    'native-equip-appearance.patch', 'local-ticket.patch', 'probe-secondary.patch',
    'retail-weapon-defaults.patch', 'retail-role-weapons.patch', 'retail-equipment-defaults.patch', 'room-spawn.patch',
    'legacy-client.patch']
    .map(name => hunks(readFile(kit, name)));
  const files = new Map();
  directory(path.join(source, 'game'));
  const sourceEntry = readFile(source, indexFile);
  const main = sha(lf(sourceEntry)) === mainIndex;
  const release = sha(sourceEntry) === releaseIndex;
  const sourceStore = fs.lstatSync(path.join(source, storeFile), { throwIfNoEntry: false }) ? readFile(source, storeFile) : null;
  let sourceKind;
  if (main) {
    if (sourceStore || fs.lstatSync(path.join(source, support[0][0]), { throwIfNoEntry: false })) {
      throw new Unsupported('BoundaryMetaServer main 007850d0 with unexpected archive modules');
    }
    sourceKind = 'metaserver-main-007850d0';
  } else if (release) {
    if (!sourceStore || sha(sourceStore) !== releaseStore) throw new Unsupported('V0.8.4 requires matching ' + storeFile);
    sourceKind = 'projectrebound-release-v0.8.4';
    files.set(indexFile, sourceEntry);
    files.set(storeFile, sourceStore);
  } else {
    files.set(indexFile, original(source, indexFile, sourceEntry, indexSha, groups));
    if (!sourceStore) throw new Unsupported('Missing ' + storeFile + ' for the selected archive service');
    files.set(storeFile, original(source, storeFile, sourceStore, storeSha, groups));
    sourceKind = 'local-archive-3961c933';
  }
  const loadoutDir = loadoutLocation(path.join(source, 'data', 'loadouts'));
  if (contains(root, loadoutDir) || contains(loadoutDir, root)) throw new Error('Profile storage overlaps managed runtime');
  for (const [name, ...allowed] of support) {
    if (name.endsWith('.js')) {
      if (main) continue;
      const bytes = readFile(source, name);
      if (!allowed.includes(sha(bytes))) throw new Unsupported(name + ' SHA-256 ' + sha(bytes));
      files.set(name, bytes);
    } else {
      const extensions = name === 'node_modules' ? /\.(?:js|cjs|mjs|json|node|wasm|proto)$/i :
        name.endsWith('proto') ? /\.proto$/ : /\.json$/;
      const selected = path.join(source, name);
      let entries = readTree(name === 'node_modules' ? fs.realpathSync(selected) : selected, extensions);
      if (main && name !== 'node_modules') entries = new Map([...entries].map(([rel, bytes]) => [rel, lf(bytes)]));
      const hash = digest(entries);
      const expected = name === 'game/proto' && (main || release) ? [main ? mainProto : releaseProto] : allowed;
      if (!expected.includes(hash)) throw new Unsupported(name + ' tree SHA-256 ' + hash);
      if (main && name === 'game/proto') continue;
      for (const [rel, bytes] of entries) files.set(name + '/' + rel, bytes);
    }
  }
  // main 只提供静态 RPC 回执，没有写盘层；其档案实现来自随包核对过的 V0.8.4 源码。
  if (main) for (const [name, bytes] of releaseSources(kit)) files.set(name, bytes);
  if (main || release) bridgeRelease(files, kit);
  const apps = path.join(root, 'apps');
  fs.mkdirSync(apps, { recursive: true });
  directory(apps);
  const stage = fs.mkdtempSync(path.join(apps, '.profile-'));
  try {
    for (const [name, bytes] of files) {
      if ([indexFile, storeFile, support[0][0]].includes(name) || name.startsWith('game/proto/')) write(stage, name, bytes);
    }
    const result = require(path.join(kit, 'deploy.cjs')).deploy('apply', stage);
    if (result.state !== 'installed' || result.sha256 !== projectedSha) throw new Error('Unexpected loadout deployment result');
    files.set(storeFile, readFile(stage, storeFile));
    fs.unlinkSync(path.join(stage, storeFile + '.boundary-toolbox-v1.orig'));
    for (const group of groups) {
      for (const file of new Set(group.map(h => h.file))) files.set(file, patch(files.get(file), group.filter(h => h.file === file)));
    }
    let entry = bindSoloParty(files.get(indexFile).toString('utf8'));
    const createSocket = 'const server = net.createServer((socket) => {';
    entry = single(entry, createSocket, createSocket + "\n  const squad = require('./party-lobby.cjs').attach(socket, protobuf);");
    const bindGate = '        socket.playerId = gateSessions.get(RPCPath);';
    entry = single(entry, bindGate, '        squad?.reset();\n' + bindGate);
    const partyRoute = '      else if(RPCPath === "/party.party/Create"){';
    entry = single(entry, partyRoute, [
      '      else if (squad && squad.handles(RPCPath)) {',
      '        await squad.dispatch(RPCPath, MessageBytes, PLAYLISTS_JSON,',
      '          bytes => socket.write(WrapMessageAndSerialize(MessageId, RPCPath, bytes)),',
      '          (name, body) => socket.write(body === null ? WrapMessageAndSerialize(-1, name, Buffer.alloc(0)) : WrapJSONMessageAndSerialize(-1, name, body)));',
      '      }', partyRoute].join('\n'));
    for (const [before, after] of [
      ['app.listen(process.env.PORT || 8000, () => {', 'app.listen(Number(process.env.PORT || 8000), MatchmakingHost, () => {'],
      ['server.listen(RPCPort, () => {', 'server.listen(RPCPort, MatchmakingHost, () => {'],
      ['matchmakingUDPServer.bind(MatchmakingPort);', 'matchmakingUDPServer.bind(MatchmakingPort, MatchmakingHost);'],
      ['matchmakingTCPServer.listen(MatchmakingPort);', 'matchmakingTCPServer.listen(MatchmakingPort, MatchmakingHost);'],
    ]) entry = single(entry, before, after);
    files.set(indexFile, Buffer.from(entry));
    files.set(storeFile, Buffer.from(single(files.get(storeFile).toString('utf8'),
      'const DATA_DIR = path.join(__dirname, "..", "data", "loadouts");',
      'const DATA_DIR = require("../profile-location.json").loadoutDir;')));
    files.set('profile-location.json', Buffer.from(JSON.stringify({ source, sourceKind, loadoutDir })));
    for (const name of ['native-menu.cjs', 'session-progress.cjs', 'ready-room.cjs', 'party-lobby.cjs', 'room-spawn.cjs', 'spawn-parts.cjs', 'receiver-main.json', 'role-paint-scope.cjs', 'role-paint-scopes.json', 'legacy-client.cjs']) {
      files.set(name, readFile(kit, name));
    }
    files.set('LICENSE.txt', readFile(kit, 'release-084-LICENSE.txt'));
    files.set('package.json', Buffer.from('{"name":"boundary-local-profile","private":true,"type":"commonjs"}\n'));
    const expected = digest(files);
    const appDir = path.join(apps, expected);
    if (!fs.existsSync(appDir)) {
      for (const [name, bytes] of files) write(stage, name, bytes);
      publishApplication(stage, appDir);
    }
    if (digest(readTree(appDir)) !== expected) throw new Error('Managed application differs from verified sources: ' + appDir);
    return { kind: 'managed', appDir, loadoutDir,
      sourceEntrySha256: sha(sourceEntry), sourceStoreSha256: sourceStore && sha(sourceStore) };
  } finally {
    if (fs.existsSync(stage) && path.dirname(directory(stage)) === fs.realpathSync(apps)) {
      fs.rmSync(stage, { recursive: true, maxRetries: 3 });
    }
  }
}

module.exports = { prepare, Unsupported };
if (require.main === module) {
  try {
    console.log(JSON.stringify(prepare(process.argv[2], process.argv[3], __dirname)));
  } catch (error) {
    if (error instanceof Unsupported) console.log(JSON.stringify({ kind: 'unsupported', reason: error.message }));
    else { process.stderr.write(error.message + '\n'); process.exitCode = 1; }
  }
}
