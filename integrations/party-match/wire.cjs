'use strict';
const {slug}=require('./catalog.cjs');
const prefix='BRTQ1|', uuid=s=>typeof s==='string' && /^[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}$/.test(s);
const phases=new Set(['waiting','cancelled','committed']);
const fail=()=>{throw Error('Invalid party frame');};
function fields(packet) {
  if (!Buffer.isBuffer(packet) || packet.length<10 || packet.length>192 ||
      packet.some(byte=>byte<0x21 || byte>0x7e)) fail();
  const s=packet.toString('ascii');if (!s.startsWith(prefix)) fail();
  const data=s.slice(prefix.length).split('|');if (!uuid(data[1])) fail();return data;
}
function pack(...data) {const p=Buffer.from(prefix+data.join('|'),'ascii');fields(p);return p;}
function command(op,party,ticket) {
  if (op==='Q' && ticket===undefined) return pack(op,party);
  if ((op==='A' || op==='C') && uuid(ticket)) return pack(op,party,ticket);
  return fail();
}
function reply(s) {
  if (!s || !uuid(s.party) || !Number.isSafeInteger(s.rev) || s.rev<0) fail();
  if (s.state==='idle') return pack('S',s.party,s.rev,'idle');
  if (!phases.has(s.state) || !uuid(s.ticket) || !slug(s.mode) || !slug(s.map) || typeof s.ack!=='boolean') fail();
  return pack('S',s.party,s.rev,s.state,s.ticket,s.mode,s.map,s.ack?'1':'0');
}
// 来自组网的 peer 须是已完成加入确认的当前会话；本函数不从数据报解析身份。
function handle(party,peer,packet) {
  try {
    const [op,id,ticket,...tail]=fields(packet);
    if (id!==party.id || tail.length) return null;
    if (op==='Q' && ticket===undefined) return reply(party.snapshot(peer));
    if (!uuid(ticket)) return null;
    if (op==='A') return reply(party.acknowledge(peer,ticket));
    if (op==='C') return reply(party.stop(peer,ticket));
  } catch {}
  return null;
}
function readReply(packet,id) {
  const f=fields(packet);
  if (f[0]!=='S' || id!==f[1] || !/^(0|[1-9][0-9]{0,14})$/.test(f[2])) fail();
  const rev=Number(f[2]),state=f[3];
  if (state==='idle' && f.length===4) return {party:id,rev,state};
  if (!phases.has(state) || f.length!==8 || !uuid(f[4]) || !slug(f[5]) || !slug(f[6]) ||
      !['0','1'].includes(f[7])) fail();
  return {party:id,rev,state,ticket:f[4],mode:f[5],map:f[6],ack:f[7]==='1'};
}
// 已认证的房主才能送入回包。最新状态可重复查询，不会重复启动旅行。
function guest(id,allowed) {
  if (!uuid(id) || !(allowed instanceof Set)) fail();
  let current=null,consent=null,delivered=null;
  return {
    receive(packet) {
      const next=readReply(packet,id);
      if (next.state!=='idle' && !allowed.has(next.mode+'/'+next.map)) fail();
      if (current && next.rev<=current.rev) return false;
      if (current?.ticket!==next.ticket || next.state==='cancelled' || next.state==='idle') consent=null;
      if (current?.ticket && next.ticket===current.ticket &&
          (next.mode!==current.mode || next.map!==current.map)) fail();
      current=Object.freeze(next);return true;
    },
    view(){return current;},
    query(){return command('Q',id);},
    accept(tk) {
      if (!current || current.ticket!==tk || current.state!=='waiting') fail();
      consent=tk;return command('A',id,tk);
    },
    cancel(tk) {
      if (!current || current.ticket!==tk || current.state!=='waiting') fail();
      consent=null;return command('C',id,tk);
    },
    travel(tk,joinExistingShare) {
      if (!current || current.ticket!==tk || current.state!=='committed' || !current.ack ||
          consent!==tk || delivered===tk) return false;
      // 上层在此同步验证同一房主、同一开局和已开启的游戏端口，不使用远端提供的地址。
      if (!joinExistingShare(Object.freeze({party:id,ticket:tk,mode:current.mode,map:current.map}))) return false;
      delivered=tk;return true;
    }
  };
}
module.exports={prefix,command,reply,handle,readReply,guest};
