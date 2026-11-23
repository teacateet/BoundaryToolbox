'use strict';

const { modesFrom } = require('./native-menu.cjs');

let ordinal = 0;
const states = new Set(['queued', 'found']);
const safeState = value => states.has(value) ? value : 'other';

function createMenuSession(readTable, local, emit = console.log) {
  let current, modes, lastQuery;
  const log = (entry, text) => emit('[PVE-MENU ' + (entry?.seq ?? 0) + '] ' + text);
  function start(player, requester, selected) {
    try { modes ??= modesFrom(readTable()); } catch { /* 原表缺失时仍保留原服务行为。 */ }
    const mode = modes?.find(m => m.id === selected || m.matchName === selected);
    const field = mode ? (selected === mode.id ? 'row' : 'match-string') :
      selected === undefined ? 'undecoded' : !selected ? 'missing' : selected === 'Purge' ? 'pvp' : 'other';
    const owner = local && mode && player && player === requester ? player : null;
    const entry = { seq: ++ordinal, owner, mayPollEmpty: !current, ticket: null, state: null };
    current = entry;
    lastQuery = null;
    log(entry, 'start mode=' + (mode?.id ?? 'unclassified') + ' field=' + field);
    return entry;
  }

  function enqueued(entry, ticket, status) {
    const known = safeState(status);
    if (current === entry && entry.owner && states.has(known) && typeof ticket === 'string' && /^[a-zA-Z0-9_-]{1,128}$/.test(ticket)) {
      entry.ticket = ticket;
      entry.state = known;
    }
    log(entry, 'enqueue state=' + (status === null ? 'unavailable' : known));
  }

  function query(player, explicit, bytes, decoded) {
    const entry = current;
    const owned = entry?.owner && entry.owner === player && entry.ticket;
    const implicit = owned && entry.mayPollEmpty && decoded && bytes === 0 && !explicit;
    const linked = Boolean(owned && (explicit === entry.ticket || implicit));
    const observedEntry = linked || (!explicit && entry?.owner && entry.owner === player) ? entry : null;
    return { entry: observedEntry, linked, ticket: explicit || (implicit ? entry.ticket : ''),
      cached: linked && entry.state === 'found',
      via: explicit ? 'explicit' : implicit ? 'session' : bytes === 0 ? 'empty-unbound' : 'unresolved' };
  }

  function queried(q, backend, replyBytes) {
    if (q.linked && current === q.entry && backend === 'found') current.state = 'found';
    const observed = ['cache-found', 'not-polled', 'unavailable'].includes(backend) ? backend : safeState(backend);
    const text = 'query via=' + q.via + ' backend=' + observed + ' replyBytes=' + replyBytes;
    if (current !== q.entry || text !== lastQuery) log(q.entry, text);
    if (current === q.entry) lastQuery = text;
  }

  function stop(player, explicit) {
    const related = current?.owner && current.owner === player && (!explicit || explicit === current.ticket);
    log(related ? current : null, 'stop ticket=' + (explicit ? 'explicit' : 'absent'));
    // 空停止请求不是取消下游票据的授权；只停用本会话的无票据轮询。
    if (current && current.owner === player) {
      if (explicit && explicit === current.ticket && current.mayPollEmpty) current = null;
      else current.mayPollEmpty = false;
    }
  }

  return { start, enqueued, query, queried, stop, reset() { current = null; lastQuery = null; } };
}

module.exports = { createMenuSession };
