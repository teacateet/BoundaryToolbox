'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const udp = require('node:dgram');
const { once } = require('node:events');
const { execFileSync } = require('node:child_process');
const { sessions } = require('../native-menu/menu-trace.cjs');
const sha = data => createHash('sha256').update(data).digest('hex');

function embedded(exe) {
  const pe = fs.readFileSync(exe);
  assert.equal(pe.toString('ascii', 0, 2), 'MZ');
  const header = pe.readUInt32LE(60);
  assert.equal(pe.toString('ascii', header, header + 4), 'PE\0\0');
  const base = path.resolve(__dirname, '../..');
  const runtime = path.join(base, 'src/core/local_profile.rs');
  const manifest = fs.readFileSync(runtime, 'utf8').split('const KIT:')[1]?.split('];')[0];
  assert.ok(manifest, 'missing production archive kit manifest');
  const inputs = [...manifest.matchAll(/include_bytes!\(\s*"([^"]+)"\s*\)/g)]
    .map(m => path.resolve(path.dirname(runtime), m[1]));
  assert.equal(inputs.length, (manifest.match(/include_bytes!/g) || []).length, 'unrecognized kit entry');
  // 即使某项意外从生产清单消失，也不能让旧客户端读档或主机匣修复通过验收。
  for (const required of ['prepare.cjs', 'native-equip-appearance.patch', 'ready-room.cjs', 'room-spawn.cjs',
    'legacy-client.patch', 'legacy-client.cjs', 'spawn-parts.cjs', 'receiver-main.json', 'role-paint-scopes.json'])
    assert.ok(inputs.some(item => path.basename(item) === required), 'required component missing from manifest: ' + required);
  inputs.push(path.join(base, 'integrations/fixed-server/local-pve-host.cjs'));
  const files = {};
  for (const item of inputs) {
    const name = path.relative(base, item).replaceAll(path.sep, '/');
    assert.ok(name.startsWith('integrations/') && !Object.hasOwn(files, name), 'invalid or duplicate component: ' + name);
    const bytes = fs.readFileSync(item);
    assert.ok(bytes.length && pe.includes(bytes), 'exact component missing from executable: ' + name);
    files[name] = sha(bytes);
  }
  return { components: inputs.length, exeSha256: sha(pe), files };
}

function auditedLease(testExe, home, source, node, port) {
  const out = execFileSync(testExe, ['--ignored', '--exact', 'core::local_profile::tests::live_fixture_room_path', '--nocapture'], {
    encoding: 'utf8', timeout: 15000, windowsHide: true,
    env: { ...process.env, PROFILE_LEASE_FIXTURE: JSON.stringify([home, source, node, port]) },
  });
  const match = /^fixture-lease=(.+)$/m.exec(out);
  assert.ok(match, 'the compiled production ownership/path check ran');
  return JSON.parse(match[1]);
}

// 每个来源在同一托管应用上启动两次不同 PID，房间端口由操作系统分配。
async function exerciseMenu({ home, service, lease, connect, before }) {
  const { meta, ports, request } = service;
  const profileBytes = () => Object.fromEntries(fs.readdirSync(meta.loadoutDir).sort().filter(n => n.endsWith('.json'))
    .map(n => [n, sha(fs.readFileSync(path.join(meta.loadoutDir, n)))]));
  const initial = profileBytes();
  assert.ok(Object.keys(initial).length, 'fixture contains preexisting loadouts');
  const { roomFileFor } = require(path.join(meta.appDir, 'ready-room.cjs'));
  assert.equal(lease, roomFileFor(meta.appDir), 'the writer and the deployed reader resolve the same file');
  if (before) {
    assert.equal(lease, before.lease);
    assert.notEqual(meta.pid, before.pid);
    assert.equal(JSON.parse(fs.readFileSync(lease)).runId, before.runId);
  } else assert.ok(!fs.existsSync(lease));
  const runId = 'fixture-' + meta.pid;
  const game = udp.createSocket('udp4');
  game.bind(0, '127.0.0.1');
  await once(game, 'listening');
  let c;
  const write = value => {
    const tmp = lease + '.' + runId + '.tmp';
    fs.writeFileSync(tmp, JSON.stringify(value), { flag: 'wx' });
    fs.renameSync(tmp, lease);
  };
  try {
    const account = 'native_menu_restart_fixture';
    const { pvePlan } = require(path.join(meta.appDir, 'native-menu.cjs'));
    const table = require(path.join(meta.appDir, 'game/definitions/DT_GameModeDefinition.json'));
    const plan = pvePlan(table, 'NORMAL BOT', 'CircularX');
    const receipt = { v: 1, runId, mode: 'pve', offline: true, difficulty: 'normal', map: plan.map,
      gameClass: plan.gameClass, port: game.address().port };
    const publish = () => write({ ...receipt, expiresAt: Date.now() + 14000 });
    c = await connect(meta.appDir, ports[1], request, account);
    const pr = '/matchmaking.Matchmaking/', empty = Buffer.alloc(0);
    const query = async () => assert.equal((await c.wire(pr + 'QueryUnityMatchmaking', empty)).length, 0);
    const stop = async () => assert.equal((await c.wire(pr + 'StopUnityMatchmaking', empty)).length, 0);
    const start = async mode => {
      const t = c.type('Request/StartMatchmakingRequest', 'StartMatchmakingRequest');
      const ack = await c.wire(pr + 'StartUnityMatchmaking', t.encode({
        Payload: { MatchmakingRequestorUserId: account }, GameMode: mode,
      }).finish());
      assert.equal(Buffer.from(ack).toString('hex'), '0800');
    };
    const presence = async value => {
      const t = c.type('Request/SetPartyPresenceRequest', 'SetPartyPresenceRequest');
      const ack = await c.wire('/party.party/SetPresence', t.encode({ Presence: value }).finish());
      assert.equal(c.type('Response/SetPartyPresenceResponse', 'SetPartyPresenceResponse').decode(ack).StatusCode, 0);
    };
    const log = () => fs.readFileSync(path.join(home, 'fixture.log'), 'utf8').split(/\r?\n/);
    await presence('InHall');
    await c.query('QueryPlayList', 'QueryPlaylistResponse');
    await query();
    assert.equal((await sessions(log())).attempts.length, 0, 'even a ready room does not start on login/restart');
    assert.ok(!log().some(l => l.startsWith('[PVE-ROOM] ')));
    assert.deepEqual(profileBytes(), initial, 'disabled or expired room preserves existing loadouts');
    publish();
    await query();
    assert.ok(!log().some(l => l.startsWith('[PVE-ROOM] ')), 'publishing a room without a click cannot allocate');
    await start('HARD BOT');
    await query();
    await stop();
    await start('Rush_PVE_Normal');
    await query();
    await presence('InMatching');
    await presence('InMatch');
    write(null);
    await query();
    await stop();
    await query();
    publish();
    await start('NORMAL BOT');
    await query();
    const audit = await sessions(log());
    assert.deepEqual(audit.attempts.map(a => [a.mode, a.field]), [
      ['Rush_PVE_Hard', 'match-string'], ['Rush_PVE_Normal', 'row'], ['Rush_PVE_Normal', 'match-string'],
    ]);
    const events = audit.attempts.map(a => a.events.map(e => e.event));
    assert.ok(events[0].includes('room wait=difficulty') && events[0].includes('query via=session backend=queued replyBytes=0'));
    assert.deepEqual(events[1].filter(s => s.startsWith('room ') || s.startsWith('client ')),
      ['room ready map=CircularX', 'client presence=InMatching', 'client presence=InMatch', 'room wait=invalid-or-expired']);
    assert.ok(events[1].includes('query via=session backend=found replyBytes=0') && events[1].includes('stop ticket=absent'));
    assert.ok(events[2].includes('query via=session backend=found replyBytes=0'));
    assert.ok(!log().some(l => l.startsWith('[MATCH] MatchServer:')));
    assert.ok(!fs.existsSync(path.join(meta.loadoutDir, account + '.json')));
    assert.deepEqual(profileBytes(), initial, 'native menu selection must not touch any existing loadout');
    // 第一轮保留上一代（过期）的房间记录，不发送 Stop；下轮必须在新会话里重新选择。
    if (!before) write({ ...receipt, expiresAt: 0 });
    return { lease, runId, pid: meta.pid };
  } finally {
    c?.close();
    await new Promise(done => game.close(done));
    if (before && fs.existsSync(lease)) fs.unlinkSync(lease);
  }
}

module.exports = { auditedLease, exerciseMenu };

if (require.main === module) {
  assert.ok(process.argv.length === 4 && process.argv[2] === '--embedded', 'Usage: node verify-menu.cjs --embedded BRT.exe');
  console.log(JSON.stringify(embedded(process.argv[3])));
}
