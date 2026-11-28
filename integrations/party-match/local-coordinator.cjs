'use strict';
const {spawn}=require('node:child_process');
const {createInterface}=require('node:readline');
const {once}=require('node:events');
const {createParty}=require('./tickets.cjs'),{captainMenu,partyReplies}=require('./native.cjs');
const {catalog}=require('./catalog.cjs');
const assert=require('node:assert/strict');

function startOwned(exe,target,port,choice) {
  const child=spawn(exe,['--brt-party-room',target,choice.ticket,choice.mode,choice.map,String(port)],
    {windowsHide:true,stdio:['pipe','pipe','pipe']});
  const finished=once(child,'close');
  child.stdin.on('error',()=>{});child.stderr.resume();
  const reader=createInterface({input:child.stdout})[Symbol.asyncIterator]();
  let closing=false,ready=false,chain=Promise.resolve();
  const receipt=(async()=>{
    const first=await reader.next();if(first.done)throw Error('Game room did not start');
    const row=JSON.parse(first.value);
    assert.deepEqual(row,{v:1,ticket:choice.ticket,mode:choice.mode,map:choice.map,port});
    ready=true;return row;
  })();
  const live=()=>!closing&&ready&&child.exitCode===null&&child.signalCode===null;
  return {receipt,live,async check(){
    const turn=chain.then(async()=>{if(!live())return false;child.stdin.write('live\n');
      const reply=await reader.next();return live() && !reply.done && reply.value==='live';});
    chain=turn.catch(()=>{});return turn;
  },close(){if(!closing){closing=true;child.stdin.end();}return finished;}};
}
// 每个席位来自本机父进程创建的独立档案进程。账号和席位不可通过网络消息指定。
// 与 BRT 已支持的专服启动组合取交集，不能仅凭零售大厅有卡片就开房。
function supportedRooms() {
  return new Set(['Rush/Warehouse','Rush_PVE_Easy/Warehouse','Rush_PVE_Easy/DataCenter',
    'Rush_PVE_Normal/MiniFarm','Rush_PVE_Normal/CircularX','Rush_PVE_Normal/OSS',
    'Rush_PVE_Hard/OSS','Rush_PVE_Hard/CircularX']);
}
function coordinator({children,accounts,table,capabilities=supportedRooms(),exe,target,port}) {
  if(children.length!==3||accounts.length!==3||new Set(accounts).size!==3)throw Error('Three distinct seats required');
  const pairs=catalog(table,capabilities);
  let party,active=null,closed=false;
  const members=children.map((child,i)=>({child,account:accounts[i],session:null}));
  const captain=members[0],owned=new Set();
  function leave(m){
    if(!m.session)return;
    // 已提交后队长先旅行会断开大厅 RPC，其他成员仍可领取同一局的地址。
    if(party?.snapshot(m).state==='committed'){m.session=null;m.travelled=true;return;}
    if(party)try{party.leave(m);}catch{}
    m.session=null;
    if(m===captain){party=null;for(const p of members)p.session=null;}
    if(active && (!party || party.snapshot(captain).state==='cancelled')){active.close();active=null;}
  }
  async function processCall(m,msg) {
    if(closed)throw Error('Coordinator closed');
    if(msg.op==='leave'){if(m.session===msg.session)leave(m);return;}
    if(m.account!==msg.account)throw Error('Seat account mismatch');
    if(msg.op==='create'){
      if(!party && m===captain)party=createParty({captain,account:captain.account,pairs,capacity:3});
      if(!party || m.travelled || (m.session && m.session!==msg.session))throw Error('Seat unavailable');
      if(m!==captain)party.enroll(m,m.account);
      m.session=msg.session;return partyReplies(party,m).create();
    }
    if(!party || m.session!==msg.session)throw Error('Not enrolled');
    const replies=partyReplies(party,m);
    switch(msg.op){
      case 'ready':return replies.ready({PartyId:msg.body});
      case 'set':return replies.setPresence({Presence:msg.body});
      case 'presence':return replies.presence();
      case 'cards':{
        const snap=party.snapshot(m);
        if(m===captain)return captainMenu(party,m,pairs).list();
        return snap.state!=='waiting'?[]:[require('./native.cjs').memberMenu({view:()=>snap}).list()[0]];
      }
      case 'start':{
        if(!members.every(p=>p.session))throw Error('Three seats required');
        if(m!==captain){const state=party.snapshot(m);
          if(state.state!=='waiting' || msg.body!=='BRT_PARTY_Confirm_'+state.ticket)throw Error('Wrong confirmation');
          return party.acknowledge(m,state.ticket);
        }
        const state=captainMenu(party,m,pairs).click(msg.body).selection;
        if(!state)throw Error('Unsupported selection');
        const currentParty=party;
        if(active)await active.close();
        if(party!==currentParty||party.snapshot(m).state!=='waiting')throw Error('Cancelled selection');
        const room=startOwned(exe,target,port,state);active=room;owned.add(room);
        room.receipt.then(()=>{
          if(closed||active!==room||party!==currentParty||!party.prepared(state.ticket,{mode:state.mode,map:state.map,live:room.live}))return room.close();
        }).catch(()=>{if(party===currentParty)try{party.stop(captain,state.ticket);}catch{}room.close();});
        return state;
      }
      case 'stop':{party.stop(m,msg.body);if(active){active.close();}return null;}
      case 'query':{
        const room=active,source=party;
        if(!room || party.snapshot(m).state!=='committed')return null;
        source.roomFor(m,msg.body);
        if(!await room.check() || room!==active || source!==party || m.session!==msg.session)return null;
        source.roomFor(m,msg.body);return {port};
      }
      default:throw Error('Unknown local operation');
    }
  }
  for(const m of members){
    m.child.on('message',async msg=>{
      if(msg?.brtParty!==1)return;
      let result;try{result={ok:true,value:await processCall(m,msg)};}catch{result={ok:false};}
      if(m.child.connected)m.child.send({brtParty:1,session:msg.session,id:msg.id,...result},()=>{});
    });
    m.child.once('disconnect',()=>leave(m));
  }
  return {async ready(){if(!active)throw Error('No room');return active.receipt;},
    async departed(i){const until=Date.now()+3000;while(!members[i]?.travelled){
      if(Date.now()>=until)throw Error('Lobby did not depart after assignment');
      await new Promise(r=>setTimeout(r,10));}},
    async close(){closed=true;await Promise.allSettled([...owned].map(room=>room.close()));}};
}
module.exports={coordinator,supportedRooms};
