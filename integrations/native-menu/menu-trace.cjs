'use strict';

const fs = require('node:fs');
const { createInterface } = require('node:readline');

const states = new Set(['InHall', 'InShootingRange', 'InParty', 'InMatching',
  'InMatch', 'InShowRoom', 'InTutorial', 'InBenchmark']);

// 只保留正常服务日志中的事件名与行号。账号、票据和地址不进入报告。
async function summarize(lines) {
  const events = new Map();
  let lineNumber = 0;
  for await (const line of lines) {
    lineNumber++;
    let name;
    if (line === '[RECV] Query Playlists!') name = 'playlist-request';
    else if (line === '[RECV] Start Matchmaking!') name = 'start-request';
    else if (/^\[MATCH\] MatchServer: ticket=\S+ status=found$/.test(line)) name = 'matcher-found';
    else {
      const transition = /^\[PARTY\] Presence (\w+) => (\w+)$/.exec(line);
      if (transition && states.has(transition[1]) && states.has(transition[2])) {
        name = transition[1] + ' -> ' + transition[2];
      }
    }
    if (name) {
      const item = events.get(name) ?? { count: 0, firstLine: lineNumber };
      item.count++;
      item.lastLine = lineNumber;
      events.set(name, item);
    }
  }
  return Object.fromEntries(events);
}

// 序号仅在一个服务进程内有意义；不把普通登录、队伍 presence 或其他连接的行配成一次点击。
async function sessions(lines) {
  const attempts = [], byId = new Map();
  const ready = new WeakSet();
  let lineNumber = 0, unassociated = 0;
  for await (const line of lines) {
    lineNumber++;
    if (line.startsWith('[PVE-ROOM] ') && line.length < 512) {
      let data;
      try { data = JSON.parse(line.slice('[PVE-ROOM] '.length)); } catch { continue; }
      if (!Number.isSafeInteger(data?.choice) || data.choice < 1) continue;
      const attempt = byId.get(String(data.choice));
      let event;
      if (data.event === 'selection-ready' && ['Warehouse', 'DataCenter', 'CircularX', 'MiniFarm', 'OSS'].includes(data.map) &&
          data.serverIp === '127.0.0.1' && Number.isInteger(data.serverPort) && data.serverPort >= 1024 && data.serverPort <= 65535) {
        event = 'room ready map=' + data.map;
      } else if (data.event === 'selection-wait' && ['absent', 'invalid-or-expired', 'different-run', 'not-local-pve',
        'port', 'difficulty', 'mode-map', 'mode-class', 'region', 'room-changed', 'pool-lease', 'pool-choice'].includes(data.reason)) {
        event = 'room wait=' + data.reason;
      } else if (data.event === 'client-presence' && ['InHall', 'InMatching', 'InMatch'].includes(data.presence)) {
        event = 'client presence=' + data.presence;
      } else if (['ticket-created', 'ticket-queried', 'endpoint-sent', 'join-requested'].includes(data.event)) {
        event = data.event;
      } else continue;
      if (!attempt || attempt.mode === 'unclassified' || (data.event === 'client-presence' ?
        !ready.has(attempt) : attempt.mode !== data.mode)) { unassociated++; continue; }
      if (data.event === 'selection-ready') ready.add(attempt);
      if (data.event === 'selection-wait') ready.delete(attempt);
      attempt.events.push({ line: lineNumber, event });
      continue;
    }
    const tag = /^\[PVE-MENU (\d{1,12})\] (.+)$/.exec(line);
    if (!tag) continue;
    const [, id, body] = tag;
    const start = /^start mode=(Rush_PVE_(?:Easy|Normal|Hard)|unclassified) field=(row|match-string|undecoded|missing|pvp|other)$/.exec(body);
    if (start && id !== '0') {
      if (byId.has(id)) byId.clear();
      const record = { attempt: Number(id), line: lineNumber, mode: start[1], field: start[2], events: [] };
      byId.set(id, record);
      attempts.push(record);
      continue;
    }
    if (!/^(?:enqueue state=(?:queued|found|unavailable|other)|stop ticket=(?:explicit|absent)|query via=(?:explicit|session|empty-unbound|unresolved) backend=(?:not-polled|cache-found|queued|found|unavailable|other) replyBytes=\d{1,8})$/.test(body)) continue;
    const attempt = byId.get(id);
    if (attempt) {
      if (body.startsWith('stop ')) ready.delete(attempt);
      attempt.events.push({ line: lineNumber, event: body });
    }
    else unassociated++;
  }
  return { attempts, unassociated };
}

// 只累计当前日志明示的请求、完成写出的回包和收到的探测，不按相邻行推断为同一次点击。
async function regionTrace(lines, window) {
  const { after = 0, through = Infinity } = window || {};
  if (!Number.isSafeInteger(after) || after < 0 ||
      (through !== Infinity && (!Number.isSafeInteger(through) || through < after))) {
    throw new Error('Expected a nonnegative line range (AFTER, THROUGH]');
  }
  const events = Object.fromEntries(['discoveryRequest', 'discoveryReply', 'regionRequest', 'regionReply',
    'qosRequest', 'startRequest'].map(key => [key, { count: 0 }]));
  const replies = new Map();
  const hall = {};
  const diagnosticHeaders = {};
  let lineNumber = 0, previousWasGet = false;
  function count(item) {
    if (!item.count++) item.firstLine = lineNumber;
    item.lastLine = lineNumber;
  }
  for await (const line of lines) {
    if (lineNumber >= through) break;
    lineNumber++;
    if (lineNumber <= after) continue;
    if (window) {
      const transition = /^\[PARTY\] Presence (\w+) => (\w+)$/.exec(line);
      const probe = /^\s*"user-agent":\s*"BRT-region-acceptance\/(anonymous-readonly|diagnostic-session)",?\s*$/.exec(line);
      if (probe) count(diagnosticHeaders[probe[1]] ??= { count: 0 });
      if (transition && states.has(transition[1]) && states.has(transition[2]) && transition.slice(1).includes('InHall')) {
        count(hall[transition[1] + ' -> ' + transition[2]] ??= { count: 0 });
      } else if (line === '[RECV] Query Playlists!') count(hall['playlist-request'] ??= { count: 0 });
    }
    if (previousWasGet && /^URL: \/(?:\?\S*)?$/.test(line)) count(events.discoveryRequest);
    previousWasGet = line === 'Method: GET';
    if (line === '[RECV] Query Matchmaking Region!') count(events.regionRequest);
    else if (line === '[MM] Recieved a new QoS message, echoing!') count(events.qosRequest);
    else if (line === '[RECV] Start Matchmaking!') count(events.startRequest);
    else if (line.startsWith('[PVE-REGIONS] ') && line.length < 256) {
      const discovery = /^\[PVE-REGIONS\] discovery rows=(\d{1,4})$/.exec(line);
      const rpc = /^\[PVE-REGIONS\] rpc rows=(\d{1,4}) ids=(\d{1,4}) names=(none|(?:us-east1|asia-east2|missing|other)(?:,(?:us-east1|asia-east2|missing|other))*)$/.exec(line);
      if (!discovery && !rpc) continue;
      const signature = discovery ? { via: 'http', rows: Number(discovery[1]) } :
        { via: 'rpc', rows: Number(rpc[1]), ids: Number(rpc[2]), names: rpc[3] === 'none' ? [] : rpc[3].split(',') };
      if (rpc && (signature.ids > signature.rows || signature.names.length !== signature.rows)) continue;
      count(discovery ? events.discoveryReply : events.regionReply);
      const key = JSON.stringify(signature);
      if (!replies.has(key)) replies.set(key, { ...signature, count: 0 });
      count(replies.get(key));
    }
  }
  const report = { events, replies: [...replies.values()] };
  if (!window) return report;
  if (lineNumber < after || (through !== Infinity && lineNumber < through)) throw new Error('Requested range exceeds this log');
  return { afterLine: after, throughLine: lineNumber, hall, diagnosticHeaders, ...report };
}

module.exports = { summarize, sessions, regionTrace };

if (require.main === module) {
  const ranged = process.argv[2] === '--region-window';
  const mode = new Map([['--sessions', sessions], ['--regions', regionTrace], ['--region-window', regionTrace]]).get(process.argv[2]);
  const file = process.argv[mode ? 3 : 2];
  const range = process.argv.slice(4);
  if (!file || (ranged ? range.length > 2 || range.some(n => !/^\d+$/.test(n)) : process.argv.length !== (mode ? 4 : 3))) {
    process.stderr.write('Usage: node menu-trace.cjs [--sessions|--regions] LOG | --region-window LOG [AFTER [THROUGH]]\n');
    process.exitCode = 1;
  } else {
    const input = fs.createReadStream(file, { encoding: 'utf8' });
    const window = ranged ? { after: Number(range[0] ?? 0), through: range[1] === undefined ? Infinity : Number(range[1]) } : undefined;
    (mode || summarize)(createInterface({ input, crlfDelay: Infinity }), window)
      .then(events => console.log(JSON.stringify(events, null, 2)))
      .catch(error => { console.error(error.message); process.exitCode = 1; });
  }
}
