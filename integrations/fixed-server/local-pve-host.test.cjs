'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const {offlineRoom, roomJournal} = require('./local-pve-host.cjs');
const local = 'http://127.0.0.1:8000';

test('automatic rooms select the local archive and the chosen retail game mode', () => {
  for (const [level, suffix, supported] of [
    ['easy', 'Easy', ['Warehouse', 'DataCenter']], ['normal', 'Normal', ['MiniFarm', 'CircularX', 'OSS']],
    ['hard', 'Hard', ['OSS', 'CircularX']]
  ]) {
    for (const map of supported) {
      const {args} = offlineRoom([map, level, '19786', local]);
      assert.deepEqual(args.filter(s => s.startsWith('-LogicServerURL=')), ['-LogicServerURL=' + local]);
      assert.ok(args.includes('-map=' + map));
      assert.deepEqual(args.filter(s => s.toUpperCase().startsWith('-MULTIHOME=')), ['-MULTIHOME=127.0.0.1']);
      assert.ok(args.find(s => s.startsWith('-mode=')).endsWith('_' + suffix + '_C'));
      assert.ok(args.includes('-server') && args.includes('-pve') && !args.some(s => s.startsWith('-online')));
    }
  }
  for (const [map, level] of [['Warehouse','normal'], ['MiniFarm','easy'], ['DataCenter','hard']])
    assert.throws(() => offlineRoom([map, level, '19786', local]), /Map not supported at this difficulty/);
  for (const invalid of [['MiniFarm','normal','19786','http://192.0.2.1:8000'], ['MiniFarm','normal','8000',local],
      ['MiniFarm','normal','19786',local,'-online=127.0.0.1'], ['MiniFarm','pvp','19786',local],
      ['MiniFarm -port=7777','normal','19786',local]]) assert.throws(() => offlineRoom(invalid));
});

test('room lifecycle survives repeat maps and does not log identities or end reasons', () => {
  const out = [], observe = roomJournal(x => out.push(x));
  const map = '[SERVER] Map loaded: MiniFarm', begin = '[SERVER_LIFECYCLE] match_started';
  const preorder = '[BRT-INVENTORY] owner-preorder-submitted roles=6 slots=36';
  for (const line of [map, '[PLAYER] private player data', preorder, preorder + ' account=private',
    '[BRT-INVENTORY] role-displayed', begin, '[SERVER_LIFECYCLE] match_end_detected reason=private text', map]) observe(line);
  assert.deepEqual(out, [map, preorder, begin, '[SERVER_LIFECYCLE] match_end_detected', map]);
});

test('preserves the real MiniFarm start and subsequent map transition', {skip: !process.env.BRT_QA_ROOM_LOG}, () => {
  const out = [], observe = roomJournal(x => out.push(x));
  for (const s of fs.readFileSync(process.env.BRT_QA_ROOM_LOG, 'utf8').split('\n')) observe(s.trimEnd());
  const map = out.indexOf('[SERVER] Map loaded: MiniFarm'), ready = out.indexOf('[SERVER] Server is now listening.');
  const start = out.indexOf('[SERVER_LIFECYCLE] match_started'), next = out.indexOf('[SERVER] Map loaded: Warehouse');
  assert.ok(map >= 0 && ready > map && start > ready && next > start);
});

test('keeps per-life and per-slot game readbacks separate from submission and rejection', () => {
  const out = [], observe = roomJournal(s => out.push(s)), prefix = '[BRT-INVENTORY] ';
  const birth = 'observed-birth life=1 pawn-age=0.0167 inventory=2 holding=0 acknowledged=0';
  const received = 'newborn-weapon life=1 slot=1 submitted=1 parts-readback=1';
  const rejected = 'newborn-weapon life=1 slot=2 submitted=1 parts-readback=0';
  const skipped = 'newborn-slot-skipped life=2 slot=1 reason=weapon-in-use';
  const noArchive = 'newborn-skipped life=2 reason=local-archive-unavailable';
  const already = 'newborn-weapon life=2 slot=2 submitted=0 parts-readback=1';
  for (const bad of [received+' account=private', received.replace('slot=1','slot=3'), already.replace('life=2','life=0'),
    skipped.replace('weapon-in-use','custom text'), noArchive+'\n', birth.replace('0.0167','nan'),
    rejected.replace('life=1','life=76561198200512751'), received.replace('submitted=1','submitted=3')]) observe(prefix+bad);
  const emissions = [birth, received, rejected, skipped, noArchive, already].map(s => prefix+s);
  for (const line of emissions) observe(line);
  assert.deepEqual(out,emissions);
});
