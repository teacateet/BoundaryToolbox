'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const vm = require('node:vm');
const { execFileSync } = require('node:child_process');
const { regionTrace } = require('./menu-trace.cjs');
const { readAddressTable, compareKeys } = require('./region-keys.cjs');

const ui = process.env.BOUNDARY_UI_PROPERTIES ||
  'C:/BrtWork/Assets/exports/ui-current/properties/ProjectBoundary/Content/UI';
const confirm = 'Menu_V2/Widgets/ConfirmPage/';
const serviceRoot = process.env.BOUNDARY_META_SNAPSHOT || 'C:/BrtWork/SDK/sdk-fixture/validation-server';
function historicalSource() {
  const bytes = fs.readFileSync(path.join(serviceRoot, 'index.js'));
  assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), '3961c933f8a3396fb01d9bb7e190db2312ce5a4c2320ea78348aed84df5ce10d');
  return bytes.toString('utf8').replaceAll('\r\n', '\n');
}
const responseType = name => require(path.join(serviceRoot, 'node_modules/protobufjs'))
  .loadSync(path.join(serviceRoot, 'game/proto/Response', name + '.proto')).lookupType('ProjectBoundary.' + name);
const widget = name => JSON.parse(fs.readFileSync(path.join(ui, name + '.json'), 'utf8'));
const bindings = objects => objects.flatMap(o => o.Properties?.ComponentDelegateBindings ?? []);
const hasEvent = (objects, component, event) => bindings(objects).some(b =>
  b.ComponentPropertyName === component && b.DelegatePropertyName === event && objects.some(o => o.Name === b.FunctionNameToBind));
const member = (objects, owner, name) => objects.find(o => o.Name === owner)?.ChildProperties?.find(p => p.Name === name);
const objectName = ref => ref?.ObjectName?.match(/\.([\w]+)'$/)?.[1];

function owningButton(objects, label) {
  const byName = new Map(objects.map(o => [o.Name, o]));
  let element = objects.find(o => o.Properties?.Text?.SourceString === label);
  const seen = new Set();
  while (element && !seen.has(element)) {
    if (element.Type === 'Button') return element.Name;
    seen.add(element);
    const slot = byName.get(objectName(element.Properties?.Slot));
    element = byName.get(objectName(slot?.Properties?.Parent));
  }
  throw new Error('Missing retail button for ' + label);
}

test('retail playlist click, party gate and region selector release are separate inputs', () => {
  const playlist = widget('MainMenu/Assignment/UMG_PlaylistItem');
  assert.ok(hasEvent(playlist, 'UMG_STD_Art_ImageBox_6', 'Btn_Clicked'));
  assert.equal(member(playlist, 'ExecuteUbergraph_UMG_PlaylistItem', 'CallFunc_IsMatchmakingNeedsRegionInfo_ReturnValue').Type, 'BoolProperty');
  const summary = widget(confirm + 'UMG_MatchmakingRegionList');
  assert.ok(hasEvent(summary, 'ButtonMain', 'OnReleased'));
  for (const fn of ['IsPartyLeader', 'IsInParty']) assert.equal(member(summary, 'OnActive', 'CallFunc_' + fn + '_ReturnValue').Type, 'BoolProperty');
  for (const fn of ['K2_BecomePartyLeader', 'K2_BecomePartyMember', 'SetMemberDisplay', 'SetLeaderDisplay']) assert.ok(summary.some(o => o.Name === fn));
  assert.equal(summary.find(o => o.Type === 'TextBlock' && o.Name === 'TextMember').Properties.Text.SourceString, 'Only the captain can choose the region');
  assert.equal(summary.find(o => o.Type === 'TextBlock' && o.Name === 'TextRegionList').Properties.Text.SourceString, 'Default Area');
});

test('retail TERRITORY actions, three design slots and per-row FName selection are verifiable without a client', () => {
  const dialog = widget(confirm + 'UMG_MatchmakingRegions');
  assert.ok(dialog.some(o => o.Properties?.Text?.SourceString === 'TERRITORY'));
  for (const [label, button, event] of [['REFRESH', 'Button_371', 'OnReleased'], ['CONFIRM', 'Button_661', 'OnReleased'],
    ['CANCEL', 'Button_836', 'OnClicked']]) {
    assert.equal(owningButton(dialog, label), button);
    assert.ok(hasEvent(dialog, button, event));
  }
  const grid = dialog.find(o => o.Name === 'GridMatchmakingRegions' && o.Type === 'UniformGridPanel');
  assert.equal(grid.Properties.Slots.length, 3);
  for (const ref of grid.Properties.Slots) {
    const slot = dialog.find(o => o.Name === objectName(ref));
    const template = dialog.find(o => o.Name === objectName(slot.Properties.Content));
    assert.equal(template.Type, 'UMG_MatchmakingRegion_C');
    assert.deepEqual(Object.keys(template.Properties).sort(), ['Slot', 'bIsVariable']);
  }
  assert.ok(hasEvent(widget(confirm + 'UMG_MatchmakingRegion'), 'ButtonMain', 'OnClicked'));
  for (const fn of ['OnButtonConfirm', 'OnButtonCancelConfirm']) assert.equal(member(dialog, fn, 'MatchmakingRegionName').Type, 'NameProperty');
});

test('canonical service binds a session but party wire fields in that historical source use its constant', () => {
  const js = historicalSource();
  assert.ok(js.includes('socket.playerId = gateSessions.get(RPCPath)'));
  const route = name => js.split('else if(RPCPath === "/party.party/' + name + '"){')[1].split('else if(RPCPath ===')[0];
  assert.ok(route('Create').includes('PartyMembers: [TEMP_USER_ID]'));
  assert.ok(route('QueryPresence').includes('UserId: TEMP_USER_ID'));
  assert.ok(route('Get').includes('const empty = Buffer.alloc(0)'));
  const reply = responseType('CreatePartyResponse');
  assert.deepEqual(reply.fieldsArray.map(f => [f.name, f.id]), [['StatusCode', 1], ['PartyId', 2], ['PartyMembers', 3]]);
  assert.equal(reply.fields.PartyMembers.repeated, true);
});

test('the stock region builder yields one missing ID on the wire, distinct from a repeated field of length zero', () => {
  const js = historicalSource();
  const declaration = name => js.match(new RegExp('^const ' + name + ' = .+$', 'm'))[0];
  // 固定摘要的公开 JS 中仅执行三个数据常量和纯函数，保留原始发现条目。
  const builder = js.match(/^function BuildRegionList\(\)\{[\s\S]*?^\}/m)[0];
  const program = [declaration('MatchmakingHost'), declaration('MatchmakingPort'),
    declaration('matchmakingUDPServerDiscoveryPayload'), builder,
    '({ discovery: matchmakingUDPServerDiscoveryPayload, rows: BuildRegionList() })'].join('\n');
  const data = vm.runInNewContext(program, { process: { env: {} } }, { timeout: 500 });
  const type = responseType('QueryMatchmakingRegionResponse');
  const encode = rows => type.encode(type.create({ StatusCode: 0, Regions: rows })).finish();
  const stock = encode(data.rows);
  const control = encode([]);
  assert.equal(data.discovery.servers.length, 1);
  assert.equal(data.discovery.servers[0].region_id, '336d1f3e-3ecb-11eb-a7dc-3b7705f20f56');
  assert.equal(stock.length, 14);
  const [row] = type.decode(stock).Regions;
  assert.deepEqual([type.decode(stock).Regions.length, row.RegionId, row.RegionName], [1, '', 'us-east1']);
  assert.equal(Buffer.from(control).toString('hex'), '0800');
  assert.equal(type.decode(control).Regions.length, 0);
  const observed = JSON.parse(fs.readFileSync(path.join(__dirname, 'evidence/20261121-preview-region-reply.json'), 'utf8'));
  const expected = { StatusCode: 0, Regions: [{ RegionId: data.discovery.servers[0].region_id, RegionName: 'us-east1' }] };
  assert.deepEqual(observed, expected);
  assert.equal(type.encode(observed).finish().length, 52);
});

test('the published Start request carries selected IDs separately from the returned region names', () => {
  const proto = require(path.join(serviceRoot, 'node_modules/protobufjs'));
  const schema = proto.loadSync(path.join(serviceRoot, 'game/proto/Request/StartMatchmakingRequest.proto'));
  const start = schema.lookupType('ProjectBoundary.StartMatchmakingRequest');
  const payload = start.fields.Payload.resolve().resolvedType;
  const list = payload.fields.UnknownMessage;
  assert.ok(list.repeated);
  assert.equal(list.resolve().resolvedType.fields.RegionId.id, 1);
  assert.equal(list.resolvedType.fields.RegionName, undefined);
  const js = historicalSource();
  assert.ok(js.includes('regionIds = (req.Payload.UnknownMessage || []).map(m => m.RegionId).filter(Boolean)'));
});

test('the preview RPC name matches the retail address table row and its discovery ID does not', () => {
  const pkg = 'ProjectBoundary/Content/GameData/CSVConfig/Matchmaking/DT_MatchmakingRegionAddress';
  const file = process.env.BOUNDARY_REGION_TABLE || 'C:/BrtWork/Assets/exports/tables/' + pkg + '.json';
  const output = execFileSync(process.execPath, [path.join(__dirname, 'region-keys.cjs'), file,
    path.join(__dirname, 'evidence/20261121-preview-region-reply.json')], { encoding: 'utf8', windowsHide: true });
  const result = JSON.parse(output);
  assert.equal(result.package, pkg);
  assert.equal(result.packageEvidence, 'export');
  assert.equal(result.tableSha256, '0ecad9cc89e2f4f8df2859a6ce7485f5e3eabed2e5d3ad7105653a1247a38c96');
  assert.deepEqual(result.keys.slice().sort(), ['asia-east2', 'asia-northeast1', 'asia-southeast1',
    'europe-west3', 'us-east1', 'us-west1', 'us-west2']);
  assert.deepEqual(result.matches, [{ index: 0, byRegionName: 'us-east1', byRegionId: null }]);
});

test('a log window separates a hall query, a later reply, and an empty observation', async () => {
  const sample = ['[PARTY] Presence InShowRoom => InHall', '[RECV] Query Playlists!', '[RECV] Query Matchmaking Region!',
    'Method: GET', 'URL: /?private-token', '[PVE-REGIONS] rpc rows=1 ids=1 names=us-east1',
    '[RECV] Start Matchmaking!', 'unrelated-private-placeholder'];
  const before = await regionTrace(sample, { after: 0, through: 4 });
  assert.equal(before.hall['InShowRoom -> InHall'].firstLine, 1);
  assert.equal(before.events.regionRequest.firstLine, 3);
  assert.equal(before.events.regionReply.count, 0);
  const after = await regionTrace(sample, { after: 4, through: 6 });
  assert.equal(after.events.regionRequest.count, 0);
  assert.equal(after.events.discoveryRequest.count, 0);
  assert.equal(after.replies[0].firstLine, 6);
  assert.equal((await regionTrace(sample, { after: 7 })).events.startRequest.count, 0);
  assert.equal((await regionTrace(sample, { after: 8, through: 8 })).throughLine, 8);
  assert.ok(!JSON.stringify(after).includes('private'));
  const tagged = await regionTrace(['  "user-agent": "BRT-region-acceptance/anonymous-readonly",',
    '  "user-agent": "BRT-region-acceptance/diagnostic-session"', '  "user-agent": "private-agent"',
    '[RECV] Query Matchmaking Region!'], { after: 0 });
  assert.deepEqual(Object.keys(tagged.diagnosticHeaders), ['anonymous-readonly', 'diagnostic-session']);
  assert.equal(tagged.events.regionRequest.firstLine, 4);
  assert.ok(!JSON.stringify(tagged).includes('private-agent'));
  for (const bounds of [{ after: 9 }, { through: 9 }, { after: 5, through: 4 }, { after: -1 }]) {
    await assert.rejects(regionTrace(sample, bounds));
  }
});

test('offline key comparison requires the actual table type and checks each service field independently', () => {
  const name = 'DT_MatchmakingRegionAddress', pkg = 'ProjectBoundary/Content/GameData/CSVConfig/Matchmaking/' + name;
  const table = { Type: 'DataTable', Name: name, Package: pkg, Properties: {
    RowStruct: { ObjectName: "ScriptStruct'PBMatchmakingRegionAddressRow'", ObjectPath: '/Script/ProjectBoundary' },
  }, Rows: { 'fixture-key': {}, fixture_guid: {} } };
  const { keys, packageEvidence } = readAddressTable([table]);
  assert.equal(packageEvidence, 'export');
  const unmarked = [{ ...table, Package: undefined }];
  assert.deepEqual(readAddressTable(unmarked, pkg + '.json'), { package: pkg, packageEvidence: 'export-path', keys });
  assert.deepEqual(readAddressTable(unmarked), { package: null, packageEvidence: 'unspecified', keys });
  for (const replacement of [{ Package: pkg + '_other' }, { Type: 'Function' }, { Properties: {} }, { Rows: [] },
    { Rows: { incomplete: null } }, { Type: 'CompositeDataTable', Properties: { ...table.Properties, ParentTables: [{}] } }]) {
    assert.throws(() => readAddressTable([{ ...table, ...replacement }]));
  }
  assert.deepEqual(compareKeys(keys, { StatusCode: 0, Regions: [
    { RegionName: 'FIXTURE-KEY', RegionId: 'elsewhere' }, { RegionName: 'fixture_key', RegionId: 'fixture_guid' }, {},
  ] }), [{ index: 0, byRegionName: 'fixture-key', byRegionId: null },
    { index: 1, byRegionName: null, byRegionId: 'fixture_guid' }, { index: 2, byRegionName: null, byRegionId: null }]);
  assert.deepEqual(compareKeys(keys, { Regions: [] }), []);
  assert.throws(() => compareKeys(keys, { StatusCode: 1, Regions: [] }));
  assert.throws(() => compareKeys(keys, {}));
  assert.throws(() => compareKeys(['fixture', 'Fixture'], { Regions: [] }));
});
