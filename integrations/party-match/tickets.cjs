'use strict';
const {randomUUID} = require('node:crypto');
const {select} = require('./catalog.cjs');
const requireValue = (x,message) => { if (!x) throw Error(message); };
const accountOK = id => typeof id === 'string' && /^[0-9]{17}$/.test(id);

// member 是由宿主在本机登录和主动入队成功后绑定的会话句柄，网络正文不能指定成员。
// room 由持有游戏进程与端口的本机所有者提供；不接收远端地址、路径或命令。
function createParty({captain, account, pairs, now=Date.now, ttl=120000, capacity=10}) {
  requireValue(captain && accountOK(account) && pairs.length && Number.isInteger(capacity) &&
    capacity >= 2 && capacity <= 32 && Number.isInteger(ttl) && ttl>0 && ttl<=300000, 'Invalid party');
  const id=randomUUID(), members=new Map([[captain,account]]);
  const presence=new Map([[captain,'InHall']]);
  let ticket=null, revision=0, closed=false;
  function cancel() {
    if (!ticket || ticket.state==='cancelled' || ticket.state==='committed') return;
    ticket.state='cancelled'; ticket.room=null; ticket.acks.clear(); ++revision;
  }
  function refresh() {
    if (ticket && ticket.state!=='cancelled' && ticket.state!=='committed' && now()>=ticket.until) cancel();
  }
  function member(peer) { requireValue(!closed && members.has(peer),'Not a party member'); refresh(); }
  function snapshot(peer) {
    member(peer);
    if (!ticket) return {party:id,rev:revision,state:'idle'};
    const {id:tk,state,mode,map}=ticket;
    return {party:id,rev:revision,state,ticket:tk,mode,map,ack:ticket.acks.has(peer)};
  }
  function finish() {
    if (ticket.state!=='waiting' || !ticket.room || ticket.acks.size!==ticket.roster.size) return;
    if (!ticket.room.live()) { cancel(); return; }
    ticket.state='committed'; ++revision;
  }
  return Object.freeze({
    id,snapshot,
    roster(peer) {member(peer);return [...members.values()];},
    presence(peer) {member(peer);return [...members].map(([p,UserId])=>({UserId,Status:presence.get(p)}));},
    setPresence(peer,value) {
      member(peer);requireValue(['InHall','InMatching','InParty','InShowRoom','InGame'].includes(value),'Invalid presence');
      presence.set(peer,value);
    },
    enroll(peer,player) {
      requireValue(!closed && peer && accountOK(player),'Invalid enrollment');
      if (members.has(peer)) {requireValue(members.get(peer)===player,'Different login');return;}
      requireValue(members.size<capacity && ![...members.values()].includes(player),'Already enrolled or full');
      refresh(); requireValue(!ticket || ticket.state==='cancelled','Roster locked');
      members.set(peer,player);presence.set(peer,'InParty');++revision;
    },
    leave(peer) {
      member(peer);
      if (ticket?.roster.has(peer)) cancel();
      members.delete(peer);presence.delete(peer); ++revision;
      if (peer===captain) { closed=true;members.clear();presence.clear();ticket=null; }
    },
    start(peer,mode,map) {
      member(peer);requireValue(peer===captain && members.size>=2,'Captain and a teammate required');
      requireValue(!ticket || ticket.state==='cancelled','Finish or cancel current ticket first');
      const chosen=select(pairs,mode,map);
      ticket={id:randomUUID(),...chosen,state:'waiting',acks:new Set([peer]),roster:new Set(members.keys()),
        until:now()+ttl,room:null}; ++revision;return snapshot(peer);
    },
    acknowledge(peer,tk) {
      member(peer);requireValue(ticket?.id===tk && ticket.roster.has(peer) &&
        (ticket.state==='waiting' || ticket.state==='committed'),'Inactive ticket');
      if (!ticket.acks.has(peer)) {ticket.acks.add(peer);++revision;}
      finish();return snapshot(peer);
    },
    stop(peer,tk) {
      member(peer);requireValue(ticket?.id===tk && ticket.roster.has(peer) && ticket.state!=='committed','Cannot cancel');
      cancel();return snapshot(peer);
    },
    prepared(tk,room) {
      refresh();if (closed || !ticket || ticket.id!==tk || ticket.state!=='waiting') return false;
      if (!room || room.mode!==ticket.mode || room.map!==ticket.map || typeof room.live!=='function' ||
          !room.live() || (ticket.room && ticket.room!==room)) {cancel();return false;}
      ticket.room=room;finish();return true;
    },
    roomFor(peer,tk) {
      member(peer);
      requireValue(ticket?.id===tk && ticket.state==='committed' && ticket.roster.has(peer) &&
        ticket.acks.has(peer) && ticket.room.live(),'No live party assignment');
      return ticket.room;
    },
    retire(tk) {
      requireValue(ticket?.id===tk,'Wrong ticket');ticket=null;++revision;
    }
  });
}
module.exports={createParty};
