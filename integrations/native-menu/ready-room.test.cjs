'use strict';

const assert = require('node:assert/strict');
const { execFileSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const net = require('node:net');
const { once } = require('node:events');
const { createInterface } = require('node:readline');
const { pvePlan } = require('./native-menu.cjs');
const { createReadyRoomSession, roomFileFor, roomReader, leaseMs } = require('./ready-room.cjs');

const zip = process.env.BOUNDARY_RELEASE_084 || path.resolve(__dirname, '../../.workflow/qa/rebound-084/Release.zip');
const member = s => execFileSync('tar', ['-xOf', zip, s], { encoding: 'utf8', windowsHide: true });
const table = JSON.parse(member('BoundaryMetaServer-main/game/definitions/DT_GameModeDefinition.json'));
const defaults = JSON.parse(member('ServerLauncher/serverconfig.json'));
const base = 1000000, owner = 'fixture', ticket = 't-1';
function receipt(extra = {}) {
  return { v: 1, runId: 'server-1', expiresAt: base + 10000, mode: 'pve', offline: true, difficulty: 'normal',
    map: 'CircularX', port: 31777, gameClass: pvePlan(table, 'NORMAL BOT', 'CircularX').gameClass, ...extra };
}
function fixture() {
  const state = { time: base, data: receipt(), reads: 0, choice: 0, events: [] };
  const session = createReadyRoomSession(() => table, () => { state.reads++; return state.data; },
    { now: () => state.time, report: e => state.events.push(e) });
  return { state, session, start: () => session.start(owner, owner, 'NORMAL BOT', ticket, ++state.choice),
    poll: () => session.status(owner, ticket) };
}

test('cold selection keeps eligible regions by membership and rejects missing metadata', async () => {
  const control = net.createServer(socket => socket.destroy());
  control.listen(0, '127.0.0.1');
  await once(control, 'listening');
  try {
    const port = control.address().port;
    const rpc = { localAddress:'127.0.0.1', remoteAddress:'127.0.0.1', localPort:port === 31001 ? 31002 : 31001, remotePort:31003 };
    const lease = { ...receipt(), autoPool:true, port:0, controlIpc:{v:1, port, nonce:'a'.repeat(64)} };
    const published = [{RegionId:'a'}, {RegionId:'b'}];
    for (const [next, expected] of [
      [{ids:['b','a','b'],published},'first'], [{ids:['a'],published},'second'],
      [undefined,'second'], [{published},'second'], [{ids:[],published},'second'],
      [{ids:['a','b'],published:[]},'second'],
    ]) {
      const session = createReadyRoomSession(() => table, () => lease, {now:() => base});
      try {
        session.start(owner, owner, 'NORMAL BOT', 'first', 1, {ids:['a','b'],published});
        session.prepare(owner, 'first', rpc, () => assert.fail('fixture never prepares a room'));
        const retry = session.start(owner, owner, 'NORMAL BOT', 'second', 2, next);
        assert.deepEqual(retry, {ticketId:expected, status:'queued'});
        assert.equal(session.issueTicket(owner, expected), null);
      } finally { session.reset(); }
    }
  } finally { await new Promise(resolve => control.close(resolve)); }
});

test('local join IPC validates the current native Query at offer and commit over TCP', { timeout: 18000 }, async t => {
  const listen = async () => {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    t.after(() => new Promise(done => server.close(done)));
    return server;
  };
  const rpcServer = await listen(), bridge = await listen();
  const incoming = once(rpcServer, 'connection');
  const lobby = net.createConnection(rpcServer.address().port, '127.0.0.1');
  const [[rpc]] = await Promise.all([incoming, once(lobby, 'connect')]);
  let connections = 0;
  bridge.on('connection', () => connections++);
  try {
    for (const scenario of ['fragmented', 'cancel', 'hall', 'new-click', 'rebind', 'expired', 'new-run',
      'changed-map', 'new-nonce', 'new-port', 'disabled', 'wrong-nonce', 'wrong-id', 'extra-field',
      'coalesced', 'oversize', 'bad-json', 'timeout']) {
      await t.test(scenario, { timeout: 2500 }, async () => {
        const { session, state } = fixture();
        const ipc = { v: 1, port: bridge.address().port, nonce: 'cf'.repeat(32) };
        state.data.joinIpc = ipc;
        const choose = () => session.start(owner, owner, 'NORMAL BOT', ticket, ++state.choice,
          { ids: ['asia'], published: [{ RegionId: 'asia' }] });
        choose();
        assert.ok(session.issueTicket(owner, ticket));
        const req = { ticketId: ticket, userId: [owner] };
        assert.equal(session.handleTicket('another', req, false, rpc), null);
        assert.equal(session.handleTicket(owner, { ...req, ticketId: 'old' }, false, rpc), null);
        const query = peer => session.handleTicket(owner, req, false, peer).assignment();
        query(null).written();
        query(rpc).written(new Error('fixture failed write'));
        const accepted = once(bridge, 'connection', { signal: AbortSignal.timeout(1500) });
        const before = connections;
        query(rpc).written();
        query(rpc).written();
        const [peer] = await accepted;
        peer.on('error', () => {});
        const lines = createInterface({ input: peer })[Symbol.asyncIterator]();
        try {
          const offer = JSON.parse((await lines.next()).value);
          assert.match(offer.requestId, /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
          assert.deepEqual(offer, { v: 1, op: 'offer', requestId: offer.requestId, runId: state.data.runId,
            gamePort: state.data.port, clientPort: lobby.localPort, rpcPort: rpcServer.address().port });
          const ready = { op: 'ready', requestId: offer.requestId, nonce: ipc.nonce };
          if (scenario === 'cancel') session.handleTicket(owner, req, true);
          if (scenario === 'hall') session.presence(owner, 'InHall');
          if (scenario === 'new-click') choose();
          if (scenario === 'rebind') session.reset();
          if (scenario === 'expired') state.time = state.data.expiresAt;
          if (scenario === 'new-run') state.data.runId = 'another-run';
          if (scenario === 'changed-map') state.data.map = 'MiniFarm';
          if (scenario === 'new-nonce') state.data.joinIpc = { ...ipc, nonce: '00'.repeat(32) };
          if (scenario === 'new-port') state.data.joinIpc = { ...ipc, port: ipc.port - 1 };
          if (scenario === 'disabled') delete state.data.joinIpc;
          if (scenario === 'wrong-nonce') ready.nonce = 'ab'.repeat(32);
          if (scenario === 'wrong-id') ready.requestId = 'other';
          if (scenario === 'extra-field') ready.host = 'example.invalid';
          let frame = JSON.stringify(ready) + '\n';
          if (scenario === 'coalesced') frame += '{}\n';
          if (scenario === 'oversize') frame = ' '.repeat(1025) + frame;
          if (scenario === 'bad-json') frame = '{\n';
          if (scenario === 'fragmented') {
            peer.write(frame.slice(0, 8));
            await new Promise(done => setTimeout(done, 10));
            peer.write(frame.slice(8, -1));
            await new Promise(done => setTimeout(done, 10));
            peer.write('\n');
            assert.deepEqual(JSON.parse((await lines.next()).value),
              { op: 'commit', requestId: offer.requestId, nonce: ipc.nonce });
            query(rpc).written();
          } else if (scenario !== 'timeout') peer.write(frame);
          const eof = await lines.next().catch(error => {
            if (error.code === 'ECONNRESET' && ['cancel', 'hall', 'new-click', 'rebind'].includes(scenario)) return { done: true };
            throw error;
          });
          assert.equal(eof.done, true);
          assert.equal(connections, before + 1);
          assert.equal(state.events.filter(e => e.event === 'join-requested').length, Number(scenario === 'fragmented'));
        } finally { peer.destroy(); session.reset(); }
      });
    }
  } finally { rpc.destroy(); lobby.destroy(); }
});

test('a lost first local offer recovers once without another native Query', { timeout: 18000 }, async t => {
  for (const scenario of ['timeout-then-ready', 'closed-twice', 'cancel', 'replaced', 'wrong-ready', 'commit-no-ack']) {
    await t.test(scenario, async () => {
      const { session, state } = fixture();
      const peers = new Set(), offers = [], commits = [];
      const rpcServer = net.createServer();
      const select = () => session.start(owner, owner, 'NORMAL BOT', ticket, ++state.choice,
        { ids: ['asia'], published: [{ RegionId: 'asia' }] });
      let signalCommit;
      const committed = new Promise(done => { signalCommit = done; });
      const bridge = net.createServer(peer => {
        peers.add(peer);
        peer.on('error', () => {});
        createInterface({ input: peer }).on('line', line => {
          const frame = JSON.parse(line);
          if (frame.op === 'commit') {
            commits.push(frame.requestId);
            peer.destroy();
            signalCommit();
            return;
          }
          offers.push(frame.requestId);
          if (scenario === 'timeout-then-ready' && offers.length === 1) return;
          if (scenario === 'closed-twice' || scenario === 'cancel' || scenario === 'replaced') {
            peer.end();
            if (scenario === 'cancel') session.stop(owner, ticket);
            if (scenario === 'replaced') state.data.runId = 'replacement';
            return;
          }
          const nonce = scenario === 'wrong-ready' ? 'ff'.repeat(32) : state.data.joinIpc.nonce;
          peer.write(JSON.stringify({ op: 'ready', requestId: frame.requestId, nonce }) + '\n');
        });
      });
      rpcServer.listen(0, '127.0.0.1'); bridge.listen(0, '127.0.0.1');
      await Promise.all([once(rpcServer, 'listening'), once(bridge, 'listening')]);
      const incoming = once(rpcServer, 'connection');
      const lobby = net.createConnection(rpcServer.address().port, '127.0.0.1');
      const [[rpc]] = await Promise.all([incoming, once(lobby, 'connect')]);
      try {
        state.data.joinIpc = { v: 1, port: bridge.address().port, nonce: 'af'.repeat(32) };
        select(); assert.ok(session.issueTicket(owner, ticket));
        const query = () => session.handleTicket(owner, { ticketId: ticket, userId: [owner] }, false, rpc).assignment().written();
        query();
        const delay = ms => new Promise(done => setTimeout(done, ms));
        if (scenario === 'timeout-then-ready' || scenario === 'commit-no-ack') {
          let timeout;
          try {
            await Promise.race([committed, new Promise((_, fail) => {
              timeout = setTimeout(() => fail(new Error('no commit for current native Query')), 3200);
            })]);
          } finally { clearTimeout(timeout); }
          query();
        }
        await delay(550);
        const expected = ['timeout-then-ready', 'closed-twice'].includes(scenario) ? 2 : 1;
        assert.equal(offers.length, expected);
        assert.equal(new Set(offers).size, expected);
        assert.deepEqual(commits, ['timeout-then-ready', 'commit-no-ack'].includes(scenario) ? [offers.at(-1)] : []);
      } finally {
        session.reset(); lobby.destroy(); rpc.destroy(); for (const peer of peers) peer.destroy();
        await Promise.all([rpcServer, bridge].map(s => new Promise(done => s.close(done))));
      }
    });
  }
});

test('retail match-finished callback has no endpoint parameter; published SetPresence is an incoming string', () => {
  const ui = process.env.BOUNDARY_MATCH_FINISHED_UI ||
    'C:/BrtWork/Assets/ui_export_2026-10-19/raw/ProjectBoundary/Content/UI/MainMenu/Message/UMG_Message_MatchmakingFinished.json';
  const rows = JSON.parse(fs.readFileSync(ui, 'utf8').replace(/^\uFEFF/, ''));
  const join = rows.find(entry => entry.Type === 'Function' && entry.Name === 'On Player Join Game');
  assert.ok(join);
  assert.deepEqual(join.ChildProperties || [], []);
  assert.ok(rows.some(entry => entry.ChildProperties?.some(p => p.Name === 'CallFunc_HasLobbyStarted_ReturnValue')));
  const snapshot = process.env.BOUNDARY_META_SNAPSHOT || 'C:/BrtWork/SDK/sdk-fixture/validation-server';
  const proto = require(path.join(snapshot, 'node_modules/protobufjs'));
  const type = proto.parse(member('BoundaryMetaServer-main/game/proto/Request/SetPartyPresenceRequest.proto'))
    .root.lookupType('ProjectBoundary.SetPartyPresenceRequest');
  assert.deepEqual(type.fieldsArray.map(f => [f.name, f.id, f.type]), [['Presence', 1, 'string']]);
});

test('published defaults do not describe any NORMAL BOT Warehouse assignment; valid table rows and classes are accepted', () => {
  assert.deepEqual([defaults.mode, defaults.difficulty, defaults.map, defaults.offline], ['pve', 'normal', 'Warehouse', true]);
  assert.throws(() => pvePlan(table, 'NORMAL BOT', defaults.map), /Map outside Rush_PVE_Normal/);
  const { state, session, start } = fixture();
  state.data = receipt({ ...defaults, gameClass: receipt().gameClass });
  assert.equal(start().status, 'queued');
  assert.deepEqual(state.events, [{ event: 'selection-wait', choice: 1, mode: 'Rush_PVE_Normal', reason: 'mode-map' }]);
  for (const [diff, mode, map] of [['easy', 'EASY BOT', 'Warehouse'], ['normal', 'Rush_PVE_Normal', 'MiniFarm'], ['hard', 'HARD BOT', 'OSS']]) {
    const plan = pvePlan(table, mode, map);
    state.data = receipt({ difficulty: diff, map, gameClass: plan.gameClass });
    const result = session.start(owner, owner, mode, ticket, ++state.choice);
    assert.deepEqual(result, { ticketId: ticket, status: 'found', serverIp: '127.0.0.1', serverPort: 31777 });
  }
});

test('a bound native selection is required; missing, malformed, mismatched and untrusted room data cannot allocate', () => {
  const { state, session, start, poll } = fixture();
  session.presence(owner, 'InMatch');
  assert.equal(poll(), null);
  for (const args of [['', '', 'NORMAL BOT', ticket], [owner, 'other', 'NORMAL BOT', ticket],
    [owner, owner, 'Purge', ticket], [owner, owner, 'normal bot', ticket], [owner, owner, 'NORMAL BOT', '']]) {
    assert.equal(session.start(...args, 1), null);
  }
  assert.equal(state.reads, 0);
  state.data = undefined;
  assert.equal(start(), null);
  state.data = null;
  assert.equal(start().status, 'queued');
  for (const bad of [{ v: 2 }, { runId: '' }, { mode: 'pvp' }, { offline: false }, { difficulty: 'hard' },
    { map: 'Warehouse' }, { gameClass: '/Game/Unknown' }, { port: 0 }, { port: 65536 }, { port: '31777' },
    ...[6969, 8000, 9000, 9001].map(port => ({ port })), { expiresAt: base }, { expiresAt: base + leaseMs + 1 }]) {
    state.data = receipt(bad);
    assert.equal(start().status, 'queued', JSON.stringify(bad));
  }
  assert.ok(state.events.length && state.events.every(e => e.event === 'selection-wait'));
  state.data = receipt({ serverIp: '192.0.2.1' });
  assert.equal(start().serverIp, '127.0.0.1');
});

test('recheck on poll, preserve generation and address, cancel or rebind without transferring a choice', () => {
  const { state, session, start, poll } = fixture();
  assert.equal(start().status, 'found');
  assert.equal(session.status('other', ticket), null);
  assert.equal(session.status(owner, 'other'), null);
  assert.equal(session.stop('other', ticket), false);
  assert.equal(session.stop(owner, 'other'), false);
  state.time += 10000;
  assert.equal(poll().status, 'queued');
  session.presence(owner, 'InMatch');
  for (const changed of [{ port: 31778 }, { map: 'MiniFarm' }, { runId: 'server-2' }]) {
    state.data = receipt({ expiresAt: state.time + 5000, ...changed });
    assert.equal(poll().status, 'queued');
  }
  state.data = receipt({ expiresAt: state.time + 5000 });
  assert.equal(poll().status, 'found');
  session.presence('other', 'InMatch');
  session.presence(owner, 'unknown');
  session.presence(owner, 'InMatching');
  session.presence(owner, 'InMatching');
  session.presence(owner, 'InMatch');
  assert.deepEqual(state.events.filter(e => e.event !== 'selection-wait').map(e => e.presence || e.event),
    ['selection-ready', 'selection-ready', 'InMatching', 'InMatch']);
  assert.equal(new Set(state.events.map(e => e.choice)).size, 1);
  assert.equal(session.stop(owner, ''), true);
  assert.equal(poll(), null);
  state.data.runId = 'server-2';
  assert.equal(start().status, 'found');
  session.reset();
  assert.equal(poll(), null);
  assert.notEqual(state.events[0].choice, state.events.at(-1).choice);
});

test('opt-in sidecar is local to the managed app hash, bounded, and not in a player or game directory', () => {
  const temp = fs.mkdtempSync(path.join(__dirname, '.ready-reader-'));
  try {
    const dir = path.join(temp, 'profile-runtime'), app = path.join(dir, 'apps', 'a'.repeat(64));
    fs.mkdirSync(app, { recursive: true });
    assert.equal(roomFileFor(path.join(temp, 'MetaServer')), null);
    assert.equal(roomFileFor(path.join(dir, 'apps', 'short')), null);
    assert.equal(roomFileFor(path.join('profile-runtime', 'apps', 'a'.repeat(64))), null);
    const file = roomFileFor(app), read = roomReader(app);
    assert.equal(file, path.join(dir, 'pve-ready-' + 'a'.repeat(64) + '.json'));
    assert.equal(read(), undefined);
    fs.writeFileSync(file, '{');
    assert.equal(read(), null);
    fs.writeFileSync(file, ' '.repeat(4097));
    assert.equal(read(), null);
    fs.writeFileSync(file, JSON.stringify(receipt()));
    assert.deepEqual(read(), receipt());
    if (process.platform === 'win32') assert.deepEqual(roomReader(path.toNamespacedPath(app))(), receipt());
    assert.equal(roomReader(path.join(dir, 'apps', 'b'.repeat(64)))(), undefined);
  } finally {
    assert.equal(path.dirname(fs.realpathSync(temp)), fs.realpathSync(__dirname));
    fs.rmSync(temp, { recursive: true, maxRetries: 4 });
  }
});
