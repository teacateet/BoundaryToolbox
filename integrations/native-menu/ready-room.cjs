'use strict';

const { selectablePool, advertisement, ephemeralPort, modesFrom, nextCards, nextPrefix, roomReader, roomFileFor, leaseMs, runOK, freshRoom, inspectRoom } = require('./native-menu.cjs');
const net = require('node:net');
const { randomUUID, randomInt } = require('node:crypto');

const loopback = ip => ip === '127.0.0.1' || ip === '::ffff:127.0.0.1';
function lobbyRoomReader(socket, read) {
  return loopback(socket.localAddress) && loopback(socket.remoteAddress) ? read : () => undefined;
}

function localPeer(rpc, ipc) {
  return ipc && rpc && !rpc.destroyed && !rpc.writableEnded && loopback(rpc.localAddress) &&
    loopback(rpc.remoteAddress) && ephemeralPort(rpc.localPort) && ephemeralPort(rpc.remotePort) && ipc.port !== rpc.localPort;
}

function receiveLine(client, consume) {
  let bytes = Buffer.alloc(0), received = false;
  client.on('data', chunk => {
    if (received || bytes.length + chunk.length > 1024) return client.destroy();
    bytes = Buffer.concat([bytes, chunk]);
    const nl = bytes.indexOf(10);
    if (nl < 0) return;
    if (nl !== bytes.length - 1) return client.destroy();
    let data;
    try { data = JSON.parse(bytes.toString('utf8')); } catch { return client.destroy(); }
    received = true;
    consume(data);
  });
}

function offerJoin(rpc, receipt, valid, onWritten) {
  const ipc = advertisement(receipt, 'joinIpc');
  if (!localPeer(rpc, ipc)) return;
  let active, timer, stopped = false, committed = false;
  function attempt(canRetry) {
    if (stopped || !localPeer(rpc, ipc) || !valid()) return;
    const requestId = randomUUID();
    const client = net.createConnection({ host: '127.0.0.1', port: ipc.port, family: 4 });
    active = client;
    const abort = () => client.destroy();
    const deadline = setTimeout(abort, 1000).unref();
    client.on('error', abort);
    client.once('close', () => {
      clearTimeout(deadline);
      // 仅无回包且尚未提交时补发一次；半帧、拒绝及提交结果不明均不得重试。
      if (canRetry && !stopped && !committed && client.bytesRead === 0)
        timer = setTimeout(() => attempt(false), 200).unref();
    });
    client.once('connect', () => {
      if (stopped || !valid()) return abort();
      deadline.refresh();
      client.write(JSON.stringify({ v: 1, op: 'offer', runId: receipt.runId, requestId,
        clientPort: rpc.remotePort, rpcPort: rpc.localPort, gamePort: receipt.port }) + '\n');
    });
    receiveLine(client, ready => {
      if (stopped || ready?.op !== 'ready' || Object.keys(ready).length !== 3 || ready.requestId !== requestId ||
          ready.nonce !== ipc.nonce || !localPeer(rpc, ipc) || !valid()) return abort();
      committed = true;
      client.end(JSON.stringify({ op: 'commit', requestId, nonce: ipc.nonce }) + '\n', onWritten);
    });
  }
  attempt(true);
  return () => { stopped = true; clearTimeout(timer); active?.destroy(); };
}

function nativeTicketProtocol(protobuf) {
  const schema = protobuf.parse(`syntax = "proto3";
    message LocalStart { repeated string regionIds = 3; }
    message LocalTicketRequest { repeated string userId = 1; string ticketId = 2; }`).root;
  const start = schema.lookupType('LocalStart'), ticket = schema.lookupType('LocalTicketRequest');
  return {
    regions(bytes) { try { return start.decode(bytes).regionIds; } catch { return null; } },
    request(bytes) { try { return ticket.decode(bytes); } catch { return null; } },
  };
}

function supportsRegions(ids, regions) {
  return Array.isArray(ids) && Array.isArray(regions) && regions.length > 0 &&
    ids.every(id => regions.some(row => row.RegionId === id));
}

function sameLocalRegions(previous, next) {
  if (!Array.isArray(previous?.ids) || !supportsRegions(next?.ids, next?.published)) return false;
  const prior = new Set(previous.ids), current = new Set(next.ids);
  return prior.size === current.size && [...current].every(id => prior.has(id));
}

function createReadyRoomSession(readTable, readRoom, { now = Date.now, report = () => {}, registerSpawn } = {}) {
  let modes, selection, localOwner, pendingReturn;
  function findMode(name) {
    try { modes ??= modesFrom(readTable()); } catch { return null; }
    return modes.find(mode => mode.id === name || mode.matchName === name);
  }
  function poolLease(choice, receipt) {
    const ipc = advertisement(receipt, 'controlIpc');
    return choice.pool?.ipc && ipc && ipc.port === choice.pool.ipc.port && ipc.nonce === choice.pool.ipc.nonce &&
      freshRoom(receipt, now()) && receipt.autoPool === true && receipt.runId === choice.runId &&
      receipt.mode === 'pve' && receipt.offline === true;
  }
  function resolve(receipt) {
    function wait(reason) {
      const waitingForOwnedRoom = reason === 'pool-choice' && !selection.pool.prepared &&
        selection.pool.admitted && receipt.queuedChoice === selection.pool.id;
      if (selection.ticketState && !waitingForOwnedRoom) {
        selection.ticketState = 'revoked';
        revokeJoin();
      }
      if (selection.wait !== reason) report({ event: 'selection-wait', choice: selection.choice, mode: selection.mode, reason });
      selection.wait = reason;
      return { ticketId: selection.ticket, status: 'queued' };
    }
    if (selection.pool) {
      if (!poolLease(selection, receipt)) return wait('pool-lease');
      if (!selection.pool.prepared || receipt.choiceId !== selection.pool.id) return wait('pool-choice');
    } else if (receipt?.autoPool === true) return wait('different-run');
    const { plan, reason } = inspectRoom(receipt, readTable, now(), selection);
    if (reason) return wait(reason);
    if (selection.map && selection.map !== plan.map) return wait('different-map');
    if (selection.regionChoice && !supportsRegions(selection.regionChoice.ids, selection.regionChoice.published)) return wait('region');
    if (selection.room && (selection.room.map !== plan.map || selection.room.port !== receipt.port)) return wait('room-changed');
    if (!selection.room || selection.wait) {
      selection.room = { map: plan.map, port: receipt.port };
      report({ event: 'selection-ready', choice: selection.choice, mode: selection.mode, map: plan.map,
        serverIp: '127.0.0.1', serverPort: receipt.port });
    }
    selection.wait = null;
    return { ticketId: selection.ticket, status: 'found', serverIp: '127.0.0.1', serverPort: receipt.port };
  }
  function start(player, requester, gameMode, ticket, choice, regionChoice) {
    const valid = player && player === requester && runOK(ticket) && Number.isSafeInteger(choice) && choice >= 1;
    const custom = typeof gameMode === 'string' && gameMode.startsWith(nextPrefix);
    const mode = valid && findMode(gameMode);
    if (!custom && !mode) { reset(); return null; }
    const receipt = readRoom();
    const available = selectablePool(receipt, now());
    // 已缓存的本地卡不能交给公网匹配器，也不能降级成别的地图。
    const blocked = () => { reset(); return {status:'unavailable'}; };
    if (custom && (!valid || !available || regionChoice?.authenticated !== true ||
        !supportsRegions(regionChoice?.ids, regionChoice?.published))) return blocked();
    // 同一大厅重复点击仍在准备的同一选项时，保留原控制连接和待发票据。
    if (selection?.player === player && selection.input === gameMode && selection.control?.writable &&
        !selection.control.readableEnded && !selection.pool.prepared && poolLease(selection, receipt) &&
        sameLocalRegions(selection.regionChoice, regionChoice)) {
      return { ticketId: selection.ticket, status: 'queued' };
    }
    reset();
    // 未启用本机房间时，原匹配器继续处理。存在但失效的记录不能用于分配。
    if (receipt === undefined) return null;
    let decided;
    if (available) {
      let cards;
      try { cards = nextCards(readTable()); } catch { return blocked(); }
      const pairs = custom ? cards.find(c => c.id === gameMode)?.pairs : cards[0].pairs.filter(p => p.mode === mode.id);
      if (!pairs?.length) return blocked();
      decided = pairs[randomInt(pairs.length)];
    }
    localOwner = player;
    selection = { player, ticket, input: gameMode, mode: decided?.mode ?? mode.id, map: decided?.map,
      choice, regionChoice, runId: freshRoom(receipt, now()) ? receipt.runId : null };
    if (receipt?.autoPool === true) selection.pool = { id: randomUUID(), ipc: advertisement(receipt, 'controlIpc') };
    return resolve(receipt);
  }
  function prepare(player, ticket, rpc, publish, started, admitted) {
    const choice = selection;
    if (!choice?.pool || choice.player !== player || choice.ticket !== ticket || choice.control ||
        !localPeer(rpc, choice.pool.ipc) || !poolLease(choice, readRoom()) ||
        !supportsRegions(choice.regionChoice?.ids, choice.regionChoice?.published)) return;
    const { id, ipc } = choice.pool;
    const control = net.createConnection({ host: '127.0.0.1', family: 4, port: ipc.port });
    choice.control = control;
    const valid = () => !control.destroyed && selection === choice && localPeer(rpc, ipc) && poolLease(choice, readRoom());
    // 启动可以跨过多次续租；原运行失去租约时同步结束等待。
    const watch = setInterval(() => {
      const receipt = readRoom();
      if (!valid()) return control.destroy();
      if (!choice.pool.admitted && receipt?.queuedChoice === id) {
        choice.pool.admitted = true;
        admitted?.();
      }
    }, 1000).unref();
    control.on('error', () => control.destroy());
    function disconnected() {
      clearInterval(watch);
      if (selection === choice) {
        if (choice.commitWritten) {
          // 对局接手后准备连接会关闭，保留归属以便显示这位玩家稍后的回大厅通知。
          pendingReturn = { player: choice.player, runId: choice.runId, choice: choice.choice };
        } else feedback(choice, 'prepare-ended');
        stop(player, ticket);
      }
    }
    control.once('end', disconnected);
    control.once('close', disconnected);
    control.once('connect', () => {
      if (!valid()) return control.destroy();
      control.write(JSON.stringify({ v: 1, op: 'prepare', runId: choice.runId, requestId: id, nonce: ipc.nonce,
        mode: choice.mode, ...(choice.map ? {map:choice.map} : {}), clientPort: rpc.remotePort, rpcPort: rpc.localPort }) + '\n');
      started?.();
    });
    receiveLine(control, reply => {
      if (reply?.op !== 'prepared' || reply.requestId !== id || !valid() ||
          Object.keys(reply).some(k => !['op','requestId','spawn'].includes(k))) return control.destroy();
      choice.pool.prepared = true;
      if (status(player, ticket)?.status !== 'found') {
        choice.pool.prepared = false;
        return control.destroy();
      }
      if (reply.spawn && registerSpawn) {
        // 交接后大厅 RPC 可断开；专服是否仍归本次对局由工具箱独占的房间租约证明。
        const run = choice.runId;
        registerSpawn(id, player, reply.spawn, run, () => {
          const receipt = readRoom();
          return freshRoom(receipt, now()) && receipt.mode === 'pve' && receipt.offline === true &&
            receipt.autoPool === true && receipt.runId === run && receipt.spawnChoice === id;
        });
      }
      clearInterval(watch);
      publish();
      choice.awaitAssignment?.();
    });
  }
  function status(player, ticket) {
    if (!selection || player !== selection.player || ticket !== selection.ticket) return null;
    return resolve(readRoom());
  }
  function stop(player, ticket) {
    if (!selection || player !== selection.player || (ticket && ticket !== selection.ticket)) return false;
    selection.control?.destroy();
    revokeJoin();
    selection = null;
    return true;
  }
  function issueTicket(player, ticket) {
    if (!selection?.regionChoice || selection.ticketState) return null;
    const room = status(player, ticket);
    if (!room || (room.status !== 'found' && !acceptedQueue(selection))) return null;
    selection.ticketState = 'issued';
    return { body: { errorCode: 0, ticketId: ticket, userIds: [player] }, written: onWritten('ticket-created') };
  }
  function acceptedQueue(choice) {
    if (!choice?.pool?.admitted || choice.pool.prepared) return false;
    const receipt = readRoom();
    return poolLease(choice, receipt) && receipt.queuedChoice === choice.pool.id;
  }
  function feedback(choice, stage) {
    if (choice?.control && choice.feedback !== stage) {
      choice.feedback = stage;
      reportFeedback(choice, stage);
    }
  }
  function reportFeedback(choice, stage) {
    report({ event: 'local-feedback', stage, at: now(), run: choice.runId, choice: choice.choice,
      ...(stage === 'preparing' ? { requestId: choice.pool.id } : {}) });
  }
  function ticketPublisher(player, ticket, progress, send) {
    const choice = selection;
    let started = false, flushed = false, accepted = false;
    const current = () => choice && selection === choice && choice.player === player && choice.ticket === ticket;
    const publish = () => {
      if (!current() || !accepted || !flushed) return;
      const create = issueTicket(player, ticket);
      if (create) send(create);
    };
    return {
      start() {
        if (started || !current()) return;
        started = true;
        feedback(choice, 'preparing');
        progress(error => {
          if (!current()) return;
          report({ event: error ? 'progress-write-failed' : 'progress-written', choice: choice.choice, mode: choice.mode });
          if (error) {
            feedback(choice, 'write-failed');
            stop(player, ticket);
          } else {
            flushed = true;
            publish();
          }
        });
      },
      ready() { accepted = true; publish(); },
      admitted() { accepted = true; publish(); },
    };
  }
  function onWritten(event) {
    const original = selection;
    return error => {
      if (error) {
        if (selection === original && event === 'ticket-created') {
          feedback(original, 'ticket-write-failed');
          stop(original.player, original.ticket);
        }
        return;
      }
      if (event === 'join-requested') original.commitWritten = true;
      report({ event, choice: original.choice, mode: original.mode });
    };
  }
  function revokeJoin() {
    if (!selection) return;
    selection.joinClosed = true;
    selection.revoke?.();
    selection.revoke = null;
  }
  function startHandoff(queried, rpc) {
    if (!rpc || selection !== queried || queried.joinClosed || queried.revoke || queried.ticketState !== 'issued') return;
    const receipt = readRoom();
    const ipc = advertisement(receipt, 'joinIpc');
    if (!ipc || resolve(receipt).status !== 'found') return;
    function valid() {
      if (selection !== queried || queried.ticketState !== 'issued' || queried.joinClosed) return false;
      const current = readRoom();
      const currentIpc = advertisement(current, 'joinIpc');
      return resolve(current).status === 'found' && currentIpc?.port === ipc.port && currentIpc.nonce === ipc.nonce;
    }
    queried.revoke = offerJoin(rpc, receipt, valid, onWritten('join-requested'));
  }
  function handleTicket(player, request, cancel, join) {
    // 控制端仍在准备时，原版还没有收到可随 Stop 回传的票号。
    if (cancel && selection?.pool && !selection.ticketState && selection.player === player &&
        request?.userId.length === 1 && request.userId[0] === player &&
        (!request.ticketId || request.ticketId === selection.ticket)) {
      feedback(selection, 'cancelled');
      stop(player, request.ticketId);
      return { cancelled: true };
    }
    if (!selection?.ticketState || selection.player !== player || request?.ticketId !== selection.ticket ||
        request.userId.length !== 1 || request.userId[0] !== player) return null;
    if (cancel) {
      feedback(selection, 'cancelled');
      stop(player, request.ticketId);
      return { cancelled: true };
    }
    if (selection.ticketState !== 'issued') return null;
    report({ event: 'ticket-queried', choice: selection.choice, mode: selection.mode });
    const room = status(player, request.ticketId);
    if (room?.status !== 'found' && !acceptedQueue(selection)) {
      selection.ticketState = 'revoked';
      return null;
    }
    const queried = selection;
    const waitingAtQuery = room.status !== 'found';
    feedback(queried, 'ticket-received');
    function assignment() {
      if (selection !== queried || selection.ticketState !== 'issued') return null;
      const current = status(player, request.ticketId);
      if (current?.status !== 'found') return null;
      const written = onWritten('endpoint-sent');
      return { body: { errorCode: 0, gameServerIP: current.serverIp + ':' + current.serverPort },
        written(error) {
          written(error);
          if (!error) startHandoff(queried, join);
        } };
    }
    return { assignment, dispatch(send) {
      const deliver = () => {
        if (selection !== queried || (waitingAtQuery && queried.queuedDeliverySent)) return;
        const delivery = assignment();
        if (!delivery) return;
        if (waitingAtQuery) {
          queried.queuedDeliverySent = true;
          queried.awaitAssignment = null;
        }
        send(delivery);
      };
      if (selection !== queried) return;
      if (waitingAtQuery && acceptedQueue(queried)) queried.awaitAssignment = deliver;
      else deliver();
    } };
  }
  function reset() {
    if (selection?.control && !selection.commitWritten) feedback(selection, 'prepare-ended');
    selection?.control?.destroy();
    revokeJoin();
    selection = null;
    localOwner = null;
    pendingReturn = null;
  }
  function presence(player, value) {
    if (pendingReturn?.player === player && value === 'InHall') {
      const room = readRoom();
      if (room?.autoPool === true && room.runId === pendingReturn.runId && freshRoom(room, now()))
        reportFeedback(pendingReturn, 'returned');
      pendingReturn = null;
      return;
    }
    if (selection?.pool && selection.player === player && value === 'InHall') {
      feedback(selection, 'returned');
      stop(player, selection.ticket);
      return;
    }
    if (selection?.player === player && value === 'InHall') revokeJoin();
    if (!selection?.room || selection.player !== player || !['InHall', 'InMatching', 'InMatch'].includes(value) ||
        selection.presence === value || resolve(readRoom()).status !== 'found') return;
    const returned = value === 'InHall' && ['InMatching', 'InMatch'].includes(selection.presence);
    selection.presence = value;
    report({ event: 'client-presence', choice: selection.choice, presence: value });
    if (returned && selection.ticketState) selection.ticketState = 'revoked';
  }
  return { start, status, stop, presence, reset, prepare, issueTicket, ticketPublisher, handleTicket,
    handlesTicket: player => Boolean(player && player === localOwner) };
}

module.exports = { createReadyRoomSession, nativeTicketProtocol, roomFileFor, roomReader, lobbyRoomReader, leaseMs };
