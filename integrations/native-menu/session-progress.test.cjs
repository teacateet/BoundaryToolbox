'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { createMenuSession } = require('./session-progress.cjs');
const { sessions } = require('./menu-trace.cjs');

const base = process.env.BOUNDARY_META_SNAPSHOT ||
  'C:/BrtWork/SDK/sdk-fixture/validation-server';
const table = () => require(path.join(base, 'game/definitions/DT_GameModeDefinition.json'));
const owner = 'fixture_owner';
const testSession = (local = true) => createMenuSession(table, local, () => {});

test('only a matching bound account, a declared PvE choice and an acknowledged loopback ticket can service an empty query', () => {
  for (const [local, bound, requester, mode, status, ticket] of [
    [false, owner, owner, 'NORMAL BOT', 'queued', 'fixture'],
    [true, '', '', 'NORMAL BOT', 'queued', 'fixture'],
    [true, owner, '', 'NORMAL BOT', 'queued', 'fixture'],
    [true, owner, owner, undefined, 'queued', 'fixture'],
    [true, owner, owner, '', 'queued', 'fixture'],
    [true, owner, owner, 'custom-mode', 'queued', 'fixture'],
    [true, owner, owner, 'Purge', 'queued', 'fixture'],
    [true, owner, owner, 'NORMAL BOT', 'unknown', 'fixture'],
    [true, owner, owner, 'NORMAL BOT', null, null],
    [true, owner, owner, 'NORMAL BOT', 'queued', ''],
  ]) {
    const session = testSession(local);
    const entry = session.start(bound, requester, mode);
    session.enqueued(entry, ticket, status);
    assert.equal(session.query(owner, '', 0, true).ticket, '');
  }
  for (const mode of ['Rush_PVE_Easy', 'NORMAL BOT', 'Rush_PVE_Hard']) {
    const session = testSession();
    const entry = session.start(owner, owner, mode);
    session.enqueued(entry, 'fixture', 'queued');
    assert.equal(session.query(owner, '', 0, true).ticket, 'fixture');
    assert.equal(session.query(owner, '', 1, true).ticket, '');
    assert.equal(session.query(owner, '', 0, false).ticket, '');
    assert.equal(session.query('fixture_second', '', 0, true).ticket, '');
    assert.equal(session.query(owner, 'different', 11, true).entry, null);
    session.reset();
    assert.equal(session.query(owner, '', 0, true).ticket, '');
  }
});

test('overlapping starts and late replies cannot attach a ticket or a found state to the next choice', () => {
  const session = testSession();
  const first = session.start(owner, owner, 'Rush_PVE_Easy');
  session.enqueued(first, 'first', 'queued');
  const old = session.query(owner, '', 0, true);
  const second = session.start(owner, owner, 'HARD BOT');
  session.enqueued(second, 'second', 'queued');
  session.enqueued(first, 'first', 'found');
  session.queried(old, 'found', 0);
  assert.equal(session.query(owner, '', 0, true).ticket, '');
  const explicit = session.query(owner, 'second', 10, true);
  assert.ok(explicit.linked && !explicit.cached);
  session.queried(explicit, 'found', 0);
  assert.ok(session.query(owner, 'second', 10, true).cached);
});

test('menu asset declares a local enum parameter; the release schema exposes no query response field', () => {
  const ui = process.env.BOUNDARY_UI_PROPERTIES ||
    'C:/BrtWork/Assets/exports/ui-current/properties/ProjectBoundary/Content/UI';
  const menu = JSON.parse(fs.readFileSync(path.join(ui, 'MainMenu/UMG_Assignment.json'), 'utf8'));
  const event = menu.find(o => o.Type === 'Function' && o.Name === 'K2_OnMatchmakingStatusChange');
  assert.equal(event.SuperStruct.ObjectPath, '/Script/ProjectBoundary');
  assert.deepEqual(event.ChildProperties.map(p => [p.Name, p.Type, p.Enum.ObjectName]),
    [['InMatchmakingStatusd', 'EnumProperty', "Enum'EPBMatchmakingStatus'"]]);
  const protobuf = require(path.join(base, 'node_modules/protobufjs'));
  const zip = process.env.BOUNDARY_RELEASE_084 || path.resolve(__dirname, '../../.workflow/qa/rebound-084/Release.zip');
  function fields(file, name) {
    const text = execFileSync('tar', ['-xOf', zip, 'BoundaryMetaServer-main/game/proto/' + file + '.proto'],
      { encoding: 'utf8', windowsHide: true });
    return protobuf.parse(text).root.lookupType('ProjectBoundary.' + name).fieldsArray.map(f => [f.name, f.id, f.type]);
  }
  assert.deepEqual(fields('Request/StartMatchmakingRequest', 'StartMatchmakingRequest'),
    [['Payload', 1, 'MatchmakingPayload'], ['GameMode', 2, 'string']]);
  assert.deepEqual(fields('Response/StartMatchmakingResponse', 'StartMatchmakingResponse'), [['StatusCode', 1, 'int32']]);
  assert.deepEqual(fields('Response/matchmaking_ext', 'QueryUnityMatchmakingRes'), []);
});

test('every progress and ready-room hunk has one exact context on the integrated or canonical service', () => {
  const file = process.env.BOUNDARY_MENU_STAGED_INDEX || path.join(base, 'index.js');
  let source = fs.readFileSync(file, 'utf8').replaceAll('\r\n', '\n');
  for (const name of ['session-progress.patch', 'ready-room.patch']) {
    const patch = fs.readFileSync(path.join(__dirname, name), 'utf8').replaceAll('\r\n', '\n');
    for (const group of patch.split(/^@@ .* @@[^\n]*\n/gm).slice(1)) {
      const lines = group.replace(/\n$/, '').split('\n');
      const before = lines.filter(s => s[0] !== '+').map(s => s.slice(1)).join('\n') + '\n';
      const after = lines.filter(s => s[0] !== '-').map(s => s.slice(1)).join('\n') + '\n';
      assert.equal(source.split(before).length, 2, name + ': ' + before.slice(0, 80));
      source = source.replace(before, () => after);
    }
  }
  new (require('node:vm').Script)(source);
});

test('ticket diagnostics correlate native queries and writes with the same choice and mode', async () => {
  const mode = 'Rush_PVE_Normal';
  const event = (name, extra) => '[PVE-ROOM] ' + JSON.stringify({ choice: 1, mode, event: name, ...extra });
  const report = await sessions(['[PVE-MENU 1] start mode=Rush_PVE_Normal field=row',
    event('selection-wait', { reason: 'pool-choice' }),
    event('ticket-created'), event('ticket-queried'), event('endpoint-sent'),
    event('join-requested', { requestId: 'private-request' }),
    event('endpoint-sent', { mode: 'Rush_PVE_Hard' }), event('selection-wait', { reason: 'region' })]);
  assert.deepEqual(report.attempts[0].events.map(row => row.event),
    ['room wait=pool-choice', 'ticket-created', 'ticket-queried', 'endpoint-sent', 'join-requested', 'room wait=region']);
  assert.ok(!JSON.stringify(report).includes('private-request'));
  assert.equal(report.unassociated, 1);
});

test('trace report keeps only allowlisted stages and never merges a restarted process or unbound traffic', async () => {
  const lines = ['[PVE-MENU 1] start mode=Rush_PVE_Easy field=row', '[PVE-MENU 2] start mode=Rush_PVE_Normal field=match-string',
    '[PVE-MENU 1] enqueue state=queued', '[PVE-MENU 2] enqueue state=found',
    '[PVE-MENU 0] query via=empty-unbound backend=not-polled replyBytes=0',
    '[PVE-MENU 1] start mode=Rush_PVE_Hard field=row', '[PVE-MENU 2] stop ticket=absent',
    '[PVE-MENU 1] query via=session backend=found replyBytes=0', '[PVE-MENU 1] query payload=private',
    '[PVE-MENU 1] enqueue state=private', 'private'];
  const evidence = await sessions(lines);
  assert.deepEqual(evidence.attempts.map(r => r.events.map(e => e.line)), [[3], [4], [8]]);
  assert.equal(evidence.unassociated, 2);
  assert.ok(!JSON.stringify(evidence).includes('private'));
});
