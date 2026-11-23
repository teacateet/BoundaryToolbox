'use strict';

const assert = require('node:assert/strict');
const { execFileSync, fork } = require('node:child_process');
const { createHash, randomBytes, randomUUID } = require('node:crypto');
const { once } = require('node:events');
const dgram = require('node:dgram');
const fs = require('node:fs');
const http = require('node:http');
const net = require('node:net');
const path = require('node:path');
const test = require('node:test');
const { summarize, sessions, regionTrace } = require('./menu-trace.cjs');
const { pvePlan } = require('./native-menu.cjs');
const { createReadyRoomSession, roomFileFor } = require('./ready-room.cjs');

const archive = path.resolve(process.env.BOUNDARY_RELEASE_084 ||
  path.join(__dirname, '../../.workflow/qa/rebound-084/Release.zip'));
const archiveSha = 'fff143b06c2782d559a19c86d32979c09fdeaa29afcccc99a16637c70fc16094';
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const root = 'BoundaryMetaServer-main';
const fixtureAccount = 'native_menu_range_fixture';

const publishedRegions = [
  { location: 6, id: '336d1f3e-3ecb-11eb-a7dc-3b7705f20f56', name: 'us-east1' },
  { location: 10, id: '11111111-2222-4333-8444-555555555501', name: 'asia-east2' },
];

test('pending allocation rechecks the lease and original selection after the Query ACK', () => {
  const table = JSON.parse(execFileSync('tar', ['-xOf', archive, root + '/game/definitions/DT_GameModeDefinition.json']));
  const mode = 'Rush_PVE_Normal', player = 'query_fixture';
  const plan = pvePlan(table, mode, 'CircularX');
  const lease = { v: 1, mode: 'pve', offline: true, runId: 'first', port: 31001, map: plan.map,
    gameClass: plan.gameClass, difficulty: 'normal', expiresAt: 11000 };
  let now = 1000, room = lease;
  const session = createReadyRoomSession(() => table, () => room, { now: () => now });
  const regions = { ids: [], published: [{ RegionId: publishedRegions[0].id }] };
  function start(ticket, choice) {
    assert.equal(session.start(player, player, mode, ticket, choice, regions).status, 'found');
    assert.ok(session.issueTicket(player, ticket));
    return session.handleTicket(player, { userId: [player], ticketId: ticket }, false);
  }
  for (const invalidate of [() => { now = lease.expiresAt; }, () => { room = { ...lease, port: 31002 }; },
    () => { room = { ...lease, runId: 'second' }; }, () => { session.reset(); },
    () => { start('next', 2); }]) {
    now = 1000;
    room = lease;
    const pending = start('first', 1);
    invalidate();
    assert.equal(pending.assignment(), null);
  }
});

test('region diagnostics distinguish requests from completed writes and redact input', async () => {
  const report = await regionTrace(['Method: GET', 'URL: /?private-token', 'Method: POST', 'URL: /',
    'Method: GET', 'URL: /api/loadout/private-player', '[RECV] Query Matchmaking Region!',
    '[PVE-REGIONS] rpc rows=2 ids=2 names=us-east1,asia-east2', '[PVE-REGIONS] discovery rows=2',
    '[PVE-REGIONS] rpc rows=2 ids=2 names=us-east1,private-player', '[PVE-REGIONS] rpc rows=0 ids=1 names=none',
    '[MM] Recieved a new QoS message, echoing!', '[RECV] Start Matchmaking!']);
  for (const event of Object.values(report.events)) assert.equal(event.count, 1);
  assert.equal(report.events.regionRequest.firstLine, 7);
  assert.deepEqual(report.replies[0].names, ['us-east1', 'asia-east2']);
  assert.ok(!JSON.stringify(report).includes('private'));
});

test('service log reports explicit requests, downstream results and presence separately', async () => {
  const events = await summarize(['[PARTY] Presence InMatching => InHall', '[RECV] Query Playlists!',
    '[RECV] Start Matchmaking!', '[MATCH] MatchServer: ticket=private-ticket status=found',
    '[PARTY] Presence InHall => InShootingRange', '[PARTY] Presence InShootingRange => InHall',
    '[PARTY] Presence InHall => InShootingRange', 'client address or token',
    '[PARTY] Presence InHall => arbitrary']);
  assert.deepEqual(Object.keys(events), ['InMatching -> InHall', 'playlist-request', 'start-request',
    'matcher-found', 'InHall -> InShootingRange', 'InShootingRange -> InHall']);
  assert.deepEqual(events['InHall -> InShootingRange'], { count: 2, firstLine: 5, lastLine: 7 });
  assert.ok(!JSON.stringify(events).includes('private-ticket'));
});

test('retail armory panels bind the same TestWeaponInputAction to GoRange', () => {
  const ui = process.env.BOUNDARY_UI_PROPERTIES ||
    'C:/BrtWork/Assets/exports/ui-current/properties/ProjectBoundary/Content/UI';
  for (const name of ['UMG_Panel_Base', 'UMG_Panel_EditCharacterSlot', 'UMG_Panel_EditWeaponSlot']) {
    const file = path.join(ui, 'Menu/CustomizeSystem/Panel', name + '.json');
    const rows = JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
    const input = rows.find(row => row.Name === 'Default__' + name + '_C').Properties.TestWeaponInputAction;
    assert.equal(input.RowName, 'GoRange');
    assert.equal(input.DataTable.ObjectPath, 'ProjectBoundary/Content/GameData/DataTables/Utility/DT_InputActions.0');
  }
});

function stageRelease(dir, patched, { matcherPort, progress, ready, setupSource }) {
  assert.equal(sha(fs.readFileSync(archive)), archiveSha, 'V0.8.4 release asset SHA-256');
  const tar = args => execFileSync('tar', args, { windowsHide: true });
  assert.equal(tar(['-xOf', archive, 'project_rebound_version.txt']).toString().trim(), 'v0.8.2');
  const app = ready ? path.join(dir, 'profile-runtime', 'apps', 'a'.repeat(64)) : path.join(dir, root);
  fs.mkdirSync(app, { recursive: true });
  tar(['-xf', archive, '-C', app, '--strip-components', '1', root + '/index.js', root + '/game/', root + '/node_modules/']);
  const file = path.join(app, 'index.js');
  const bytes = fs.readFileSync(file);
  assert.equal(sha(bytes), '14ae33e2731895279f39fdfaf6295cd59bc2e2a8deea780e166dd5665d01ad89');
  if (setupSource) setupSource(app);
  if (patched) {
    const git = args => execFileSync('git', args, { cwd: app, windowsHide: true });
    git(['init', '--quiet', '--template=']);
    git(['apply', '--check', path.join(__dirname, 'playlist.patch')]);
    git(['apply', path.join(__dirname, 'playlist.patch')]);
    fs.copyFileSync(path.join(__dirname, 'native-menu.cjs'), path.join(app, 'native-menu.cjs'));
    if (progress) {
      const extra = path.join(__dirname, 'session-progress.patch');
      git(['apply', '--check', extra]);
      git(['apply', extra]);
      git(['apply', '--reverse', '--check', extra]);
      fs.copyFileSync(path.join(__dirname, 'session-progress.cjs'), path.join(app, 'session-progress.cjs'));
      if (ready) {
        const room = path.join(__dirname, 'ready-room.patch');
        git(['apply', '--check', room]);
        git(['apply', room]);
        git(['apply', '--reverse', '--check', room]);
        fs.copyFileSync(path.join(__dirname, 'ready-room.cjs'), path.join(app, 'ready-room.cjs'));
      }
    }
  }
  let code = fs.readFileSync(file, 'utf8');
  // 只调整测试副本的监听配置。就绪时回传 OS 分配的端口，整套会话和 RPC 处理器保持原样。
  for (const [old, local] of [
    ['const MatchmakingHost = "204.12.195.98";', 'const MatchmakingHost = "127.0.0.1";'],
    ['const MATCHSERVER_API = `http://${MatchmakingHost}:9001`;',
      'const MATCHSERVER_API = "http://127.0.0.1:' + (matcherPort || 0) + '";'],
    ['"endpoint": "127.0.0.1:6969"', '"endpoint": "127.0.0.1:" + server.address().port'],
    ['app.listen(process.env.PORT || 8000, () => {', 'const web = app.listen(0, "127.0.0.1", () => {'],
    ['server.listen(6969, () => {', 'server.listen(0, "127.0.0.1", () => {'],
    ['matchmakingUDPServer.bind(9000);', 'matchmakingUDPServer.bind(0, "127.0.0.1");'],
    ['matchmakingTCPServer.listen(9000);', 'matchmakingTCPServer.listen(0, "127.0.0.1", () => process.send({ http: web.address().port, rpc: server.address().port }));'],
  ]) {
    assert.equal(code.split(old).length, 2, old);
    code = code.replace(old, local);
  }
  fs.writeFileSync(file, code);
  return app;
}

async function httpJson(port, route, body) {
  return new Promise((resolve, reject) => {
    const req = http.request({ host: '127.0.0.1', port, path: route, method: body ? 'POST' : 'GET',
      agent: false, headers: { 'Content-Type': 'application/json' }, timeout: 4000 }, res => {
      let raw = '';
      res.setEncoding('utf8');
      res.on('data', data => { raw += data; });
      res.on('error', reject);
      res.on('end', () => {
        try { assert.equal(res.statusCode, 200); resolve(JSON.parse(raw)); }
        catch (error) { reject(error); }
      });
    });
    req.on('timeout', () => req.destroy(new Error('Local HTTP timeout')));
    req.on('error', reject);
    req.end(body && JSON.stringify(body));
  });
}

async function withLogin(app, ports, account, run, beforeBind) {
  async function login(playerId) {
    const session = await httpJson(ports.http, '/connectServer', { playerId, platform: 'steam', version: '124840' });
    assert.equal(session.userId, playerId);
    assert.equal(session.endpoint, '127.0.0.1:' + ports.rpc);
    assert.ok(session.gateToken);
    return session;
  }
  const session = await login(account);
  const pb = require(path.join(app, 'node_modules/protobufjs'));
  const type = (dir, name) => pb.loadSync(path.join(app, 'game/proto', dir, name + '.proto'))
    .lookupType('ProjectBoundary.' + name);
  const envelope = type('Request', 'RequestWrapper');
  const response = type('Response', 'ResponseWrapper');
  const socket = net.createConnection(ports.rpc, '127.0.0.1');
  socket.setTimeout(4000, () => socket.destroy(new Error('Local menu RPC timeout')));
  try {
    await once(socket, 'connect');
    const stream = socket[Symbol.asyncIterator]();
    let pending = Buffer.alloc(0), id = 0;
    async function receive(wrapper) {
      while (pending.length < 4 || pending.length < 4 + pending.readUInt32BE()) {
        const chunk = await stream.next();
        if (chunk.done) throw new Error('Menu RPC closed before response');
        pending = Buffer.concat([pending, chunk.value]);
      }
      const end = 4 + pending.readUInt32BE();
      const reply = wrapper.decode(pending.subarray(4, end));
      pending = pending.subarray(end);
      return reply;
    }
    function frame(messageId, route, request) {
      const bytes = envelope.encode({ MessageId: messageId, RPCPath: route, Message: request }).finish();
      const prefix = Buffer.alloc(4);
      prefix.writeUInt32BE(bytes.length);
      return Buffer.concat([prefix, bytes]);
    }
    async function rpc(route, request, wrapper = response) {
      socket.write(frame(++id, route, request));
      const reply = await receive(wrapper);
      assert.equal(reply.MessageId, id);
      assert.ok(reply.RPCPath === route, 'reply route matches request');
      if (wrapper === response) assert.equal(reply.ErrorCode, 0);
      return reply.Message;
    }
    async function pipeline(calls) {
      const first = id + 1;
      socket.write(Buffer.concat(calls.map(([route, request]) => frame(++id, route, request))));
      for (const [index, [route]] of calls.entries()) {
        const reply = await receive(response);
        assert.deepEqual([reply.MessageId, reply.RPCPath, reply.ErrorCode], [first + index, route, 0]);
      }
    }
    async function push(method) {
      const reply = await receive(response);
      assert.deepEqual([reply.MessageId, reply.RPCPath, reply.ErrorCode], [-1, method, 0]);
      return Buffer.from(reply.Message);
    }
    const bind = session => rpc(session.gateToken, Buffer.alloc(0), envelope);
    if (beforeBind) await beforeBind(rpc, type);
    await bind(session);
    await run(rpc, type, async playerId => bind(await login(playerId)), push, pipeline, socket.localPort);
  } finally { socket.destroy(); }
}

function partyClient(rpc, type) {
  const empty = Buffer.alloc(0);
  async function call(method, response, body = empty) {
    const reply = type('Response', response).decode(await rpc('/party.party/' + method, body));
    assert.ok(Object.hasOwn(reply, 'StatusCode'));
    assert.equal(reply.StatusCode, 0);
    return reply;
  }
  return {
    create: () => call('Create', 'CreatePartyResponse'),
    async members() {
      const reply = await call('QueryPresence', 'QueryPartyPresenceResponse');
      return reply.PartyMembers.map(member => [member.UserId, member.Status]);
    },
    set: presence => call('SetPresence', 'SetPartyPresenceResponse',
      type('Request', 'SetPartyPresenceRequest').encode({ Presence: presence }).finish()),
  };
}

async function exerciseSoloParties(app, ports, readRegions) {
  const first = 'solo_fixture_a', second = 'solo_fixture_b', third = 'solo_fixture_c';
  await withLogin(app, ports, first, async (rpc, type, rebind) => {
    const a = partyClient(rpc, type);
    const firstParty = await a.create();
    assert.deepEqual(firstParty.PartyMembers, [first]);
    await a.set('InHall');
    const list = type('Response', 'QueryPlaylistResponse').decode(
      await rpc('/matchmaking.Matchmaking/QueryPlayList', Buffer.alloc(0)));
    assert.equal(list.StatusCode, 0);
    assert.deepEqual(JSON.parse(list.PlaylistsJSON).PVE.map(row => row.GameModeList),
      [['Rush_PVE_Easy'], ['Rush_PVE_Normal'], ['Rush_PVE_Hard']]);
    await readRegions(rpc, type);
    await a.set('InShowRoom');
    await withLogin(app, ports, second, async (peerRpc, peerType) => {
      const b = partyClient(peerRpc, peerType);
      const secondParty = await b.create();
      assert.deepEqual(secondParty.PartyMembers, [second]);
      assert.ok(firstParty.PartyId && secondParty.PartyId);
      assert.notEqual(firstParty.PartyId, secondParty.PartyId);
      assert.deepEqual(await b.members(), [[second, 'InMatching']]);
      await b.set('InHall');
      assert.deepEqual(await a.members(), [[first, 'InShowRoom']]);
      await rebind(third);
      assert.deepEqual(await a.members(), [[third, 'InMatching']]);
      const thirdParty = await a.create();
      assert.deepEqual(thirdParty.PartyMembers, [third]);
      assert.ok(thirdParty.PartyId && ![firstParty.PartyId, secondParty.PartyId].includes(thirdParty.PartyId));
      await a.set('InTutorial');
      assert.deepEqual(await b.members(), [[second, 'InHall']]);
      assert.deepEqual(await a.members(), [[third, 'InTutorial']]);
      assert.equal((await peerRpc('/party.party/Get', Buffer.alloc(0))).length, 0);
    });
  }, async (rpc, type) => {
    // 此时 HTTP 会话已签发，当前 RPC 连接尚未绑定。
    const set = type('Request', 'SetPartyPresenceRequest').encode({ Presence: 'InTutorial' }).finish();
    for (const [route, body] of [['Create', Buffer.alloc(0)], ['SetPresence', set], ['QueryPresence', Buffer.alloc(0)]]) {
      assert.equal((await rpc('/party.party/' + route, body)).length, 0);
    }
  });
}

async function exchangeMenu(app, ports, patched) {
  await withLogin(app, ports, fixtureAccount, async (rpc, type) => {
    const matching = '/matchmaking.Matchmaking/';
    const list = type('Response', 'QueryPlaylistResponse').decode(await rpc(matching + 'QueryPlayList', Buffer.alloc(0)));
    assert.equal(list.StatusCode, 0);
    const catalog = JSON.parse(list.PlaylistsJSON);
    assert.deepEqual(catalog.PVP.map(row => row.GameModeList), [['Purge']]);
    const region = type('Response', 'QueryMatchmakingRegionResponse').decode(
      await rpc(matching + 'QueryUnityMatchmakingRegion', Buffer.alloc(0)));
    assert.equal(region.StatusCode, 0);
    if (patched) {
      assert.deepEqual(catalog.PVE.map(row => row.GameModeList), [['Rush_PVE_Easy'], ['Rush_PVE_Normal'], ['Rush_PVE_Hard']]);
      const discovery = await httpJson(ports.http, '/');
      assert.ok(discovery.servers[0].region_id);
      assert.deepEqual(region.Regions.map(row => row.RegionId), discovery.servers.map(row => row.region_id));
    } else {
      assert.equal(catalog.PVE, undefined);
      assert.equal(region.Regions[0].RegionId, '');
    }
    const query = type('Request', 'QueryNotificationRequest');
    const notice = type('Response', 'QueryNotificationResponse').decode(await rpc('/notification.Notification/QueryNotification',
      query.encode({ Platform: 'steam', LanguageCode: 'en' }).finish()));
    assert.deepEqual(notice.Notifications.map(n => n.Title),
      ['4/01/2026 Patchnotes', '3/31/2026 Patchnotes', 'Project Rebound Alpha']);

    const set = type('Request', 'SetPartyPresenceRequest');
    const ack = type('Response', 'SetPartyPresenceResponse');
    const presence = type('Response', 'QueryPartyPresenceResponse');
    for (const state of ['InHall', 'InShootingRange', 'InShowRoom', 'InShootingRange', 'InHall', 'InTutorial', 'InHall']) {
      const reply = await rpc('/party.party/SetPresence', set.encode({ Presence: state }).finish());
      assert.equal(ack.decode(reply).StatusCode, 0);
      const result = presence.decode(await rpc('/party.party/QueryPresence', Buffer.alloc(0)));
      assert.equal(result.StatusCode, 0);
      assert.deepEqual(result.PartyMembers.map(member => member.Status), [state]);
    }
  });
}

async function checkRelease(patched, options = {}) {
  const dir = fs.mkdtempSync(path.join(__dirname, '.release-menu-'));
  let child, closed;
  try {
    const app = stageRelease(dir, patched, options);
    const env = { NO_PROXY: '*' };
    for (const key of ['SystemRoot', 'WINDIR', 'TEMP', 'TMP']) if (process.env[key]) env[key] = process.env[key];
    child = fork(path.join(app, 'index.js'), [], { cwd: app, stdio: ['ignore', options.trace ? 'pipe' : 'ignore', 'pipe', 'ipc'],
      env, execArgv: ['--no-global-search-paths'], windowsHide: true });
    if (options.trace) require('node:readline').createInterface({ input: child.stdout }).on('line', line => {
      if (line.startsWith('[PVE-MENU ') || line.startsWith('[PVE-ROOM] ')) options.trace.push(line);
    });
    let errors = '';
    child.stderr.on('data', data => { errors += data; });
    closed = once(child, 'close');
    const [ports] = await Promise.race([once(child, 'message', { signal: AbortSignal.timeout(10000) }),
      closed.then(() => { throw new Error('Local release exited: ' + errors); })]);
    if (options.run) await options.run(app, ports);
    else await exchangeMenu(app, ports, patched);
    assert.equal(fs.existsSync(path.join(app, 'data')), false);
  } finally {
    if (child && child.exitCode === null && child.signalCode === null) child.kill();
    if (closed) await closed;
    assert.equal(path.dirname(fs.realpathSync(dir)), fs.realpathSync(__dirname));
    fs.rmSync(dir, { recursive: true, maxRetries: 4 });
  }
}

test('V0.8.4 stock session: playlist, region, operator notices and seven menu presence round trips',
  () => checkRelease(false));
test('V0.8.4 with the existing patch: all three PvE modes, preserved PvP and published discovery region',
  () => checkRelease(true));

function stageRegionSources(dir) {
  const release = path.join(dir, 'release'), kit = path.join(dir, 'kit');
  fs.mkdirSync(release);
  fs.mkdirSync(kit);
  assert.equal(sha(fs.readFileSync(archive)), archiveSha);
  execFileSync('tar', ['-xf', archive, '-C', release, '--strip-components', '1',
    root + '/index.js', root + '/game/', root + '/node_modules/'], { windowsHide: true });
  const registry = fs.readFileSync(path.resolve(__dirname, '../../src/core/local_profile.rs'), 'utf8');
  for (const [, rel] of registry.matchAll(/include_bytes!\("\.\.\/\.\.\/integrations\/([\w./-]+)"\)/g)) {
    fs.copyFileSync(path.resolve(__dirname, '..', rel), path.join(kit, path.basename(rel)));
  }
  assert.ok(fs.existsSync(path.join(kit, 'region-catalog.patch')), 'registered production region patch');
  const sources = new Map([['projectrebound-release-v0.8.4', release]]);
  for (const [kind, origin, files] of [
    ['metaserver-main-007850d0', process.env.PROFILE_TEST_MAIN || 'C:/BrtWork/SDK/BoundaryMetaServer-main',
      ['index.js', 'game/definitions', 'game/proto']],
    ['local-archive-3961c933', process.env.BOUNDARY_META_SNAPSHOT ||
      'C:/BrtWork/SDK/sdk-fixture/validation-server',
      ['index.js', 'game/definitions', 'game/proto', 'game/loadoutStore.js', 'game/definitionIndex.js']],
  ]) {
    const copy = path.join(dir, kind);
    fs.mkdirSync(path.join(copy, 'game'), { recursive: true });
    for (const file of files) fs.cpSync(path.join(origin, file), path.join(copy, file), { recursive: true });
    fs.symlinkSync(path.join(release, 'node_modules'), path.join(copy, 'node_modules'), 'junction');
    sources.set(kind, copy);
  }
  const original = fs.readFileSync(path.join(sources.get('metaserver-main-007850d0'), 'index.js'), 'utf8').replaceAll('\r\n', '\n');
  assert.equal(sha(original), 'de98c06ce2ba56d4c35312d634883cce99df7b659ccca4bbef49b4f748c60f9a');
  for (const [i, row] of publishedRegions.entries()) {
    assert.ok(original.includes('location_id: ' + row.location + ', region_id: "' + row.id + '"'));
    assert.ok(original.includes('RegionId: matchmakingUDPServerDiscoveryPayload.servers[' + i + '].region_id, RegionName: "' + row.name + '"'));
  }
  return { sources, kit };
}

async function reserveRegionPorts() {
  for (let attempt = 1; ; attempt++) {
    try { return await reserveTcpAndUdp(); } catch (err) {
      // Windows 的 TCP 动态端口可能处在 UDP 排除范围内；仅夹具重选端口。
      if (!['EADDRINUSE','EACCES'].includes(err.code) || attempt >= 8) throw err;
    }
  }
}

async function reserveTcpAndUdp() {
  const tcp = [], udp = dgram.createSocket('udp4');
  try {
    for (let i = 0; i < 3; i++) {
      const socket = net.createServer();
      tcp.push(socket);
      socket.listen(0, '127.0.0.1');
      await once(socket, 'listening');
    }
    const [http, rpc, qos] = tcp.map(socket => socket.address().port);
    udp.bind(qos, '127.0.0.1');
    await once(udp, 'listening');
    return { http, rpc, qos };
  } finally {
    await new Promise(done => udp.close(done));
    for (const socket of tcp) if (socket.listening) await new Promise(done => socket.close(done));
  }
}

async function exerciseAutoPool(app, ports, signer, archiveCheck = false) {
  const fixtureAccount = '76561198185503627';
  const control = net.createServer(), join = net.createServer(), sockets = new Set();
  let game = dgram.createSocket('udp4');
  let prepares = 0;
  control.on('connection', () => prepares++);
  const file = roomFileFor(app);
  const table = require(path.join(app, 'game/definitions/DT_GameModeDefinition.json'));
  const pb = require(path.join(app, 'node_modules/protobufjs'));
  const runId = randomUUID();
  const empty = Buffer.alloc(0), prefix = '/matchmaking.Matchmaking/';
  try {
    for (const server of [control, join]) {
      server.on('connection', socket => {
        sockets.add(socket);
        socket.on('error', () => {});
        socket.once('close', () => sockets.delete(socket));
        socket.setTimeout(2500, () => socket.destroy());
      });
      server.listen(0, '127.0.0.1');
      await once(server, 'listening');
    }
    game.bind(0, '127.0.0.1');
    await once(game, 'listening');
    const advertisement = server => ({ v: 1, port: server.address().port, nonce: randomBytes(32).toString('hex') });
    const base = { v: 1, autoPool: true, mode: 'pve', offline: true, runId, controlIpc: advertisement(control) };
    const joinIpc = advertisement(join);
    const write = fields => fs.writeFileSync(file, JSON.stringify({ ...base, expiresAt: Date.now() + 14000, ...fields }));
    write({});
    function accept(server) {
      const connection = once(server, 'connection', { signal: AbortSignal.timeout(2500) });
      return connection.then(([socket]) => {
        const lines = require('node:readline').createInterface({ input: socket })[Symbol.asyncIterator]();
        return { socket, read: async () => JSON.parse((await lines.next()).value) };
      });
    }
    let closedOnDisconnect;
    await withLogin(app, ports, fixtureAccount, async (rpc, type, rebind, push, _, clientPort) => {
      const route = (op, bytes = empty) => rpc(prefix + op, bytes);
      const sample = require('../loadout/probe-minifarm-sample.json');
      if (archiveCheck) {
        const role = type('Request','UpdateRoleArchiveV2Request'), gun = type('Request','UpdateWeaponArchiveV2Request');
        for (const [Operation, ItemId] of [[1,sample.primary],[2,sample.secondary],[4,sample.mobility],[5,sample.left],[6,sample.right]]) {
          await rpc('/assets.Assets/UpdateRoleArchiveV2', role.encode({RoleId:sample.role,Operation,ItemId}).finish());
        }
        for (const WeaponArchive of sample.weapons) await rpc('/assets.Assets/UpdateWeaponArchiveV2',
          gun.encode({RoleId:sample.role,WeaponArchive}).finish());
      }
      const regions = type('Response', 'QueryMatchmakingRegionResponse').decode(await route('QueryUnityMatchmakingRegion'));
      const hongKong = regions.Regions.find(r => r.RegionName === 'asia-east2').RegionId;
      const modes = ['Rush_PVE_Easy', 'Rush_PVE_Normal', 'Rush_PVE_Hard'];
      const cards = async () => JSON.parse(type('Response', 'QueryPlaylistResponse').decode(await route('QueryPlayList')).PlaylistsJSON);
      const full = await cards();
      assert.deepEqual(full.PVE.map(row => row.Name), modes);
      const click = async (mode, region = hongKong) => {
        const request = pb.Writer.create().uint32(10).fork().uint32(10).string(fixtureAccount).ldelim()
          .uint32(18).string(mode);
        for (const id of [].concat(region)) request.uint32(26).string(id);
        assert.equal(Buffer.from(await route('StartUnityMatchmaking', request.finish())).toString('hex'), '0800');
      };
      const unique = new Set();
      async function prepare(mode, region) {
        const connection = accept(control);
        await click(mode, region);
        const peer = await connection;
        const offer = await peer.read();
        // 等待房间就绪前，大厅已收到建票进度。
        assert.equal((await push('StartCreateTicketServerCall')).length, 0);
        const id = offer.requestId;
        assert.match(id, /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
        assert.ok(!unique.has(id));
        unique.add(id);
        assert.deepEqual(offer, { v: 1, op: 'prepare', runId, requestId: id, mode,
          nonce: base.controlIpc.nonce, clientPort, rpcPort: ports.rpc });
        return { id, socket: peer.socket, prepared(spawn) { peer.socket.write(JSON.stringify({ op: 'prepared', requestId: id, spawn }) + '\n'); } };
      }
      const user = pb.Writer.create().uint32(10).string(fixtureAccount).finish();
      const set = type('Request', 'SetPartyPresenceRequest');
      await click(modes[0], 'unpublished');
      let pending = await prepare(modes[2]);
      let closed = once(pending.socket, 'end', { signal: AbortSignal.timeout(2000) });
      await route('StopUnityMatchmaking', user);
      await closed;
      pending = await prepare(modes[0]);
      closed = once(pending.socket, 'end', { signal: AbortSignal.timeout(2000) });
      const replaced = await prepare(modes[1]);
      await closed;
      // 当前代次和难度均匹配，旧 choiceId 仍不能发票。
      const ready = (mode, choiceId) => {
        const map = ['Warehouse', 'CircularX', 'OSS'][modes.indexOf(mode)];
        return { choiceId, difficulty: mode.slice('Rush_PVE_'.length).toLowerCase(), map, gameClass: pvePlan(table, mode, map).gameClass,
          port: game.address().port, joinIpc };
      };
      write(ready(modes[1], pending.id));
      closed = once(replaced.socket, 'end', { signal: AbortSignal.timeout(2000) });
      replaced.prepared();
      await closed;
      assert.deepEqual(await cards(), full);
      for (const rejected of ['wrong-id', 'expired', 'closed']) {
        const attempt = await prepare(modes[0]);
        const fields = ready(modes[0], attempt.id);
        if (rejected === 'expired') fields.expiresAt = Date.now() - 1;
        write(fields);
        const ended = once(attempt.socket, 'close', { signal: AbortSignal.timeout(2000) });
        if (rejected === 'closed') attempt.socket.end();
        else if (rejected === 'expired') attempt.prepared();
        else attempt.socket.write(JSON.stringify({ op: 'prepared', requestId: randomUUID() }) + '\n');
        await ended;
        assert.deepEqual(await cards(), full);
        write({});
      }
      for (const action of ['hall', 'rebind']) {
        const attempt = await prepare(modes[0]);
        closed = once(attempt.socket, 'end', { signal: AbortSignal.timeout(2000) });
        if (action === 'hall') await rpc('/party.party/SetPresence', set.encode({ Presence: 'InHall' }).finish());
        else await rebind(fixtureAccount);
        await closed;
      }
      for (const mode of modes) {
        const cold = mode === modes[1];
        if (cold) {
          await new Promise(done => game.close(done));
          game = dgram.createSocket('udp4');
          write({});
        }
        const selectedIds = regions.Regions.map(row => row.RegionId);
        const firstIds = cold && selectedIds.length === 1 ? [] : selectedIds;
        const attempt = await prepare(mode, cold ? firstIds : hongKong);
        if (cold) {
          const count = prepares;
          await click(mode, firstIds);
          await click(mode, [...firstIds].reverse());
          // 下一次同步回复也检查帧顺序，额外进度、票据或地址都会使断言失败。
          await route('QueryUnityMatchmaking');
          assert.equal(prepares, count, 'duplicate Start restarted the cold room');
          game.bind(0, '127.0.0.1');
          await once(game, 'listening');
        }
        await route('QueryUnityMatchmaking');
        const canRead = mode === modes[1], token = randomBytes(32);
        const verifier = createHash('sha256').update(token).digest('hex');
        const owner = mode === modes[2] ? '76561198112699624' : fixtureAccount;
        const signature = require('node:crypto').createHmac('sha256', signer)
          .update(['brt-local-room-v1',runId,attempt.id,owner,verifier].join('\n')).digest('hex');
        const proof = { owner, verifier, signature: mode === modes[0] ? '0'.repeat(64) : signature };
        const connectionId = randomUUID(), url = 'http://127.0.0.1:' + ports.http + '/api/brt/v1/rooms/pve-' + attempt.id + '/spawn/';
        const admit = async (account, key = token.toString('hex')) => (await fetch(url + 'admit', {
          method:'POST', headers:{'Content-Type':'application/json',Authorization:'Bearer ' + key},
          body:JSON.stringify({connectionId,authenticatedSteamId:account}) })).status;
        write({...ready(mode, attempt.id), spawnChoice:attempt.id});
        assert.equal(await admit(fixtureAccount),403);
        assert.deepEqual(await cards(), full);
        attempt.prepared(proof);
        const ticket = JSON.parse(await push('CreateTicketServerCall'));
        assert.deepEqual(ticket, { errorCode: 0, ticketId: ticket.ticketId, userIds: [fixtureAccount] });
        const query = pb.Writer.create().uint32(10).string(fixtureAccount).uint32(18).string(ticket.ticketId).finish();
        const joining = accept(join);
        await route('QueryUnityMatchmaking', query);
        assert.deepEqual(JSON.parse(await push('QueryTicketServerCall')), { errorCode: 0, gameServerIP: '127.0.0.1:' + game.address().port });
        const peer = await joining, offer = await peer.read();
        assert.deepEqual(offer, { v: 1, op: 'offer', runId, requestId: offer.requestId, clientPort, rpcPort: ports.rpc,
          gamePort: game.address().port });
        peer.socket.write(JSON.stringify({ op: 'ready', nonce: joinIpc.nonce, requestId: offer.requestId }) + '\n');
        assert.deepEqual(await peer.read(), { op: 'commit', requestId: offer.requestId, nonce: joinIpc.nonce });
        assert.equal(await admit(fixtureAccount,verifier),403);
        assert.equal(await admit('76561198112699624'),403);
        assert.equal(await admit(fixtureAccount),canRead ? 204 : 403);
        if (canRead) {
          // 独立读方按原作字段号读取目录。观察结果不能当成首生或重生。
          const decoder = pb.parse(`syntax = "proto3"; message R { string id=1; repeated string pod=2;
              repeated string mobility=3; repeated string melee=4; repeated string weapon=5; }
              message Catalog { string owner=1; repeated R rows=2; int32 unknown=3; }`).root.lookupType('Catalog');
          const raw = await rpc('/assets.Assets/QueryAssetsInMatch',pb.Writer.create().uint32(10).string(fixtureAccount).finish());
          const catalog = decoder.decode(raw);
          const retail = require('../loadout/coverage/data/catalog.json');
          assert.equal(catalog.owner,fixtureAccount);
          assert.equal(Object.hasOwn(catalog,'unknown'),false);
          assert.deepEqual(catalog.rows.map(r=>r.id).sort(),[...retail.normalRoles].sort());
          for (const row of catalog.rows) {
            const def=retail.roles[row.id];
            for (const [field,scope] of [['pod','podScope'],['mobility','mobilityScope'],['melee','meleeWeaponScope'],['weapon','weaponScope']]) {
              assert.deepEqual([...row[field]].sort(),[...def[scope]].sort(),row.id+' '+field);
            }
          }
        }
        if (canRead && archiveCheck) {
          const {createRoomSpawnClient} = require('../loadout/room-spawn-client.cjs');
          const roomId = 'pve-' + attempt.id, origin = 'http://127.0.0.1:' + ports.http;
          const host = createRoomSpawnClient({origin,roomId,credential:token.toString('hex')});
          const member = await host.admit(fixtureAccount,connectionId);
          const menu = (await member.initialMenu()).roles.find(row => row.CharacterID === sample.role);
          assert.deepEqual(menu.InventoryItems.slice(0,4),[sample.primary,sample.secondary,sample.left,sample.right]);
          const selection = {role:sample.role,primary:sample.primary,secondary:sample.secondary};
          const first = await member.prepare(1,selection);
          assert.equal(first.FirstWeaponPartData.ReceiverMainAppearance.WeaponPartSkinPaintingID,'GSW-DMR_ReceiverMain_PTMetalGold');
          assert.equal(first.SecondWeaponPartData.ReceiverMainAppearance.WeaponPartSkinPaintingID,'GSW-FOP_ReceiverMain_PTElectroRed');
          for (const [gun,sight] of [[first.FirstWeaponPartData,'GSW-DMR_SGT-REFLEX-3X'],[first.SecondWeaponPartData,'GSW-FOP_SGT-REDDOT-MICRO']])
            assert.ok(gun.WeaponPartConfigs.some(p => p.WeaponPartID === sight));
          const next = await member.prepare(2,{...selection,secondary:'None'});
          assert.equal(next.SecondWeaponPartData.WeaponID,'None');
          assert.equal(next.MobilityModuleData.MobilityModuleID,sample.mobility);
          await member.leave();
          if (process.env.BRT_QA_LOCAL_HTTP_EXE) {
            const stdout = execFileSync(process.env.BRT_QA_LOCAL_HTTP_EXE,['--local-room-http'], {windowsHide:true,
              encoding:'utf8',input:JSON.stringify({port:ports.http,steam:fixtureAccount,connection:randomUUID()}),
              env:{...process.env,BRT_LOCAL_SPAWN_ROOM:roomId,BRT_LOCAL_SPAWN_TOKEN:token.toString('hex')}});
            assert.match(stdout,/PASS C\+\+ WinHTTP/);
            console.log(stdout.trim());
          }
        }
        if (mode !== modes[0]) {
          const ended = once(attempt.socket, 'end', { signal: AbortSignal.timeout(2000) });
          attempt.socket.end();
          await ended;
          write({});
          assert.equal(await admit(fixtureAccount),403);
          await route('QueryUnityMatchmaking', query);
          assert.deepEqual(await cards(), full);
          if (mode === modes[1]) await rebind(fixtureAccount);
          for (const Presence of ['InShootingRange', 'InHall', 'InHall'])
            await rpc('/party.party/SetPresence', set.encode({ Presence }).finish());
        } else await route('StopUnityMatchmaking', query);
      }
      pending = await prepare(modes[2]);
      closedOnDisconnect = once(pending.socket, 'end', { signal: AbortSignal.timeout(2000) });
    });
    await closedOnDisconnect;
  } finally {
    for (const socket of sockets) socket.destroy();
    control.close(); join.close(); game.close();
    fs.rmSync(file, { force: true });
  }
}

async function exerciseRegions(app, expected, revised, fixedQos = revised, poolOnly = false, archiveCheck = false) {
  const ports = await reserveRegionPorts();
  const env = { PORT: String(ports.http), RPC_PORT: String(ports.rpc), MATCHMAKING_PORT: String(ports.qos), NO_PROXY: '*' };
  const signer = randomBytes(32);
  if (poolOnly) env.BRT_PROFILE_ROOM_SIGNER = signer.toString('hex');
  for (const key of ['SystemRoot', 'WINDIR', 'TEMP', 'TMP']) if (process.env[key]) env[key] = process.env[key];
  const child = fork(path.join(app, 'index.js'), [], { cwd: app, env, execArgv: ['--no-global-search-paths'],
    windowsHide: true, stdio: ['ignore', 'pipe', 'ignore', 'ipc'] });
  const lines = require('node:readline').createInterface({ input: child.stdout, crlfDelay: Infinity });
  const closed = once(child, 'close');
  const ready = once(lines, 'qos-listening', { signal: AbortSignal.timeout(12000) });
  const localRooms = [];
  const ticketEvents = [];
  const stages = [];
  const trace = regionTrace((async function* () {
    for await (const line of lines) {
      if (line === 'mrooooow >.< - ' + ports.qos) lines.emit('qos-listening');
      if (line.startsWith('[PVE-ROOM] ')) {
        const event = JSON.parse(line.slice('[PVE-ROOM] '.length));
        if (event.event === 'local-feedback') stages.push(event);
        if (event.event === 'selection-ready') localRooms.push([event.mode, event.serverIp, event.serverPort]);
        if (['ticket-created', 'ticket-queried', 'endpoint-sent'].includes(event.event)) ticketEvents.push(event);
      }
      yield line;
    }
  })());
  try {
    await Promise.race([ready, closed.then(() => { throw new Error('Region fixture exited before QoS startup'); })]);
    if (poolOnly) {
      await exerciseAutoPool(app, ports, signer, archiveCheck);
      const names = stages.map(s => s.stage);
      for (const required of ['preparing','ticket-received','cancelled','returned','prepare-ended'])
        assert.ok(names.includes(required),required);
      for (const received of stages.filter(s => s.stage === 'ticket-received')) {
        assert.ok(stages.slice(0,stages.indexOf(received)).some(s =>
          s.run === received.run && s.choice === received.choice && s.stage === 'preparing'));
      }
      const afterTicket = stages.filter(s => s.stage === 'ticket-received').map(ticket =>
        stages.slice(stages.indexOf(ticket) + 1).filter(s => s.run === ticket.run && s.choice === ticket.choice)
          .map(s => s.stage));
      assert.deepEqual(afterTicket, [['cancelled'], [], ['returned']],
        'only the original lobby should report a return after the room handoff, once');
      assert.ok(!names.includes('join_ack'), 'only the toolbox bridge confirms handoff');
      return;
    }
    const { servers } = await httpJson(ports.http, '/');
    assert.deepEqual(servers, expected.map(row => ({ location_id: row.location, region_id: row.id,
      ipv4: '127.0.0.1', ipv6: '', port: ports.qos })));
    const readRegions = async (rpc, type) => {
      const reply = type('Response', 'QueryMatchmakingRegionResponse').decode(
        await rpc('/matchmaking.Matchmaking/QueryUnityMatchmakingRegion', Buffer.alloc(0)));
      assert.equal(reply.StatusCode, 0);
      const asia = reply.Regions.filter(row => row.RegionName.includes('asia'));
      assert.equal(asia.length, Number(revised), 'CN / Eastern Asia rows after retail filtering');
      if (revised) {
        assert.equal(asia[0].RegionName, 'asia-east2');
        assert.match(asia[0].RegionId, /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
        assert.ok(servers.some(server => server.region_id === asia[0].RegionId));
        assert.equal(new Set(reply.Regions.map(row => row.RegionId)).size, reply.Regions.length);
      }
      assert.deepEqual(reply.Regions.map(row => [row.RegionId, row.RegionName]), expected.map(row => [row.id, row.name]));
      return reply.Regions;
    };
    if (revised) await exerciseSoloParties(app, ports, readRegions);
    else await withLogin(app, ports, fixtureAccount, readRegions);
    const probe = dgram.createSocket('udp4');
    try {
      probe.bind(0, '127.0.0.1');
      await once(probe, 'listening');
      // Unity QoS v0: the UTF-8 title length includes its own byte; sequence, ID and timestamp follow.
      const probes = [
        ['59000241013412e803000000000000', '9500013412e803000000000000'],
        ['590008426f756e6461720578560102030405060708', '95000578560102030405060708'],
        ['59000a426f756e64617279210b9abc0807060504030201', '95000b9abc0807060504030201'],
      ];
      for (const server of servers) {
        for (const [request, expectedReply] of probes) {
          const response = once(probe, 'message', { signal: AbortSignal.timeout(3500) });
          probe.send(Buffer.from(request, 'hex'), server.port, server.ipv4);
          const [bytes, peer] = await response;
          assert.deepEqual([peer.address, peer.port], ['127.0.0.1', ports.qos]);
          if (fixedQos) assert.equal(bytes.toString('hex'), expectedReply);
          else assert.notEqual(bytes.toString('hex'), expectedReply, 'unmodified service is a negative control');
        }
        if (fixedQos) {
          const unexpected = once(probe, 'message', { signal: AbortSignal.timeout(250) });
          for (const bad of ['590000000000000000000001fe', '5900ff41013412e803000000000000',
            '59100241013412e803000000000000', probes[0][0].slice(0, -2)]) {
            probe.send(Buffer.from(bad, 'hex'), server.port, server.ipv4);
          }
          await assert.rejects(unexpected, { name: 'AbortError' });
        }
      }
    } finally { probe.close(); }
    if (revised && fixedQos) {
      const game = dgram.createSocket('udp4');
      const bridge = net.createServer();
      let offers = 0;
      bridge.on('connection', () => offers++);
      const file = roomFileFor(app);
      assert.ok(file);
      try {
        bridge.listen(0, '127.0.0.1');
        await once(bridge, 'listening');
        game.bind(0, '127.0.0.1');
        await once(game, 'listening');
        const port = game.address().port;
        const table = require(path.join(app, 'game/definitions/DT_GameModeDefinition.json'));
        await withLogin(app, ports, fixtureAccount, async (rpc, type, rebind, push, _, clientPort) => {
          const prefix = '/matchmaking.Matchmaking/', empty = Buffer.alloc(0);
          const rows = await readRegions(rpc, type);
          const selected = rows.find(row => row.RegionName === 'asia-east2');
          async function playlists() {
            const bytes = await rpc(prefix + 'QueryPlayList', empty);
            const reply = type('Response', 'QueryPlaylistResponse').decode(bytes);
            assert.equal(reply.StatusCode, 0);
            return JSON.parse(reply.PlaylistsJSON);
          }
          const full = await playlists();
          assert.deepEqual(full.PVE.map(row => row.Name), ['Rush_PVE_Easy', 'Rush_PVE_Normal', 'Rush_PVE_Hard']);
          for (const [difficulty, map] of [['normal', 'CircularX'], ['easy', 'Warehouse'], ['hard', 'OSS']]) {
            const plan = pvePlan(table, difficulty.toUpperCase() + ' BOT', map);
            const lease = { v: 1, runId: randomUUID(), expiresAt: Date.now() + 14000,
              mode: 'pve', offline: true, difficulty, map, gameClass: plan.gameClass, port };
            if (difficulty === 'hard') lease.joinIpc = { v: 1, port: bridge.address().port, nonce: randomBytes(32).toString('hex') };
            fs.writeFileSync(file, JSON.stringify(lease));
            const catalog = await playlists();
            assert.deepEqual(catalog, { ...full, PVE: [full.PVE.find(row => row.Name === plan.mode)] });
            assert.deepEqual(catalog.PVE[0].GameModeList, [plan.mode]);
            const pb = require(path.join(app, 'node_modules/protobufjs'));
            const start = (user, regions = difficulty === 'easy' ? [] : [selected.RegionId], qos = ['qos-sample']) => {
              const writer = pb.Writer.create().uint32(10).fork().uint32(10).string(user);
              for (const id of qos) writer.uint32(18).fork().uint32(10).string(id).ldelim();
              writer.ldelim().uint32(18).string(catalog.PVE[0].GameModeList[0]);
              for (const id of regions) writer.uint32(26).string(id);
              return writer.finish();
            };
            const ack = async (send, user, regions, qos) => assert.equal(Buffer.from(
              await send(prefix + 'StartUnityMatchmaking', start(user, regions, qos))).toString('hex'), '0800');
            async function ticket(send, receive, user) {
              await ack(send, user);
              assert.equal((await receive('StartCreateTicketServerCall')).length, 0);
              const data = JSON.parse(await receive('CreateTicketServerCall'));
              assert.match(data.ticketId, /^[a-f0-9-]{36}$/);
              assert.deepEqual(data, { errorCode: 0, ticketId: data.ticketId, userIds: [user] });
              return data.ticketId;
            }
            // 独立于服务 proto，按原生字段号编码玩家列表和票据。
            const wire = (users, id) => {
              const writer = pb.Writer.create();
              for (const user of users) writer.uint32(10).string(user);
              return writer.uint32(18).string(id).finish();
            };
            const call = async (send, op, users, id) =>
              assert.equal((await send(prefix + op + 'UnityMatchmaking', wire(users, id))).length, 0);
            const assignment = async receive => assert.deepEqual(JSON.parse(await receive('QueryTicketServerCall')),
              { errorCode: 0, gameServerIP: '127.0.0.1:' + port });
            const id = await ticket(rpc, push, fixtureAccount);
            await call(rpc, 'Query', [fixtureAccount, 'extra'], id);
            if (difficulty === 'normal') {
              const peer = 'ticket_peer';
              await withLogin(app, ports, peer, async (send, _, bind, receive, pipeline) => {
                const other = await ticket(send, receive, peer);
                assert.notEqual(other, id);
                await call(send, 'Query', [peer], id);
                await call(rpc, 'Stop', [fixtureAccount], other);
                await call(send, 'Query', [fixtureAccount], other);
                await call(send, 'Query', [peer], other);
                await assignment(receive);
                await call(send, 'Stop', [peer], other);
                await call(send, 'Query', [peer], other);
                await send(prefix + 'QueryPlayList', empty);
                await bind(fixtureAccount);
                const rebound = await ticket(send, receive, fixtureAccount);
                await call(send, 'Query', [fixtureAccount], id);
                await call(rpc, 'Stop', [fixtureAccount], rebound);
                const active = wire([fixtureAccount], rebound);
                await pipeline([['Query', active], ['Stop', active]].map(([op, body]) => [prefix + op + 'UnityMatchmaking', body]));
                await call(send, 'Query', [fixtureAccount], rebound);
                await send(prefix + 'QueryPlayList', empty);
              });
            }
            const accepted = lease.joinIpc && once(bridge, 'connection');
            await call(rpc, 'Query', [fixtureAccount], id);
            await assignment(push);
            if (accepted) {
              const [peer] = await Promise.race([accepted, new Promise((_, reject) =>
                setTimeout(() => reject(new Error('Missing managed join offer')), 2000).unref())]);
              const lines = require('node:readline').createInterface({ input: peer })[Symbol.asyncIterator]();
              try {
                const offer = JSON.parse((await lines.next()).value);
                assert.deepEqual(offer, { v: 1, op: 'offer', requestId: offer.requestId, runId: lease.runId,
                  clientPort, rpcPort: ports.rpc, gamePort: port });
                peer.write(JSON.stringify({ op: 'ready', requestId: offer.requestId, nonce: lease.joinIpc.nonce }) + '\n');
                assert.deepEqual(JSON.parse((await lines.next()).value),
                  { op: 'commit', requestId: offer.requestId, nonce: lease.joinIpc.nonce });
              } finally { peer.destroy(); }
            }
            await call(rpc, 'Query', [fixtureAccount], id);
            await assignment(push);
            if (difficulty === 'easy') {
              const set = type('Request', 'SetPartyPresenceRequest');
              for (const Presence of ['InMatching', 'InShowRoom', 'InHall']) {
                await rpc('/party.party/SetPresence', set.encode({ Presence }).finish());
              }
              await call(rpc, 'Query', [fixtureAccount], id);
            }
            await call(rpc, 'Stop', [fixtureAccount], id);
            fs.writeFileSync(file, JSON.stringify({ ...lease, expiresAt: Date.now() - 1 }));
            assert.deepEqual(await playlists(), full);
            if (difficulty === 'hard') {
              fs.writeFileSync(file, JSON.stringify(lease));
              const stale = await ticket(rpc, push, fixtureAccount);
              fs.writeFileSync(file, JSON.stringify({ ...lease, expiresAt: Date.now() - 1 }));
              await call(rpc, 'Query', [fixtureAccount], stale);
              fs.writeFileSync(file, JSON.stringify(lease));
              await call(rpc, 'Query', [fixtureAccount], stale);
              await ack(rpc, fixtureAccount, ['unpublished-region'], [selected.RegionId]);
              await ack(rpc, fixtureAccount, [selected.RegionId, 'unpublished-region'], [selected.RegionId, 'qos-sample']);
              await rpc(prefix + 'QueryPlayList', empty);
            }
          }
          fs.unlinkSync(file);
          assert.deepEqual(await playlists(), full);
          const returned = await readRegions(rpc, type);
          assert.equal(returned.find(row => row.RegionName === selected.RegionName).RegionId, selected.RegionId);
        });
        assert.deepEqual(localRooms, ['Normal', 'Normal', 'Normal', 'Easy', 'Hard', 'Hard']
          .map(mode => ['Rush_PVE_' + mode, '127.0.0.1', port]));
        const byChoice = new Map();
        for (const event of ticketEvents) {
          assert.deepEqual(Object.keys(event).sort(), ['choice', 'event', 'mode']);
          assert.ok(Number.isSafeInteger(event.choice) && event.choice > 0);
          if (!byChoice.has(event.choice)) byChoice.set(event.choice, [event.mode]);
          byChoice.get(event.choice).push(event.event);
        }
        const queried = ['ticket-queried', 'endpoint-sent'];
        const opened = mode => ['Rush_PVE_' + mode, 'ticket-created'];
        assert.deepEqual([...byChoice.values()], [
          [...opened('Normal'), ...queried, ...queried], [...opened('Normal'), ...queried], [...opened('Normal'), 'ticket-queried'],
          [...opened('Easy'), ...queried, ...queried], [...opened('Hard'), ...queried, ...queried],
          [...opened('Hard'), 'ticket-queried'],
        ]);
        assert.equal(offers, 1);
      } finally { game.close(); bridge.close(); }
    }
  } finally {
    if (child.exitCode === null && child.signalCode === null) child.kill();
    await closed;
  }
  const report = await trace;
  assert.equal(report.events.discoveryRequest.count, 1);
  const selectedRoom = revised && fixedQos;
  assert.equal(report.events.regionRequest.count, selectedRoom ? 3 : 1);
  assert.equal(report.events.qosRequest.count, expected.length * 3);
  assert.equal(report.events.startRequest.count, selectedRoom ? 8 : 0);
  assert.equal(report.events.regionReply.count, selectedRoom ? 3 : Number(revised));
  assert.equal(report.events.discoveryReply.count, Number(revised));
  if (revised) assert.deepEqual(report.replies.find(row => row.via === 'rpc').names, expected.map(row => row.name));
}

test('managed local room reads its archived menu, both modified weapons and a second life', async () => {
  const dir = fs.mkdtempSync(path.join(__dirname, '.local-spawn-check-'));
  try {
    const {prepare} = require('../profile-runtime/prepare.cjs');
    const {sources,kit} = stageRegionSources(dir);
    const home = path.join(dir,'profile-runtime'); fs.mkdirSync(home);
    const source = sources.get('local-archive-3961c933');
    const {appDir} = prepare(source,home,kit);
    await exerciseRegions(appDir,[],true,true,true,true);
  } finally {
    assert.equal(path.dirname(fs.realpathSync(dir)),fs.realpathSync(__dirname));
    fs.rmSync(dir,{recursive:true,maxRetries:4});
  }
});

test('managed solo parties, region discovery, logged-in RPC and UDP QoS on three verified sources', async t => {
  const dir = fs.mkdtempSync(path.join(__dirname, '.region-check-'));
  try {
    const { prepare, Unsupported } = require('../profile-runtime/prepare.cjs');
    const { sources, kit } = stageRegionSources(dir);
    const home = path.join(dir, 'profile-runtime');
    fs.mkdirSync(home);
    for (const [kind, source] of sources) await t.test(kind, async sub => {
      const { appDir } = prepare(source, home, kit);
      const meta = JSON.parse(fs.readFileSync(path.join(appDir, 'profile-location.json')));
      assert.equal(meta.sourceKind, kind);
      assert.equal(meta.loadoutDir, path.join(source, 'data', 'loadouts'));
      const seed = path.join(appDir, 'index.js'), patch = path.join(kit, 'region-catalog.patch');
      const qosPatch = path.join(kit, 'region-qos.patch');
      const generated = fs.readFileSync(seed, 'utf8');
      const rows = kind === 'metaserver-main-007850d0' ? publishedRegions : [{ ...publishedRegions[0], name: 'asia-east2' }];
      await exerciseRegions(appDir, rows, true);
      await sub.test('one authenticated lobby prepares, cancels, reselects and joins all three PvE cards', () =>
        exerciseRegions(appDir, rows, true, true, true));
      const git = (...args) => execFileSync('git', ['-c', 'core.autocrlf=false', ...args],
        { cwd: appDir, windowsHide: true, stdio: 'pipe' });
      git('init', '--quiet', '--template=');
      git('apply', '-R', '--check', qosPatch);
      git('apply', '-R', qosPatch);
      await exerciseRegions(appDir, rows, true, false);
      git('apply', '-R', '--check', patch);
      git('apply', '-R', patch);
      await exerciseRegions(appDir, publishedRegions.slice(0, 1), false);
      git('apply', '--check', patch);
      git('apply', patch);
      git('apply', '--check', qosPatch);
      git('apply', qosPatch);
      assert.equal(fs.readFileSync(seed, 'utf8'), generated);
      assert.equal(fs.existsSync(meta.loadoutDir), false);
    });
    await t.test('both clients of an existing two-member fixture see its original roster', async () => {
      const ids = ['fixture_captain', 'fixture_guest'];
      const members = [[ids[0], 'InHall'], [ids[1], 'InParty']];
      await checkRelease(false, {
        setupSource(source) {
          const file = path.join(source, 'index.js');
          const original = fs.readFileSync(file, 'utf8');
          const create = original.replace('PartyMembers: [TEMP_USER_ID]', 'PartyMembers: ' + JSON.stringify(ids));
          const list = JSON.stringify(members.map(([UserId, Status]) => ({ UserId, Status })));
          const roster = create.replace('[{\n          UserId: TEMP_USER_ID,\n          Status: PartyPresence\n        }]', list);
          assert.notEqual(create, original);
          assert.notEqual(roster, create);
          fs.writeFileSync(file, roster);
          const apps = () => fs.readdirSync(path.join(home, 'apps')).sort();
          const before = apps();
          assert.throws(() => prepare(source, home, kit), Unsupported);
          assert.equal(fs.readFileSync(file, 'utf8'), roster);
          assert.deepEqual(apps(), before);
        },
        async run(app, ports) {
          await withLogin(app, ports, ids[0], async (rpc, type) => {
            const captain = partyClient(rpc, type);
            assert.deepEqual((await captain.create()).PartyMembers, ids);
            await withLogin(app, ports, ids[1], async (guestRpc, guestType) => {
              assert.deepEqual(await partyClient(guestRpc, guestType).members(), members);
              assert.deepEqual(await captain.members(), members);
            });
          });
        },
      });
    });
  } finally {
    assert.equal(path.dirname(fs.realpathSync(dir)), fs.realpathSync(__dirname));
    fs.rmSync(dir, { recursive: true, maxRetries: 4 });
  }
});

test('release native menu selects a leased room on click only; client presence is separate and Query has no destination', async t => {
  let downstream = 0;
  const matcher = http.createServer((_req, res) => { downstream++; res.writeHead(503).end(); });
  matcher.listen(0, '127.0.0.1');
  await once(matcher, 'listening');
  t.after(() => new Promise(done => matcher.close(done)));
  const udp = dgram.createSocket('udp4');
  udp.bind(0, '127.0.0.1');
  await once(udp, 'listening');
  t.after(() => new Promise(done => udp.close(done)));
  const port = udp.address().port, trace = [], empty = Buffer.alloc(0), prefix = '/matchmaking.Matchmaking/';
  await checkRelease(true, { ready: true, progress: true, matcherPort: matcher.address().port, trace,
    run: async (app, ports) => {
      const table = require(path.join(app, 'game/definitions/DT_GameModeDefinition.json'));
      const plan = pvePlan(table, 'NORMAL BOT', 'CircularX');
      const file = roomFileFor(app);
      fs.writeFileSync(file, JSON.stringify({ v: 1, runId: 'udp-fixture', expiresAt: Date.now() + 14000,
        mode: 'pve', offline: true, difficulty: 'normal', map: plan.map, gameClass: plan.gameClass, port }));
      await withLogin(app, ports, fixtureAccount, async (rpc, type) => {
        const set = type('Request', 'SetPartyPresenceRequest');
        const setPresence = async (send, value) => {
          const reply = await send('/party.party/SetPresence', set.encode({ Presence: value }).finish());
          assert.equal(type('Response', 'SetPartyPresenceResponse').decode(reply).StatusCode, 0);
        };
        await setPresence(rpc, 'InHall');
        await rpc(prefix + 'QueryPlayList', empty);
        assert.equal((await rpc(prefix + 'QueryUnityMatchmaking', empty)).length, 0);
        const start = type('Request', 'StartMatchmakingRequest').encode({
          GameMode: 'NORMAL BOT', Payload: { MatchmakingRequestorUserId: fixtureAccount },
        }).finish();
        assert.equal(Buffer.from(await rpc(prefix + 'StartUnityMatchmaking', start)).toString('hex'), '0800');
        assert.equal((await rpc(prefix + 'QueryUnityMatchmaking', empty)).length, 0);
        // 同账号的新连接也没有旧选择；不能用全局 PartyPresence 证明客户端进入该房间。
        await withLogin(app, ports, fixtureAccount, async other => {
          await setPresence(other, 'InMatch');
          assert.equal((await other(prefix + 'QueryUnityMatchmaking', empty)).length, 0);
        });
        await setPresence(rpc, 'InMatching');
        await setPresence(rpc, 'InMatch');
        await setPresence(rpc, 'InMatch');
        fs.unlinkSync(file);
        assert.equal((await rpc(prefix + 'QueryUnityMatchmaking', empty)).length, 0);
        await setPresence(rpc, 'InHall');
        assert.equal((await rpc(prefix + 'StopUnityMatchmaking', empty)).length, 0);
        assert.equal((await rpc(prefix + 'QueryUnityMatchmaking', empty)).length, 0);
      });
    } });
  assert.equal(downstream, 0, 'local enqueue, status and cancel must not contact any matcher');
  const events = trace.filter(l => l.startsWith('[PVE-ROOM] ')).map(l => JSON.parse(l.slice('[PVE-ROOM] '.length)));
  assert.equal(events.length, 4);
  const choice = events[0].choice;
  assert.ok(Number.isSafeInteger(choice));
  assert.deepEqual(events, [
    { event: 'selection-ready', choice, mode: 'Rush_PVE_Normal', map: 'CircularX', serverIp: '127.0.0.1', serverPort: port },
    { event: 'client-presence', choice, presence: 'InMatching' }, { event: 'client-presence', choice, presence: 'InMatch' },
    { event: 'selection-wait', choice, mode: 'Rush_PVE_Normal', reason: 'absent' },
  ]);
  const selected = trace.findIndex(l => l.startsWith('[PVE-ROOM] '));
  const begun = trace.findIndex(l => l.startsWith('[PVE-MENU ' + choice + '] start mode=Rush_PVE_Normal'));
  assert.ok(begun >= 0 && selected > begun);
  assert.ok(trace.slice(0, selected).some(l => l.endsWith('via=empty-unbound backend=not-polled replyBytes=0')));
  for (const backend of ['found', 'queued']) assert.ok(trace.some(l => l.endsWith('via=session backend=' + backend + ' replyBytes=0')));
  assert.ok(trace.at(-1).endsWith('via=empty-unbound backend=not-polled replyBytes=0'));
  const audit = await sessions(trace);
  assert.equal(audit.attempts.length, 1);
  assert.deepEqual(audit.attempts[0].events.map(e => e.event).filter(s => s.startsWith('room ') || s.startsWith('client ')),
    ['room ready map=CircularX', 'client presence=InMatching', 'client presence=InMatch', 'room wait=absent']);
  assert.ok(!JSON.stringify(audit).includes('127.0.0.1'));
});

test('release session A/B: confirmed PvE queue, empty polls, no cross-session association, no invented reply', async t => {
  const calls = [];
  let refuseEnqueue = false;
  const matcher = http.createServer(async (req, res) => {
    let data = '';
    for await (const part of req) data += part;
    calls.push({ method: req.method, path: req.url, body: data ? JSON.parse(data) : null });
    const status = req.method === 'GET' && calls.filter(c => c.method === 'GET').length > 1 ? 'found' : 'queued';
    const refused = refuseEnqueue && req.url === '/matchmaking/enqueue';
    res.writeHead(refused ? 503 : 200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ status: refused ? 'found' : status, ticketId: 'synthetic-ticket', serverIp: '127.0.0.1', serverPort: 31003 }));
  });
  matcher.listen(0, '127.0.0.1');
  await once(matcher, 'listening');
  t.after(() => new Promise(done => matcher.close(done)));
  const prefix = '/matchmaking.Matchmaking/';
  for (const progress of [false, true]) {
    calls.length = 0;
    const trace = [];
    await checkRelease(true, { progress, ready: progress, matcherPort: matcher.address().port, trace,
      run: (app, ports) => withLogin(app, ports, fixtureAccount, async (rpc, type) => {
        const empty = Buffer.alloc(0);
        const query = 'QueryUnityMatchmaking', stop = 'StopUnityMatchmaking';
        const start = type('Request', 'StartMatchmakingRequest');
        const startReply = await rpc(prefix + 'StartUnityMatchmaking', start.encode({
          Payload: { MatchmakingRequestorUserId: fixtureAccount }, GameMode: 'NORMAL BOT',
        }).finish());
        assert.equal(Buffer.from(startReply).toString('hex'), '0800');
        assert.equal(calls[0].path, '/matchmaking/enqueue');
        assert.equal(calls[0].body.gameMode, 'NORMAL BOT');
        await withLogin(app, ports, fixtureAccount, async fresh => {
          assert.equal((await fresh(prefix + query, empty)).length, 0);
        });
        assert.equal(calls.length, 1, 'relogin on a second connection must not adopt the first ticket');
        for (let i = 0; i < 3; i++) assert.equal((await rpc(prefix + query, empty)).length, 0);
        assert.deepEqual(calls.slice(1).map(c => [c.method, c.path]), progress ?
          Array(2).fill(['GET', '/matchmaking/status/synthetic-ticket']) : []);
        assert.equal((await rpc(prefix + stop, empty)).length, 0);
        assert.ok(!calls.some(c => c.path.includes('/cancel/')));
        assert.equal((await rpc(prefix + query, empty)).length, 0);
        assert.equal(calls.length, progress ? 3 : 1);
        if (progress) {
          refuseEnqueue = true;
          await withLogin(app, ports, fixtureAccount, async fresh => {
            await fresh(prefix + 'StartUnityMatchmaking', start.encode({
              Payload: { MatchmakingRequestorUserId: fixtureAccount }, GameMode: 'Rush_PVE_Hard',
            }).finish());
            assert.equal((await fresh(prefix + query, empty)).length, 0);
          });
          assert.equal(calls.length, 4, 'a 503 is not a confirmed ticket even when its JSON claims found');
        }
      }) });
    if (!progress) { assert.deepEqual(trace, []); continue; }
    assert.ok(trace.some(line => line.endsWith('enqueue state=unavailable')));
    assert.ok(trace.some(line => /start mode=Rush_PVE_Normal field=match-string$/.test(line)));
    for (const backend of ['queued', 'found', 'cache-found']) {
      assert.ok(trace.some(line => line.endsWith('query via=session backend=' + backend + ' replyBytes=0')));
    }
    assert.ok(!trace.join('\n').includes('synthetic-ticket') && !trace.join('\n').includes(fixtureAccount));
  }
});
