'use strict';
const {randomUUID}=require('node:crypto');
const path=require('node:path');
const party='/party.party/', match='/matchmaking.Matchmaking/';
const endpoints=new Map([
  [party+'Create',['create',null,'CreatePartyResponse']],
  [party+'Ready',['ready','PartyReadyRequest','PartyReadyResponse']],
  [party+'SetPresence',['set','SetPartyPresenceRequest','SetPartyPresenceResponse']],
  [party+'QueryPresence',['presence',null,'QueryPartyPresenceResponse']],
  [match+'QueryPlayList',['cards',null,'QueryPlaylistResponse']],
  [match+'StartUnityMatchmaking',['start','StartMatchmakingRequest','StartMatchmakingResponse']],
  [match+'QueryUnityMatchmaking',['query',null,null]],
  [match+'StopUnityMatchmaking',['stop',null,null]],
]);
const loopback=s=>s==='127.0.0.1'||s==='::ffff:127.0.0.1';
const types=new Map();
function attach(socket,pb) {
  // 只接受显式启动的本机大厅与继承的父进程 IPC；不在玩家机器上开控制端口。
  if (process.env.BRT_LOCAL_PARTY_SEAT!=='1' || typeof process.send!=='function' ||
      !loopback(socket.localAddress)||!loopback(socket.remoteAddress)) return null;
  const ticketType=pb.parse('syntax="proto3"; message Ticket { repeated string userId=1; string ticketId=2; }').root.lookupType('Ticket');
  const type=(dir,name)=>{
    const p=path.join(__dirname,'game','proto',dir,name+'.proto');
    if (!types.has(p)) types.set(p,pb.loadSync(p).lookupType('ProjectBoundary.'+name));
    return types.get(p);
  };
  const waiting=new Map();
  let session=randomUUID(), enrolled=false, account=null, issued=null, delivered=null;
  function notify(op,body,id) {
    if (!process.connected) throw Error('Party coordinator disconnected');
    process.send({brtParty:1,session,id,op,body,account:socket.playerId});
  }
  function request(op,body) {
    if(waiting.size>=8) throw Error('Too many pending lobby calls');
    const id=randomUUID();
    return new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{waiting.delete(id);reject(Error('Party coordinator timeout'));},4000);
      waiting.set(id,v=>{clearTimeout(timer);v?.ok?resolve(v.value):reject(Error('Party call declined'));});
      try {notify(op,body,id);} catch {waiting.get(id)(null);waiting.delete(id);}
    });
  }
  function onReply(msg) {
    if(msg?.brtParty!==1 || msg.session!==session) return;
    const finish=waiting.get(msg.id);if(finish){waiting.delete(msg.id);finish(msg);}
  }
  process.on('message',onReply);
  function reset() {
    if(enrolled || waiting.size) try {notify('leave',null,randomUUID());} catch {}
    for(const finish of waiting.values()) finish(null);
    waiting.clear(); session=randomUUID();enrolled=false;issued=null;delivered=null;account=null;
  }
  socket.once('close',()=>{reset();process.off('message',onReply);});
  return {handles:r=>endpoints.has(r),reset,
    async dispatch(route,bytes,base,respond,push) {
      const generation=session;
      const [op,req,res]=endpoints.get(route);
      const payload=v=>res?type('Response',res).encode(v).finish():Buffer.alloc(0);
      let replied=false;
      const write=v=>{if(!socket.destroyed && generation===session){respond(payload(v));replied=true;}};
      try {
        if(!socket.joinAuthenticated || !/^\d{17}$/.test(socket.playerId)) throw Error('Local lobby not bound');
        const body=req?type('Request',req).decode(bytes):null;
        if(op==='create') {
          const result=await request(op,null);
          if(session!==generation || socket.destroyed)return;
          enrolled=true;account=socket.playerId;write(result);return;
        }
        if(!enrolled || account!==socket.playerId) throw Error('Party not enrolled');
        if(op==='cards') {const cards=await request(op,null);write({StatusCode:0,PlaylistsJSON:JSON.stringify({...base,PVE:cards})});return;}
        if(op==='start') {
          if(body.Payload?.MatchmakingRequestorUserId!==account) throw Error('Wrong lobby');
          const state=await request(op,body.GameMode);
          if(session!==generation||socket.destroyed)return;
          write({StatusCode:0});
          if(issued!==state.ticket){issued=state.ticket;delivered=null;push('StartCreateTicketServerCall',null);
            push('CreateTicketServerCall',{errorCode:0,ticketId:issued,userIds:[account]});}
          return;
        }
        if(op==='query'||op==='stop') {
          const q=ticketType.decode(bytes);
          if(!issued||q.ticketId!==issued||q.userId.length!==1||q.userId[0]!==account)throw Error('Wrong ticket');
          const target=await request(op,issued);write({});
          if(session!==generation||socket.destroyed||issued!==q.ticketId)return;
          if(op==='stop'){issued=null;return;}
          if(target && delivered!==issued){
            if(!Number.isInteger(target.port)||target.port<1024||target.port>65535)throw Error('Invalid local room');
            delivered=issued;push('QueryTicketServerCall',{errorCode:0,gameServerIP:'127.0.0.1:'+target.port});
          }
          return;
        }
        write(await request(op,op==='ready'?body.PartyId:op==='set'?body.Presence:null));
      } catch { if(!replied)write({StatusCode:1}); }
    }
  };
}
module.exports={attach};
