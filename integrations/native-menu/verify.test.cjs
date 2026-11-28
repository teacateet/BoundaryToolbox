'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const net = require('node:net');
const { once } = require('node:events');
const { setTimeout: delay } = require('node:timers/promises');
const { createHash } = require('node:crypto');
const { execFileSync, fork } = require('node:child_process');
const { modesFrom, withPvePlaylists, pvePlan, resolveTutorial, tutorialWorld,
  packageNames, tutorialKeys, managedRegionCatalog, regionReplySummary } = require('./native-menu.cjs');

const source = process.env.BOUNDARY_META_SNAPSHOT ||
  'C:/BrtWork/SDK/sdk-fixture/validation-server';
const content = process.env.BOUNDARY_MENU_EXPORTS ||
  'C:/BrtWork/Assets/exports/ui-current/properties/ProjectBoundary/Content/UI/MainMenu';
const manifest = fs.readFileSync(process.env.BOUNDARY_PACKAGE_LIST ||
  'C:/BrtWork/Assets/reports/all-source-paths.txt', 'utf8');
const localWorlds = packageNames(manifest);
const modeFile = 'game/definitions/DT_GameModeDefinition.json';
const table = require(path.resolve(source, modeFile));
const protobuf = require(path.resolve(source, 'node_modules/protobufjs'));
const prefix = '/matchmaking.Matchmaking/';
const schema = {
  RequestWrapper: 'Request/RequestWrapper', ResponseWrapper: 'Response/ResponseWrapper',
  QueryPlaylistResponse: 'Response/QueryPlaylistResponse',
  QueryMatchmakingRegionResponse: 'Response/QueryMatchmakingRegionResponse',
  StartMatchmakingRequest: 'Request/StartMatchmakingRequest',
  StartMatchmakingResponse: 'Response/StartMatchmakingResponse',
  QueryUnityMatchmakingReq: 'Response/matchmaking_ext', QueryUnityMatchmakingRes: 'Response/matchmaking_ext',
  StopUnityMatchmakingReq: 'Response/matchmaking_ext', StopUnityMatchmakingRes: 'Response/matchmaking_ext',
};
function type(name) {
  return protobuf.loadSync(path.join(source, 'game/proto', schema[name] + '.proto'))
    .lookupType('ProjectBoundary.' + name);
}
function encode(name, obj) {
  const message = type(name);
  return message.encode(message.create(obj)).finish();
}

test('original cards, component click delegates and tutorial visibility', () => {
  const asset = name => JSON.parse(fs.readFileSync(path.join(content, name + '.json'), 'utf8'));
  const menu = asset('UMG_MainMenu');
  const card = name => menu.find(item => item.Name === name && item.Type === 'UMG_MainMenuPlayCard_C').Properties;
  assert.equal(card('PVE').ParentNodeName, 'ASSIGNMENT');
  assert.equal(card('Tutorial').ParentNodeName, 'TUTORAIL');
  assert.equal(card('PVE').PlayListType, 'EPBPlayListType::PVE');
  assert.match(card('PVE').PageClass.ObjectPath, /\/UMG_Assignment\.\d+$/);
  assert.match(card('Tutorial').PageClass.ObjectPath, /\/UMG_Tutorial\.\d+$/);
  const bindings = objects => objects.flatMap(obj => obj.Properties?.ComponentDelegateBindings || []);
  assert.ok(bindings(menu).some(b => b.ComponentPropertyName === 'PVE' && b.DelegatePropertyName === 'StartMatch'));
  const commonCard = asset('Universal/components/UMG_MainMenuPlayCard');
  assert.ok(bindings(commonCard).some(b => b.ComponentPropertyName === 'btn' &&
    b.DelegatePropertyName === 'OnClicked' && b.FunctionNameToBind.includes('OnButtonClickedEvent')));
  assert.ok(commonCard.some(obj => obj.ChildProperties?.some(p => p.Name === 'CallFunc_GotoPage_ReturnValue')));
  assert.ok(bindings(asset('Tutorial/UMG_TutorialDisplayMode')).some(b => b.DelegatePropertyName === 'Btn_Clicked'));
  const entries = asset('UMG_Tutorial').filter(item => item.Type === 'UMG_TutorialDisplayMode_C');
  assert.deepEqual(entries.map(item => item.Properties.TutorialLevelName), tutorialKeys);
  assert.deepEqual(entries.filter(item => item.Properties.Visibility === 'ESlateVisibility::Collapsed')
    .map(item => item.Properties.TutorialLevelName), ['Simulation']);
});

test('all seven allowed mode/map pairs have a persistent map, class and difficulty sublevel', () => {
  const modes = modesFrom(table);
  assert.deepEqual(modes.map(mode => mode.maps), [['warehouse', 'DataCenter'], ['MiniFarm', 'CircularX', 'OSS'], ['OSS', 'CircularX']]);
  for (const mode of modes) {
    for (const row of mode.maps) {
      const plan = pvePlan(table, mode.matchName, row);
      assert.ok(localWorlds.has(plan.world));
      const classPackage = plan.gameClass.split('.')[0].replace('/Game/', 'ProjectBoundary/Content/');
      assert.ok(manifest.split(/\r?\n/).includes(classPackage + '.uasset'));
      assert.ok([...localWorlds].some(world => world.startsWith(plan.world.slice(0, plan.world.lastIndexOf('/') + 1))
        && world.toLowerCase().endsWith('_mode_' + mode.id.toLowerCase())));
      assert.deepEqual(plan.argv, [plan.world + '?game=' + plan.gameClass]);
    }
  }
  assert.throws(() => pvePlan(table, 'NORMAL BOT', 'Warehouse'), /outside/);
  assert.throws(() => pvePlan(table, 'Purge', 'OSS'), /Unknown/);
});

test('retail available-region dialog, selected-region list and QoS widgets have distinct input types', () => {
  const ui = process.env.BOUNDARY_UI_PROPERTIES || path.resolve(content, '..');
  const read = name => JSON.parse(fs.readFileSync(path.join(ui, 'Menu_V2/Widgets/ConfirmPage', name + '.json'), 'utf8'));
  const graph = (items, name) => items.find(x => x.Name === 'ExecuteUbergraph_' + name).ChildProperties;
  const dialogName = 'UMG_MatchmakingRegions', selectedName = 'UMG_MatchmakingRegionList';
  const dialog = read(dialogName), selected = read(selectedName), row = read('UMG_MatchmakingRegion');
  const assignment = JSON.parse(fs.readFileSync(path.join(content, 'UMG_Assignment.json'), 'utf8'));
  assert.ok(assignment.some(widget => widget.Type === selectedName + '_C' && widget.Name === selectedName));
  const fields = new Map(graph(dialog, dialogName).map(field => [field.Name, field]));
  assert.equal(fields.get('CallFunc_Array_Get_Item').Struct.ObjectName, "ScriptStruct'PBMatchmakingRegionInfo'");
  assert.equal(fields.get('CallFunc_GetDataTableRowFromName_OutRow').Struct.ObjectName, "ScriptStruct'PBMatchmakingRegionAddressRow'");
  assert.equal(fields.get('CallFunc_Create_ReturnValue').PropertyClass.ObjectName, "WidgetBlueprintGeneratedClass'UMG_MatchmakingRegion_C'");
  assert.equal(fields.get('CallFunc_AddChildToUniformGrid_ReturnValue').PropertyClass.ObjectName, "Class'UniformGridSlot'");
  assert.ok(dialog.some(field => field.Name === 'K2_RefreshMatchmakingRegions'));
  assert.ok(selected.some(field => field.Name === 'K2_RefreshRegionsForMatchmaking'));
  assert.equal(graph(selected, selectedName).find(field => field.Name === 'CallFunc_Array_Get_Item').Type, 'StrProperty');
  for (const field of ['K2_RefreshQosData', 'QueryPingState', 'TextRegionName', 'TextRegionAddress']) {
    assert.ok(row.some(object => object.Name === field), field);
  }
  const files = new Set(manifest.split(/\r?\n/));
  for (const ext of ['uasset', 'uexp']) {
    assert.ok(files.has('ProjectBoundary/Content/GameData/CSVConfig/Matchmaking/DT_MatchmakingRegionAddress.' + ext));
  }
});

test('unidentified and customized discovery keep their existing data and diagnostic names are allowlisted', () => {
  const dir = fs.mkdtempSync(path.join(__dirname, '.region-metadata-'));
  try {
    const row = Object.freeze({ location_id: 6, region_id: '336d1f3e-3ecb-11eb-a7dc-3b7705f20f56',
      ipv4: '127.0.0.1', ipv6: '', port: 49172 });
    const discovery = Object.freeze({ servers: Object.freeze([row]) });
    const metadata = path.join(dir, 'profile-location.json');
    assert.equal(managedRegionCatalog(dir, discovery), null);
    fs.writeFileSync(metadata, '{"sourceKind":"custom"}');
    assert.equal(managedRegionCatalog(dir, discovery), null);
    fs.writeFileSync(metadata, '{"sourceKind":"metaserver-main-007850d0"}');
    for (const servers of [[], [row, row], [null], [{ ...row, ipv4: '192.0.2.1' }],
      [{ ...row, location_id: 10 }], [{ ...row, region_id: '' }], [{ ...row, port: 0 }]]) {
      assert.equal(managedRegionCatalog(dir, { servers }), null);
    }
    const restored = managedRegionCatalog(dir, discovery);
    assert.equal(discovery.servers.length, 1);
    assert.equal(restored.discovery.servers[1].port, row.port);
    assert.equal(regionReplySummary([]), '[PVE-REGIONS] rpc rows=0 ids=0 names=none');
    assert.equal(regionReplySummary([{ RegionId: 'private-id', RegionName: 'private-name' }, {}]),
      '[PVE-REGIONS] rpc rows=2 ids=1 names=other,missing');
  } finally {
    assert.equal(path.dirname(fs.realpathSync(dir)), fs.realpathSync(__dirname));
    fs.rmSync(dir, { recursive: true });
  }
});

test('merging preserves other playlists, existing PvE, and repeated deployment', () => {
  const base = { PVP: [{ Name: 'Purge' }], Event: [], PVE: [{ Name: 'custom', GameModeList: ['NORMAL BOT'] }] };
  const before = JSON.stringify(base);
  const merged = withPvePlaylists(base, table);
  assert.equal(JSON.stringify(base), before);
  assert.equal(merged.PVP, base.PVP);
  assert.equal(merged.PVE[0], base.PVE[0]);
  assert.deepEqual(merged.PVE.slice(1).map(entry => entry.GameModeList), [['Rush_PVE_Easy'], ['Rush_PVE_Hard']]);
  assert.equal(withPvePlaylists(merged, table), merged);
  assert.throws(() => withPvePlaylists({ PVE: [{ Name: 'Rush_PVE_Normal' }] }, table), /collision/);
});

test('a local lease keeps the original row first and preserves community entries and fallback', () => {
  const custom = { Name: 'community', GameModeList: ['HARD BOT', 'Purge'], Priority: 100 };
  const defaults = withPvePlaylists({ PVP: [{ Name: 'Playtest' }], Event: [] }, table);
  const base = { ...defaults, PVE: [custom, ...defaults.PVE] };
  const before = JSON.stringify(base);
  const plan = pvePlan(table, 'HARD BOT', 'OSS');
  const lease = { v: 1, runId: 'room-1', expiresAt: Date.now() + 14000, mode: 'pve', offline: true,
    port: 31777, difficulty: 'hard', map: 'OSS', gameClass: plan.gameClass };
  for (const data of [undefined, null, { ...lease, expiresAt: 0 }, { ...lease, mode: 'pvp' },
    { ...lease, offline: false }, { ...lease, port: 9001 }, { ...lease, map: 'Warehouse' },
    { ...lease, gameClass: 'other' }, { ...lease, difficulty: 'unknown' }]) {
    assert.equal(withPvePlaylists(base, table, () => data), base);
  }
  const active = withPvePlaylists(base, table, () => lease);
  assert.deepEqual(active.PVE, [defaults.PVE[2], custom]);
  assert.equal(active.PVE[0], defaults.PVE[2]);
  assert.equal(active.PVE[1], custom);
  assert.equal(active.PVP, base.PVP);
  assert.equal(active.Event, base.Event);
  assert.deepEqual(withPvePlaylists({ ...base, PVE: [custom] }, table, () => lease), active);
  assert.equal(JSON.stringify(base), before);
});

test('next-match cards are local, idempotent and restricted to seven retail assignments', () => {
  const base = {PVE:[], PVP:[{Name:'QuickPlay'}]};
  const lease = {v:1, runId:'local', autoPool:true, nextMatch:1, mode:'pve', offline:true,
    expiresAt:Date.now()+12000, controlIpc:{v:1,port:31611,nonce:'0'.repeat(64)}};
  const catalog = withPvePlaylists(base, table, () => lease);
  assert.deepEqual(withPvePlaylists(catalog, table, () => lease), catalog);
  assert.equal(catalog.PVE.length, 13);
  assert.equal(catalog.PVP, base.PVP);
  const all = require('./native-menu.cjs').nextCards(table)[0];
  assert.equal(all.id, 'BRT_NEXT_Any');
  assert.equal(new Set(all.pairs.map(p => p.mode + '/' + p.map)).size, 7);
  for (const delta of [{nextMatch:0}, {offline:false}, {mode:'pvp'}, {expiresAt:0}, {controlIpc:{v:1,port:8000}}])
    assert.ok(!withPvePlaylists(base,table,() => ({...lease, ...delta})).PVE.some(r => r.Name.startsWith('BRT_NEXT_')));
});

test('tutorial keys need an explicit mapping; direct worlds must exist in the retail list', () => {
  for (const key of tutorialKeys) {
    assert.deepEqual(resolveTutorial(key, [], localWorlds), { key, defaultCollapsed: key === 'Simulation',
      status: 'level-mapping-required', argv: [] });
  }
  const name = '/Game/Maps/EA/Tutorial/Tutorial_Shoot';
  const fixture = [{ Name: 'Default__PBLocalPlayer_BP_C', Properties: {
    TutorialLevels: [{ Key: 'Weapons', Value: { AssetPathName: name + '.Tutorial_Shoot' } }] } }];
  assert.deepEqual(resolveTutorial('Weapons', fixture, localWorlds).argv, [name]);
  assert.throws(() => resolveTutorial('Weapons', fixture, new Set()), /missing/);
  for (const world of ['Tutorial_Movement', 'Tutorial_Shoot', 'Tutorial_Combat', 'Tutorial_Radar']) {
    assert.deepEqual(tutorialWorld(world, localWorlds).argv, ['/Game/Maps/EA/Tutorial/' + world]);
  }
  assert.throws(() => tutorialWorld('Tutorial_MODE_MovementA', localWorlds), /Unknown/);
});

function stage(dir, patched, matcherPort) {
  const files = ['index.js', 'game/definitionIndex.js', 'game/loadoutStore.js', modeFile,
    ...new Set(Object.values(schema).map(file => 'game/proto/' + file + '.proto'))];
  for (const file of files) {
    fs.mkdirSync(path.dirname(path.join(dir, file)), { recursive: true });
    fs.copyFileSync(path.join(source, file), path.join(dir, file));
  }
  const index = path.join(dir, 'index.js');
  const bytes = fs.readFileSync(index);
  assert.equal(createHash('sha256').update(bytes).digest('hex'),
    '3961c933f8a3396fb01d9bb7e190db2312ce5a4c2320ea78348aed84df5ce10d');
  fs.writeFileSync(index, bytes.toString('utf8').replace(/\r\n/g, '\n'));
  if (patched) {
    fs.copyFileSync(path.join(__dirname, 'native-menu.cjs'), path.join(dir, 'native-menu.cjs'));
    const git = (...args) => execFileSync('git', ['apply', ...args, path.join(__dirname, 'playlist.patch')],
      { cwd: dir, env: { ...process.env, GIT_CEILING_DIRECTORIES: __dirname }, windowsHide: true });
    git('--check');
    git();
    git('--reverse', '--check');
  }
  let code = fs.readFileSync(index, 'utf8');
  const management = 'const MATCHSERVER_API = `http://${MatchmakingHost}:9001`;';
  const bootstrap = 'app.listen(process.env.PORT || 8000, () => {';
  assert.equal(code.split(management).length, 2);
  assert.equal(code.split(bootstrap).length, 2);
  code = code.replace(management, 'const MATCHSERVER_API = "http://127.0.0.1:' + matcherPort + '";');
  // 仅在测试副本启动原 TCP 路由，端口由系统分配。
  fs.writeFileSync(index, code.slice(0, code.indexOf(bootstrap)) +
    "server.listen(0, '127.0.0.1', () => process.send(server.address().port));\n");
}

async function withService(patched, matcherPort, run, omitModeTable = false) {
  const dir = fs.mkdtempSync(path.join(__dirname, '.native-replay-'));
  let child;
  let exited;
  try {
    stage(dir, patched, matcherPort);
    if (omitModeTable) fs.unlinkSync(path.join(dir, modeFile));
    child = fork(path.join(dir, 'index.js'), [], { cwd: dir, execArgv: [], windowsHide: true,
      env: { ...process.env, NODE_PATH: path.resolve(source, 'node_modules'), NODE_OPTIONS: '' },
      stdio: ['ignore', 'ignore', 'pipe', 'ipc'] });
    let errors = '';
    child.stderr.on('data', data => { errors += data; });
    exited = once(child, 'exit');
    const [port] = await Promise.race([once(child, 'message', { signal: AbortSignal.timeout(7000) }),
      exited.then(() => { throw new Error('Isolated RPC exited: ' + errors); })]);
    await withRpcClient(port, run);
    assert.equal(fs.existsSync(path.join(dir, 'data')), false);
  } finally {
    if (child && child.exitCode === null && child.signalCode === null) child.kill();
    if (exited) await exited;
    assert.equal(path.dirname(fs.realpathSync(dir)), fs.realpathSync(__dirname));
    fs.rmSync(dir, { recursive: true, maxRetries: 3 });
  }
}

async function withRpcClient(port, run) {
  const socket = net.createConnection({ host: '127.0.0.1', port });
  socket.setTimeout(4000, () => socket.destroy(new Error('RPC read timeout')));
  try {
    await once(socket, 'connect');
    const chunks = socket[Symbol.asyncIterator]();
    let pending = Buffer.alloc(0);
    await run(async (id, route, responseType, requestType, obj) => {
      const rpcPath = prefix + route;
      const envelope = encode('RequestWrapper', { MessageId: id, RPCPath: rpcPath,
        Message: requestType ? encode(requestType, obj) : Buffer.alloc(0) });
      const packet = Buffer.alloc(4 + envelope.length);
      packet.writeUInt32BE(envelope.length);
      packet.set(envelope, 4);
      socket.write(packet);
      while (pending.length < 4 || pending.length < 4 + pending.readUInt32BE()) {
        const { value, done } = await chunks.next();
        if (done) throw new Error('Missing RPC response: ' + route);
        pending = Buffer.concat([pending, value]);
      }
      const end = 4 + pending.readUInt32BE();
      const wrapper = type('ResponseWrapper').decode(pending.subarray(4, end));
      pending = pending.subarray(end);
      assert.equal(wrapper.MessageId, id);
      assert.equal(wrapper.RPCPath, rpcPath);
      assert.equal(wrapper.ErrorCode, 0);
      return { message: type(responseType).decode(wrapper.Message), hex: Buffer.from(wrapper.Message).toString('hex') };
    });
    await delay(120);
    assert.equal(pending.length + socket.readableLength, 0, 'Unexpected trailing RPC data');
  } finally {
    socket.destroy();
  }
}

test('real staged TCP routes against owned loopback matcher', { timeout: 25000 }, async t => {
  const calls = [];
  let status = 'queued';
  const matcher = http.createServer(async (req, res) => {
    let body = '';
    for await (const chunk of req) body += chunk;
    calls.push({ method: req.method, path: req.url, body: body && JSON.parse(body) });
    res.setHeader('Content-Type', 'application/json');
    const result = { status, serverIp: '127.0.0.1', serverPort: 31415 };
    res.end(JSON.stringify(result));
  });
  matcher.listen(0, '127.0.0.1');
  await once(matcher, 'listening');
  t.after(() => new Promise(resolve => matcher.close(resolve)));
  let baselinePvp;
  const regionId = '336d1f3e-3ecb-11eb-a7dc-3b7705f20f56';
  for (const patched of [false, true]) {
    await withService(patched, matcher.address().port, async rpc => {
      const playlists = await rpc(11, 'QueryPlayList', 'QueryPlaylistResponse');
      assert.equal(playlists.message.StatusCode, 0);
      const catalog = JSON.parse(playlists.message.PlaylistsJSON);
      if (patched) {
        assert.deepEqual(catalog.PVP, baselinePvp);
        assert.deepEqual(catalog.PVE.map(item => item.GameModeList), modesFrom(table).map(m => [m.id]));
      } else {
        baselinePvp = catalog.PVP;
        assert.deepEqual(baselinePvp[0].GameModeList, ['Purge']);
        assert.equal(catalog.PVE, undefined);
      }
      const regions = await rpc(12, 'QueryUnityMatchmakingRegion', 'QueryMatchmakingRegionResponse');
      assert.equal(regions.message.Regions[0].RegionId, patched ? regionId : '');
      if (!patched) return;
      for (const mode of ['Rush_PVE_Easy', 'NORMAL BOT', 'Rush_PVE_Hard', 'Purge', undefined]) {
        const player = 'synthetic-menu-player';
        status = 'queued';
        const start = await rpc(13, 'StartUnityMatchmaking', 'StartMatchmakingResponse', 'StartMatchmakingRequest',
          { Payload: { MatchmakingRequestorUserId: player, UnknownMessage: [{ RegionId: regionId }] }, GameMode: mode });
        const { path: posted, method, body } = calls.at(-1);
        assert.equal(posted, '/matchmaking/enqueue');
        assert.equal(method, 'POST');
        assert.deepEqual([body.userId, body.gameMode, body.regionIds], [player, mode ?? 'Purge', [regionId]]);
        assert.match(body.ticketId, /^[0-9a-f-]{36}$/);
        assert.equal(start.hex, '0800');
        const query = { ticketId: body.ticketId, userId: player };
        const queued = await rpc(14, 'QueryUnityMatchmaking', 'QueryUnityMatchmakingRes', 'QueryUnityMatchmakingReq', query);
        assert.equal(queued.hex, '');
        status = 'found';
        const found = await rpc(15, 'QueryUnityMatchmaking', 'QueryUnityMatchmakingRes', 'QueryUnityMatchmakingReq', query);
        assert.equal(calls.at(-1).path, '/matchmaking/status/' + body.ticketId);
        assert.equal(calls.at(-1).method, 'GET');
        assert.equal(found.hex, '');
        const stop = await rpc(16, 'StopUnityMatchmaking', 'StopUnityMatchmakingRes', 'StopUnityMatchmakingReq', query);
        assert.equal(stop.hex, '');
        assert.equal(calls.at(-1).path, '/matchmaking/cancel/' + body.ticketId);
        assert.equal(calls.at(-1).method, 'POST');
      }
      status = 'found';
      const immediate = await rpc(20, 'StartUnityMatchmaking', 'StartMatchmakingResponse', 'StartMatchmakingRequest',
        { Payload: { MatchmakingRequestorUserId: 'synthetic-immediate' }, GameMode: 'Rush_PVE_Normal' });
      const ticketId = calls.at(-1).body.ticketId;
      const afterStart = calls.length;
      assert.equal(immediate.hex, '0800');
      for (const query of [{}, { ticketId }]) {
        const reply = await rpc(21, 'QueryUnityMatchmaking', 'QueryUnityMatchmakingRes', 'QueryUnityMatchmakingReq', query);
        assert.equal(reply.hex, '');
      }
      assert.equal(calls.length, afterStart);
    });
  }
  await withService(true, matcher.address().port, async rpc => {
    const playlists = await rpc(30, 'QueryPlayList', 'QueryPlaylistResponse');
    assert.deepEqual(JSON.parse(playlists.message.PlaylistsJSON), { PVP: baselinePvp });
  }, true);
});
