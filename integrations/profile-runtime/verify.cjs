'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const http = require('node:http');
const net = require('node:net');
const udp = require('node:dgram');
const { once } = require('node:events');
const { createInterface } = require('node:readline');
const { spawn, spawnSync, execFile, execFileSync } = require('node:child_process');
const { promisify } = require('node:util');
const { setTimeout: delay } = require('node:timers/promises');
const { auditedLease, exerciseMenu } = require('./verify-menu.cjs');

const base = __dirname;
const build = path.join(base, '.build', 'official-baselines');
let probe, rustTest;
const snapshot = process.env.PROFILE_TEST_SOURCE ||
  'C:/BrtWork/SDK/sdk-fixture/validation-server';
const dependencies = process.env.PROFILE_TEST_MODULES ||
  'C:/BrtWork/SDK/sdk-fixture/release-v0.8.0/BoundaryMetaServer-main/node_modules';
const node = process.env.PROFILE_TEST_NODE || process.execPath;
const player = 'profile_runtime_fixture';
const role = 'SNIPER';
const gun = 'PEACE_GSW-AR';
const mag = 'GSW-AR_MAG-AR-EXT';
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const releaseZip = process.env.PROFILE_TEST_RELEASE || path.resolve(base, '../../.workflow/qa/rebound-084/Release.zip');
const mainSource = process.env.PROFILE_TEST_MAIN || 'C:/BrtWork/SDK/BoundaryMetaServer-main';

function copyApplication(source, modules, dest) {
  fs.mkdirSync(dest, { recursive: true });
  fs.copyFileSync(path.join(source, 'index.js'), path.join(dest, 'index.js'));
  for (const file of ['game/loadoutStore.js', 'game/definitionIndex.js', 'game/definitions', 'game/proto']) {
    fs.cpSync(path.join(source, file), path.join(dest, file), { recursive: true });
  }
  fs.cpSync(fs.realpathSync(modules), path.join(dest, 'node_modules'), { recursive: true });
}

function signature(dir) {
  const entries = [];
  function walk(sub) {
    for (const entry of fs.readdirSync(path.join(dir, sub), { withFileTypes: true })) {
      const name = path.join(sub, entry.name);
      if (name === 'data') continue;
      if (entry.isDirectory()) walk(name);
      else entries.push(name + ':' + hash(fs.readFileSync(path.join(dir, name))));
    }
  }
  walk('');
  return hash(entries.sort().join('\n'));
}

function owner(home, source, exe, port) {
  const result = spawnSync(probe, [home, source, exe, String(port)], {
    encoding: 'utf8', windowsHide: true, timeout: 15000,
  });
  assert.equal(result.status, 0, result.stderr);
  return JSON.parse(result.stdout).pid;
}

async function unrelatedListener(home, source) {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const port = server.address().port;
  try { assert.equal(owner(home, source, process.execPath, port), null); }
  finally { await new Promise(done => server.close(done)); }
  assert.equal(owner(home, source, process.execPath, port), null);
}

async function allocatePorts() {
  const held = [];
  let datagram;
  try {
    for (let i = 0; i < 3; i++) {
      const socket = net.createServer();
      held.push(socket);
      socket.listen(0, '127.0.0.1');
      await once(socket, 'listening');
    }
    const ports = held.map(s => s.address().port);
    datagram = udp.createSocket('udp4');
    datagram.bind(ports[2], '127.0.0.1');
    await once(datagram, 'listening');
    return ports;
  } finally {
    if (datagram) datagram.close();
    await Promise.all(held.filter(s => s.listening).map(s => new Promise(done => s.close(done))));
  }
}

async function serve(home, source, env, run) {
  const ports = await allocatePorts();
  const child = spawn(probe, [home, source, node, ...ports.map(String)], {
    env, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'],
  });
  let stderr = '';
  child.stderr.on('data', data => { stderr += data; });
  const closed = once(child, 'close');
  const lines = createInterface({ input: child.stdout });
  try {
    const next = once(lines, 'line', { signal: AbortSignal.timeout(30000) });
    const meta = await Promise.race([next.then(([line]) => JSON.parse(line)), closed.then(() => {
      throw new Error('Rust probe exited: ' + stderr);
    })]);
    assert.equal(meta.kind, 'managed', JSON.stringify(meta));
    const request = (method, route, body) => new Promise((resolve, reject) => {
      const req = http.request({ hostname: '127.0.0.1', port: ports[0], path: route, method,
        headers: { 'content-type': 'application/json' }, timeout: 2500 }, res => {
        let raw = '';
        res.on('data', part => { raw += part; });
        res.on('end', () => {
          try { resolve({ status: res.statusCode, body: JSON.parse(raw) }); }
          catch (error) { reject(error); }
        });
      });
      req.on('timeout', () => req.destroy(new Error('Local HTTP timed out')));
      req.on('error', reject);
      req.end(body === undefined ? undefined : JSON.stringify(body));
    });
    let ready = false;
    for (let i = 0; i < 70 && !ready; i++) {
      try { ready = (await request('GET', '/api/health')).body.status === 'ok'; }
      catch { await delay(60); }
    }
    assert.ok(ready, fs.readFileSync(path.join(home, 'fixture.log'), 'utf8').slice(-1500));
    await run({ meta, ports, request });
  } finally {
    lines.close();
    child.stdin.end();
    await closed;
  }
}

async function nativeClient(appDir, port, request, account = player) {
  const protobuf = require(path.join(appDir, 'node_modules/protobufjs'));
  const type = (file, name) => protobuf.loadSync(path.join(appDir, 'game/proto', file + '.proto'))
    .lookupType('ProjectBoundary.' + name);
  const req = type('Request/RequestWrapper', 'RequestWrapper');
  const res = type('Response/ResponseWrapper', 'ResponseWrapper');
  const auth = await request('POST', '/connectServer', { playerId: account, platform: 'steam', version: '124840' });
  assert.equal(auth.status, 200);
  assert.equal(auth.body.userId, account);
  assert.equal(auth.body.endpoint, '127.0.0.1:' + port);
  const socket = net.createConnection(port, '127.0.0.1');
  socket.setTimeout(5000, () => socket.destroy(new Error('Local RPC timed out')));
  await once(socket, 'connect');
  const chunks = socket[Symbol.asyncIterator]();
  let pending = Buffer.alloc(0), id = 0;
  async function exchange(route, bytes, wrapper) {
    const data = req.encode({ MessageId: ++id, RPCPath: route, Message: bytes }).finish();
    const header = Buffer.alloc(4);
    header.writeUInt32BE(data.length);
    socket.write(Buffer.concat([header, data]));
    while (pending.length < 4 || pending.length < 4 + pending.readUInt32BE()) {
      const chunk = await chunks.next();
      if (chunk.done) throw new Error('Local RPC closed');
      pending = Buffer.concat([pending, chunk.value]);
    }
    const end = 4 + pending.readUInt32BE();
    const reply = wrapper.decode(pending.subarray(4, end));
    pending = pending.subarray(end);
    assert.equal(reply.MessageId, id);
    assert.equal(reply.RPCPath, route);
    if (wrapper === res) assert.equal(reply.ErrorCode, 0);
    return reply.Message;
  }
  try { await exchange(auth.body.gateToken, Buffer.alloc(0), req); }
  catch (error) { socket.destroy(); throw error; }
  const archiveType = protobuf.loadSync(path.join(base, '../loadout/native-read.proto')).lookupType('fixture.Archive');
  return {
    close: () => socket.destroy(),
    type, wire: (route, data) => exchange(route, data, res),
    async archive(...roles) {
      const input = type('Request/GetPlayerArchiveV2Request', 'GetPlayerArchiveV2Request');
      const data = await exchange('/assets.Assets/GetPlayerArchiveV2', input.encode({ RoleIDs: roles }).finish(), res);
      return archiveType.decode(data).PlayerRoleDatas;
    },
    async equip(roleId, operation, itemId, expectedStatus = 200) {
      const input = type('Request/UpdateRoleArchiveV2Request', 'UpdateRoleArchiveV2Request');
      const data = await exchange('/assets.Assets/UpdateRoleArchiveV2',
        input.encode({ RoleId: roleId, Operation: operation, ItemId: itemId }).finish(), res);
      assert.equal(type('Response/UpdateRoleArchiveV2', 'UpdateRoleArchiveV2Response').decode(data).StatusCode, expectedStatus);
    },
    async parts(roleId, weapon) {
      const input = type('Request/UpdateWeaponArchiveV2Request', 'UpdateWeaponArchiveV2Request');
      const data = await exchange('/assets.Assets/UpdateWeaponArchiveV2',
        input.encode({ RoleId: roleId, WeaponArchive: weapon }).finish(), res);
      assert.equal(type('Response/UpdateRoleArchiveV2', 'UpdateRoleArchiveV2Response').decode(data).StatusCode, 0);
    },
    async query(name, output) {
      const data = await exchange('/matchmaking.Matchmaking/' + name, Buffer.alloc(0), res);
      return type('Response/' + output, output).decode(data);
    },
  };
}

function menuRun(home, source, service, before) {
  const lease = auditedLease(rustTest, home, source, node, service.ports[0]);
  assert.ok(lease, 'the verified running application has one lease path');
  return exerciseMenu({ home, service, lease, connect: nativeClient, before });
}

const armoryAccount = 'native_armory_fixture';
const armoryGuns = ['PEACE_GSW-AR', 'PEACE_RU-AKM'];
const armoryMags = ['GSW-AR_MAG-AR-EXT', 'RU-AKM_MAG-AK-EXT'];

function readArmory(row) {
  const magazines = armoryGuns.map(id => row.WeaponConfig.Weapons.find(w => w.WeaponId === id)
    ?.Parts.find(p => p.SlotId === 9)?.PartId);
  return [row.PrimaryWeapon, row.SecondWeapon, row.LeftPylon, row.RightPylon, ...magazines];
}

async function writeNativeArmory(appDir, port, request) {
  const client = await nativeClient(appDir, port, request, armoryAccount);
  try {
    const [before] = await client.archive('Peace');
    const { body: role } = await request('GET', '/api/definitions/roles/PEACE');
    const left = role.podScope.find(id => id && id !== 'None' && id !== before.LeftPylon);
    const right = role.podScope.find(id => id && id !== 'None' && id !== left && id !== before.RightPylon);
    assert.ok(left && right);
    for (const [key, slot, value] of [['Peace', 1, armoryGuns[0]], ['PEACE', 2, armoryGuns[1]],
      ['Peace', 5, left], ['PEACE', 6, right]]) {
      await client.equip(key, slot, value);
    }
    for (const [i, weaponId] of armoryGuns.entries()) {
      const weapon = before.WeaponConfig.Weapons.find(entry => entry.WeaponId === weaponId);
      const old = weapon.Parts.find(p => p.SlotId === 9);
      assert.ok(old && old.PartId !== armoryMags[i]);
      const parts = weapon.Parts.map(p => ({ SlotId: p.SlotId, PartId: p.SlotId === 9 ? armoryMags[i] : p.PartId }));
      await client.parts(i === 0 ? 'PEACE' : 'Peace', { WeaponId: weaponId, Parts: parts });
    }
    return [...armoryGuns, left, right, ...armoryMags];
  } finally { client.close(); }
}

async function checkBindings(pid, ports) {
  // 按本次创建的进程 PID 读取 Windows 监听表，同时检查两种传输协议。
  const listing = execFileSync('netstat', ['-ano', '-p', 'TCP'], { encoding: 'utf8', windowsHide: true });
  const udpListing = execFileSync('netstat', ['-ano', '-p', 'UDP'], { encoding: 'utf8', windowsHide: true });
  const rows = (s, protocol) => s.split(/\r?\n/).map(l => l.trim().split(/\s+/))
    .filter(row => row[0] === protocol && row.at(-1) === String(pid));
  const listeners = rows(listing, 'TCP').filter(row => row[2] === '0.0.0.0:0' || row[2] === '[::]:0');
  assert.deepEqual(listeners.map(row => row[1]).sort(), ports.map(p => '127.0.0.1:' + p).sort());
  assert.deepEqual(rows(udpListing, 'UDP').map(row => row[1]), ['127.0.0.1:' + ports[2]]);
}

async function verify(area) {
  const source = path.join(area, 'Steam fixture', 'ProjectBoundary', 'Binaries', 'Win64', 'BoundaryMetaServer-main');
  const home = path.join(area, 'Orbital fixture');
  fs.mkdirSync(home);
  copyApplication(snapshot, dependencies, source);
  const srcBefore = signature(source);
  const store = path.join(area, 'Existing player storage');
  fs.mkdirSync(store);
  fs.mkdirSync(path.join(source, 'data'));
  fs.symlinkSync(store, path.join(source, 'data/loadouts'), 'junction');
  const disk = path.join(store, player + '.json');
  const prior = { playerId: player, roles: { [role]: { primaryWeapon: 'None', _skinToken: 'previous-role' } } };
  fs.writeFileSync(disk, JSON.stringify(prior));
  const untouched = path.join(store, 'other_fixture.json');
  fs.writeFileSync(untouched, '{"roles":{}}');
  const diskBefore = hash(fs.readFileSync(disk));
  const marker = path.join(area, 'unexpected.txt');
  const env = { ...process.env, NODE_OPTIONS: '--require=' + JSON.stringify(marker), NODE_PATH: area,
    HTTP_PROXY: 'http://127.0.0.1:1', HTTPS_PROXY: 'http://127.0.0.1:1', NODE_USE_ENV_PROXY: '1' };
  const inspect = () => spawnSync(probe, [home, source, node], { env, encoding: 'utf8', windowsHide: true, timeout: 30000 });
  const managed = () => {
    const result = inspect();
    assert.equal(result.status, 0, result.stderr);
    return JSON.parse(result.stdout);
  };
  const parallel = await Promise.all([0, 1].map(async () => {
    const output = await promisify(execFile)(probe, [home, source, node], { env, windowsHide: true, timeout: 30000 });
    return JSON.parse(output.stdout);
  }));
  assert.equal(parallel[0].kind, 'managed');
  assert.deepEqual(parallel[0], parallel[1]);
  assert.deepEqual(fs.readdirSync(path.join(home, 'profile-runtime/apps')), [path.basename(parallel[0].appDir)]);
  console.log('PASS concurrent cold preparation published one verified application');
  let appDir, savedPod, firstPort, firstPid, expectedArmory, previousMenu;
  const armoryDisk = path.join(store, armoryAccount + '.json');
  const requestPath = '/api/loadout/' + player;
  await serve(home, source, env, async ({ meta, ports, request }) => {
    appDir = meta.appDir;
    [firstPort] = ports;
    firstPid = meta.pid;
    assert.equal(fs.realpathSync(meta.loadoutDir), fs.realpathSync(store));
    assert.equal(fs.existsSync(path.join(appDir, 'data')), false);
    assert.equal(fs.existsSync(marker), false);
    await checkBindings(meta.pid, ports);
    assert.equal(owner(home, source, node, ports[0]), meta.pid, 'actual listener on the managed application');
    const otherHome = path.join(area, 'Other home');
    const otherSource = path.join(area, 'Other install');
    fs.mkdirSync(otherHome);
    fs.mkdirSync(otherSource);
    assert.equal(owner(otherHome, source, node, ports[0]), null);
    assert.equal(owner(home, otherSource, node, ports[0]), null);
    assert.equal(owner(home, source, probe, ports[0]), null);
    assert.equal(auditedLease(rustTest, otherHome, source, node, ports[0]), null);
    assert.equal(auditedLease(rustTest, home, otherSource, node, ports[0]), null);
    assert.equal(auditedLease(rustTest, home, source, probe, ports[0]), null);
    const installedEntry = path.join(source, 'index.js');
    const entryBytes = fs.readFileSync(installedEntry);
    try {
      fs.appendFileSync(installedEntry, '\n// fixture changed source');
      assert.equal(owner(home, source, node, ports[0]), null);
    } finally { fs.writeFileSync(installedEntry, entryBytes); }
    const location = path.join(appDir, 'profile-location.json');
    const originalLocation = fs.readFileSync(location);
    try {
      fs.writeFileSync(location, JSON.stringify({ source, loadoutDir: otherSource }));
      assert.equal(owner(home, source, node, ports[0]), null);
    } finally { fs.writeFileSync(location, originalLocation); }
    const receiptName = fs.readdirSync(path.join(home, 'profile-runtime')).find(name => /^verified-.*\.json$/.test(name));
    assert.ok(receiptName);
    const receipt = path.join(home, 'profile-runtime', receiptName);
    const proofBytes = fs.readFileSync(receipt);
    const proof = JSON.parse(proofBytes);
    assert.match(proof.menu_sha256, /^[a-f0-9]{64}$/);
    for (const legacy of [true, false]) {
      const bad = { ...proof, menu_sha256: '0'.repeat(64) };
      if (legacy) delete bad.menu_sha256;
      fs.writeFileSync(receipt, JSON.stringify(bad));
      try {
        assert.equal(owner(home, source, node, ports[0]), null);
        assert.equal(auditedLease(rustTest, home, source, node, ports[0]), null);
      } finally { fs.writeFileSync(receipt, proofBytes); }
    }
    for (const rel of ['native-menu.cjs', 'session-progress.cjs', 'ready-room.cjs', 'game/definitions/DT_GameModeDefinition.json',
      'game/proto/Request/StartMatchmakingRequest.proto', 'game/proto/Response/matchmaking_ext.proto']) {
      const file = path.join(appDir, rel), bytes = fs.readFileSync(file);
      fs.appendFileSync(file, '\n');
      try {
        assert.equal(owner(home, source, node, ports[0]), null, rel);
        assert.equal(auditedLease(rustTest, home, source, node, ports[0]), null, rel);
      } finally { fs.writeFileSync(file, bytes); }
    }
    fs.renameSync(receipt, receipt + '.fixture-hold');
    try { assert.equal(owner(home, source, node, ports[0]), null); }
    finally { fs.renameSync(receipt + '.fixture-hold', receipt); }
    assert.equal(owner(home, source, node, ports[0]), meta.pid);
    const roles = await request('GET', '/api/definitions/roles/Sniper');
    assert.equal(roles.status, 200);
    assert.equal(roles.body.roleId, role);
    assert.equal((await request('GET', '/api/definitions/roles/no-such-fixture')).status, 404);
    const native = await nativeClient(appDir, ports[1], request);
    try {
      const archive = await native.archive(role, 'Sniper');
      assert.deepEqual(archive.map(row => row.PrimaryWeapon), ['None', 'None']);
      const a = JSON.parse((await native.query('QueryPlayList', 'QueryPlaylistResponse')).PlaylistsJSON);
      assert.ok(a.PVP.some(entry => entry.GameModeList.includes('Purge')));
      assert.deepEqual(a.PVE.map(entry => entry.GameModeList[0]), ['Rush_PVE_Easy', 'Rush_PVE_Normal', 'Rush_PVE_Hard']);
      const region = await native.query('QueryUnityMatchmakingRegion', 'QueryMatchmakingRegionResponse');
      assert.equal(region.Regions[0].RegionId, (await request('GET', '/')).body.servers[0].region_id);
      assert.equal(hash(fs.readFileSync(disk)), diskBefore);
      const defs = (await request('GET', '/api/definitions/roles/PEACE')).body;
      savedPod = defs.podScope.find(id => id && id !== 'None');
      assert.ok(savedPod);
      const result = await request('PUT', requestPath, { roles: { PEACE: {
        primaryWeapon: gun, secondaryWeapon: 'PEACE_RU-AKM', rightLauncher: { id: savedPod },
        weaponConfigs: { [gun]: { parts: [{ slotType: 9, weaponPartId: mag }] } },
        characterData: { fixture: 'retained' },
      } } });
      assert.equal(result.status, 200, JSON.stringify(result.body));
      assert.equal(result.body.validation.valid, true);
      const [peace] = await native.archive('Peace');
      assert.equal(peace.PrimaryWeapon, gun);
      assert.equal(peace.RightPylon, savedPod);
      assert.equal(peace.WeaponConfig.Weapons.find(w => w.WeaponId === gun).Parts.find(p => p.SlotId === 9).PartId, mag);
      await native.equip('PEACE', 6, '');
      const reread = await request('GET', requestPath + '/PEACE');
      assert.equal(reread.body.rightLauncher.id, 'None');
      assert.equal(JSON.parse(fs.readFileSync(disk)).roles[role]._skinToken, 'previous-role');
    } finally { native.close(); }
    expectedArmory = await writeNativeArmory(appDir, ports[1], request);
    previousMenu = await menuRun(home, source, { meta, ports, request });
  });
  assert.equal(owner(home, source, node, firstPort), null);
  assert.equal(auditedLease(rustTest, home, source, node, firstPort), null);
  await unrelatedListener(home, source);
  console.log('PASS live PID ownership, separate launcher, wrong home/install/Node, changed source/location, missing receipt, foreign and closed listeners');
  console.log('PASS Rust launch, 3 TCP + 1 UDP loopback listeners, session, PvE/region, alias and two-way REST/RPC loadout');
  const written = hash(fs.readFileSync(disk));
  const armoryWritten = hash(fs.readFileSync(armoryDisk));
  assert.notEqual(written, diskBefore);
  await serve(home, source, env, async ({ meta, ports, request }) => {
    assert.equal(meta.appDir, appDir);
    assert.notEqual(meta.pid, firstPid);
    assert.equal(owner(home, source, node, ports[0]), meta.pid);
    await menuRun(home, source, { meta, ports, request }, previousMenu);
    const armory = await nativeClient(appDir, ports[1], request, armoryAccount);
    try {
      const keys = ['pEaCe', 'PEACE', 'Peace'];
      const roles = await armory.archive(...keys);
      assert.deepEqual(roles.map(row => row.RoleID), keys);
      for (const row of roles) assert.deepEqual(readArmory(row), expectedArmory, row.RoleID);
      assert.deepEqual(Object.keys(JSON.parse(fs.readFileSync(armoryDisk)).roles), ['Peace']);
    } finally { armory.close(); }
    const native = await nativeClient(appDir, ports[1], request);
    try {
      const [peace] = await native.archive('Peace');
      assert.equal(peace.PrimaryWeapon, gun);
      assert.equal(peace.RightPylon, 'None');
      assert.deepEqual((await request('GET', requestPath)).body.roles.PEACE.characterData, { fixture: 'retained' });
    } finally { native.close(); }
  });
  assert.equal(hash(fs.readFileSync(disk)), written);
  assert.equal(hash(fs.readFileSync(armoryDisk)), armoryWritten);
  console.log('PASS native-only mixed-case writes, fresh session after restart, both weapons, both magazines, both pylons, single original role key');
  console.log('PASS local-archive-3961c933: verified lease path, closed/foreign listener and altered menu receipt/module/table/schema rejected; native menu restart');
  assert.equal(fs.readFileSync(untouched, 'utf8'), '{"roles":{}}');
  assert.equal(signature(source), srcBefore);
  assert.equal(fs.existsSync(path.join(appDir, 'data')), false);
  console.log('PASS original data junction, restart, read-only byte equality, untouched peer profile and original application');

  const index = path.join(source, 'index.js');
  const before = fs.readFileSync(index);
  fs.appendFileSync(index, '\nthrow new Error("fixture unknown entry");\n');
  const unknown = managed();
  assert.equal(unknown.kind, 'unsupported');
  assert.match(unknown.reason, /index\.js SHA-256/);
  fs.writeFileSync(index, before);
  const modeData = path.join(source, 'game/definitions/DT_GameModeDefinition.json');
  const modeBytes = fs.readFileSync(modeData);
  fs.appendFileSync(modeData, ' ');
  assert.equal(managed().kind, 'unsupported');
  fs.writeFileSync(modeData, modeBytes);
  const localEntry = path.join(appDir, 'native-menu.cjs');
  const originalEntry = fs.readFileSync(localEntry);
  fs.appendFileSync(localEntry, '\n// fixture drift\n');
  const broken = inspect();
  assert.notEqual(broken.status, 0);
  assert.match(broken.stderr, /Managed application differs/);
  fs.writeFileSync(localEntry, originalEntry);
  const recovered = managed();
  assert.equal(recovered.appDir, appDir, JSON.stringify(recovered));
  console.log('PASS explicit unsupported version, corrupt managed cache diagnostic, recovery');

  const deploy = require(path.join(base, '../loadout/deploy.cjs')).deploy;
  assert.equal(deploy('apply', source).state, 'installed');
  const gitEnv = { ...process.env, GIT_CEILING_DIRECTORIES: area };
  // 用开发端 Git 独立构造已预装的组合；实际启动不调用 Git。
  execFileSync('git', ['init', '--quiet', '--template='], { cwd: source, env: gitEnv, windowsHide: true });
  for (const patch of ['../native-menu/playlist.patch', '../match-ui/role-read-case.patch']) {
    execFileSync('git', ['apply', path.resolve(base, patch)], { cwd: source, env: gitEnv, windowsHide: true });
  }
  const combined = managed();
  assert.equal(combined.appDir, appDir, JSON.stringify(combined));
  execFileSync('git', ['apply', path.resolve(base, '../loadout/native-role-write.patch')],
    { cwd: source, env: gitEnv, windowsHide: true });
  const nativeCombined = managed();
  assert.equal(nativeCombined.appDir, appDir, JSON.stringify(nativeCombined));
  for (const name of ['session-progress.patch', 'ready-room.patch']) {
    execFileSync('git', ['apply', path.resolve(base, '../native-menu', name)], { cwd: source, env: gitEnv, windowsHide: true });
    assert.equal(managed().appDir, appDir, name);
  }
  assert.equal(hash(fs.readFileSync(disk)), written);
  assert.equal(hash(fs.readFileSync(armoryDisk)), armoryWritten);
  assert.equal(fs.existsSync(marker), false);
  console.log('PASS three- and four-patch preinstalled sources converge to the same application and profiles');

  // 新安装尚未写入首个角色档案；准备操作只解析路径。
  const fresh = path.join(area, 'Fresh install');
  copyApplication(snapshot, dependencies, fresh);
  const freshMeta = JSON.parse(execFileSync(probe, [home, fresh, node], { env, encoding: 'utf8', windowsHide: true }));
  assert.equal(freshMeta.kind, 'managed');
  assert.equal(freshMeta.loadoutDir, path.join(fs.realpathSync(fresh), 'data/loadouts'));
  assert.equal(fs.existsSync(path.join(fresh, 'data')), false);
  assert.notEqual(freshMeta.appDir, appDir);
  console.log('PASS fresh installation uses its own first-save location');
}

async function verifyOfficial(area) {
  const home = path.join(area, 'Official fixtures');
  fs.mkdirSync(home);
  assert.equal(hash(fs.readFileSync(releaseZip)), 'fff143b06c2782d559a19c86d32979c09fdeaa29afcccc99a16637c70fc16094');
  const releaseRoot = path.join(area, 'V0.8.4');
  fs.mkdirSync(releaseRoot);
  const name = 'BoundaryMetaServer-main';
  const official = path.join(releaseRoot, name);
  // 只解出应用代码、定义和依赖，发布包中的玩家档案不进入回归目录。
  execFileSync('tar', ['-xf', releaseZip, '-C', releaseRoot, ...['index.js', 'game', 'node_modules'].map(p => name + '/' + p)]);
  const main = path.join(area, 'MetaServer main', name);
  fs.mkdirSync(path.join(main, 'game'), { recursive: true });
  fs.copyFileSync(path.join(mainSource, 'index.js'), path.join(main, 'index.js'));
  for (const rel of ['game/proto', 'game/definitions']) {
    fs.cpSync(path.join(mainSource, rel), path.join(main, rel), { recursive: true });
  }
  fs.cpSync(fs.realpathSync(process.env.PROFILE_TEST_MAIN_MODULES || path.join(mainSource, 'node_modules')),
    path.join(main, 'node_modules'), { recursive: true });
  const plan = src => JSON.parse(execFileSync(probe, [home, src, node], { encoding: 'utf8', windowsHide: true }));
  const apps = [];
  for (const [src, kind] of [[official, 'projectrebound-release-v0.8.4'], [main, 'metaserver-main-007850d0']]) {
    const initial = signature(src);
    const prepared = plan(src);
    assert.equal(prepared.kind, 'managed', JSON.stringify(prepared));
    apps.push(prepared.appDir);
    assert.equal(JSON.parse(fs.readFileSync(path.join(prepared.appDir, 'profile-location.json'))).sourceKind, kind);
    const data = path.join(src, 'data/loadouts');
    const disk = path.join(data, armoryAccount + '.json');
    assert.equal(prepared.loadoutDir, path.join(fs.realpathSync(src), 'data/loadouts'));
    assert.ok(!fs.existsSync(data));
    const peer = path.join(data, 'untouched_fixture.json');
    let expected, previousMenu;
    await serve(home, src, process.env, async ({ meta, ports, request }) => {
      await checkBindings(meta.pid, ports);
      assert.equal(owner(home, src, node, ports[0]), meta.pid);
      const other = src === main ? official : main;
      assert.equal(owner(home, other, node, ports[0]), null);
      assert.equal(auditedLease(rustTest, home, other, node, ports[0]), null);
      if (src === main) {
        const store = path.join(main, 'game/loadoutStore.js');
        fs.writeFileSync(store, '// synthetic mixed installation');
        try {
          assert.equal(owner(home, src, node, ports[0]), null);
          assert.equal(plan(src).kind, 'unsupported');
        } finally { fs.unlinkSync(store); }
      }
      const discovery = (await request('GET', '/')).body.servers;
      assert.ok(discovery.length && discovery.every(s => s.ipv4 === '127.0.0.1' && s.port === ports[2]));
      const session = await nativeClient(meta.appDir, ports[1], request, armoryAccount);
      try {
        const list = JSON.parse((await session.query('QueryPlayList', 'QueryPlaylistResponse')).PlaylistsJSON);
        assert.deepEqual(list.PVE.map(p => p.GameModeList[0]), ['Rush_PVE_Easy', 'Rush_PVE_Normal', 'Rush_PVE_Hard']);
        assert.ok(list.PVP.some(p => p.GameModeList.includes('Purge')));
        const region = await session.query('QueryUnityMatchmakingRegion', 'QueryMatchmakingRegionResponse');
        assert.equal(region.Regions[0].RegionId, discovery[0].region_id);
        const roleDef = await request('GET', '/api/definitions/roles/Sniper');
        assert.equal(roleDef.body.roleId, 'SNIPER');
        await session.archive('Peace');
        assert.ok(!fs.existsSync(data), 'read-only session must not create an archive directory');
        await session.equip('Peace', 2, 'PROBE_GSW-DMR', 1);
        assert.ok(!fs.existsSync(disk), 'rejected first edit must not create a player archive');
      } finally { session.close(); }
      expected = await writeNativeArmory(meta.appDir, ports[1], request);
      assert.deepEqual(Object.keys(JSON.parse(fs.readFileSync(disk)).roles), ['Peace']);
      const update = await request('PUT', '/api/loadout/' + armoryAccount, {
        roles: { pEaCe: { primaryWeapon: armoryGuns[0], secondaryWeapon: armoryGuns[1] } },
      });
      assert.equal(update.status, 200);
      assert.deepEqual(Object.keys(JSON.parse(fs.readFileSync(disk)).roles), ['Peace']);
      const split = path.join(data, 'split_fixture.json');
      fs.writeFileSync(split, JSON.stringify({ roles: {
        Peace: { primaryWeapon: armoryGuns[0] }, PEACE: { primaryWeapon: 'None' },
      } }));
      const splitBytes = fs.readFileSync(split);
      const splitRole = spelling => request('GET', '/api/loadout/split_fixture/' + spelling);
      assert.equal((await splitRole('Peace')).body.primaryWeapon, armoryGuns[0]);
      assert.equal((await splitRole('PEACE')).body.primaryWeapon, 'None');
      assert.equal((await splitRole('pEaCe')).body.primaryWeapon, armoryGuns[1]);
      assert.equal((await splitRole('unknown_fixture')).status, 404);
      assert.deepEqual(fs.readFileSync(split), splitBytes);
      fs.writeFileSync(peer, '{"roles":{}}');
      previousMenu = await menuRun(home, src, { meta, ports, request });
    });
    const written = hash(fs.readFileSync(disk));
    await serve(home, src, process.env, async ({ meta, ports, request }) => {
      await menuRun(home, src, { meta, ports, request }, previousMenu);
      const session = await nativeClient(meta.appDir, ports[1], request, armoryAccount);
      try {
        const keys = ['Peace', 'PEACE', 'pEaCe'];
        const roles = await session.archive(...keys);
        assert.deepEqual(roles.map(row => row.RoleID), keys);
        for (const row of roles) assert.deepEqual(readArmory(row), expected);
        assert.equal((await request('GET', '/api/loadout/' + armoryAccount + '/PEACE')).body.primaryWeapon, armoryGuns[0]);
      } finally { session.close(); }
    });
    assert.equal(hash(fs.readFileSync(disk)), written);
    assert.equal(fs.readFileSync(peer, 'utf8'), '{"roles":{}}');
    assert.equal(signature(src), initial);
    const schema = path.join(src, 'game/proto/Response/ResponseWrapper.proto');
    fs.appendFileSync(schema, '\n// fixture changed protocol\n');
    assert.equal(plan(src).kind, 'unsupported');
    console.log('PASS ' + kind + ': first save, loopback/owner, PvE/PvP/region, native menu and armory/REST after restart, drift rejected');
  }
  assert.notEqual(apps[0], apps[1]);
  fs.copyFileSync(path.join(snapshot, 'game/loadoutStore.js'), path.join(official, 'game/loadoutStore.js'));
  assert.match(plan(official).reason, /requires matching/);
}

async function main() {
  execFileSync('cargo', ['test', '--offline', '--locked', '--manifest-path', 'regression/Cargo.toml',
    '--target-dir', build], { cwd: base, stdio: 'inherit', windowsHide: true });
  execFileSync('cargo', ['build', '--offline', '--locked', '--manifest-path', 'regression/Cargo.toml',
    '--target-dir', build], { cwd: base, stdio: 'inherit', windowsHide: true });
  const built = execFileSync('cargo', ['test', '--offline', '--locked', '--no-run', '--message-format=json',
    '--manifest-path', 'regression/Cargo.toml', '--target-dir', build], { cwd: base, encoding: 'utf8', windowsHide: true });
  const tests = built.trim().split(/\r?\n/).map(line => JSON.parse(line))
    .filter(r => r.profile?.test && r.executable && r.target.name === 'profile-runtime-probe');
  assert.equal(tests.length, 1);
  rustTest = tests[0].executable;
  const area = fs.mkdtempSync(path.join(base, '.profile-test-'));
  try {
    probe = path.join(area, 'profile-runtime-probe.exe');
    fs.copyFileSync(path.join(build, 'debug/profile-runtime-probe.exe'), probe);
    await verifyOfficial(area);
    await verify(area);
  }
  finally {
    assert.equal(path.dirname(fs.realpathSync(area)), fs.realpathSync(base));
    assert.ok(path.basename(area).startsWith('.profile-test-'));
    fs.rmSync(area, { recursive: true, maxRetries: 4 });
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
