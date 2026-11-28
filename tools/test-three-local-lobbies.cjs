'use strict';
// node tools/test-three-local-lobbies.cjs <BRT.exe> <隔离安装 Win64> <隔离档案源> <验收输出目录>
const fs=require('node:fs'),path=require('node:path'),net=require('node:net'),dgram=require('node:dgram');
const {fork}=require('node:child_process'),{once}=require('node:events'),{randomUUID}=require('node:crypto');
const assert=require('node:assert/strict');
const {coordinator}=require('../integrations/party-match/local-coordinator.cjs');
const {withLogin}=require('../integrations/native-menu/test-lobby-client.cjs');
const sleep=n=>new Promise(resolve=>setTimeout(resolve,n));
async function freePorts(){
  const holds=[];
  try {
    for(let i=0;i<9;i++){const s=net.createServer();s.listen(0,'127.0.0.1');await once(s,'listening');holds.push(s);}
    const p=holds.map(s=>s.address().port);
    return [0,3,6].map(i=>({http:p[i],rpc:p[i+1],mm:p[i+2]}));
  }finally{await Promise.all(holds.map(s=>new Promise(r=>s.close(r))));}
}
async function main(){
  const [exe,target,source,out]=process.argv.slice(2).map(p=>path.resolve(p));
  const q=path.join(out,randomUUID());fs.mkdirSync(q,{recursive:true});
  const kit=path.join(q,'kit');fs.mkdirSync(kit);
  const root=path.resolve(__dirname,'..'),registry=fs.readFileSync(path.join(root,'src/core/local_profile.rs'),'utf8');
  for(const [,rel] of registry.matchAll(/include_bytes!\("\.\.\/\.\.\/integrations\/([\w./-]+)"\)/g))
    fs.copyFileSync(path.join(root,'integrations',rel),path.join(kit,path.basename(rel)));
  fs.mkdirSync(path.join(q,'profiles'));
  const app=require(path.join(kit,'prepare.cjs')).prepare(source,path.join(q,'profiles'),kit).appDir;
  assert(app,'Unsupported profile source');
  const accounts=['76561000000000001','76561000000000002','76561000000000003'],ports=await freePorts();
  const socket=dgram.createSocket('udp4');socket.bind(0,'127.0.0.1');await once(socket,'listening');
  const port=socket.address().port;await new Promise(r=>socket.close(r));
  const children=[],closes=[],logFds=[];
  let hub;
  try{
    for(const [i,p] of ports.entries()){
      const log=fs.openSync(path.join(q,'lobby-'+i+'.log'),'wx');logFds.push(log);
      const child=fork(path.join(app,'index.js'),[],{cwd:app,windowsHide:true,stdio:['ignore',log,log,'ipc'],
        env:{...process.env,PORT:String(p.http),RPC_PORT:String(p.rpc),MATCHMAKING_PORT:String(p.mm),BRT_LOCAL_PARTY_SEAT:'1'}});
      children.push(child);closes.push(once(child,'close'));
    }
    const table=require(path.join(app,'game/definitions/DT_GameModeDefinition.json'));
    hub=coordinator({children,accounts,table,capabilities:new Set(['Rush_PVE_Normal/OSS']),exe,target,port});
    for(const [i,p] of ports.entries()){
      let ok=false;for(let k=0;k<100;k++){if(children[i].exitCode!==null)throw Error('Lobby failed: '+q);
        try{await fetch('http://127.0.0.1:'+p.http+'/',{signal:AbortSignal.timeout(600)});ok=true;break;}catch{await sleep(100);}}
      assert(ok,'Lobby timed out: '+q);
    }
    const seats=[];
    const collect=i=>withLogin(app,ports[i],accounts[i],async(rpc,type,_rebind,push,_pipeline,_port,depart)=>{
      const mm='/matchmaking.Matchmaking/';
      async function create(){return type('Response','CreatePartyResponse').decode(await rpc('/party.party/Create',Buffer.alloc(0)));}
      async function cards(){return JSON.parse(type('Response','QueryPlaylistResponse').decode(await rpc(mm+'QueryPlayList',Buffer.alloc(0))).PlaylistsJSON).PVE;}
      async function click(card){
        const bytes=type('Request','StartMatchmakingRequest').encode({Payload:{MatchmakingRequestorUserId:accounts[i]},GameMode:card}).finish();
        assert.equal(type('Response','StartMatchmakingResponse').decode(await rpc(mm+'StartUnityMatchmaking',bytes)).StatusCode,0);
        await push('StartCreateTicketServerCall');const got=JSON.parse(await push('CreateTicketServerCall'));
        assert.deepEqual(got.userIds,[accounts[i]]);return got.ticketId;
      }
      async function query(t,send=true){
        const pb=require(path.join(app,'node_modules/protobufjs'));
        await rpc(mm+'QueryUnityMatchmaking',pb.Writer.create().uint32(10).string(accounts[i]).uint32(18).string(t).finish());
        return send?JSON.parse(await push('QueryTicketServerCall')).gameServerIP:null;
      }
      seats.push({create,cards,click,query,depart});const seen=await create();assert.equal(seen.StatusCode,0);
      if(i<2)return collect(i+1);
      const ids=new Set();for(const seat of seats){const party=await seat.create();ids.add(party.PartyId);assert.deepEqual(party.PartyMembers,accounts);}
      assert.equal(ids.size,1);
      const row=(await seats[0].cards()).find(c=>c.Name==='BRT_PARTY_Pair_Rush_PVE_Normal_OSS');assert(row);
      const tk=await seats[0].click(row.Name);
      for(const idx of [1,2]){
        const [card]=await seats[idx].cards();assert.equal(card.Name,'BRT_PARTY_Confirm_'+tk);
        if(idx===2)await seats[0].query(tk,false);
        assert.equal(await seats[idx].click(card.Name),tk);
      }
      const confirmed=await hub.ready();assert.equal(confirmed.ticket,tk);
      const first=await seats[0].query(tk);
      seats[0].depart();
      await hub.departed(0);
      const arrivals=[first,...await Promise.all(seats.slice(1).map(s=>s.query(tk)))];
      assert.deepEqual(arrivals,Array(3).fill('127.0.0.1:'+port));
      return {lobbyProcesses:3,fixtureAccounts:3,retailPlayers:0,partyIds:ids.size,tickets:1,mode:confirmed.mode,map:confirmed.map,
        oneWindowsGameEndpoint:true,gamePort:port,earlyAssignment:false,captainLobbyClosedBeforeGuestDelivery:true};
    },undefined,125000);
    const proof=await collect(0);
    assert.equal(proof?.lobbyProcesses,3);
    await hub.close();
    const reuse=dgram.createSocket('udp4');try{reuse.bind(port,'127.0.0.1');await once(reuse,'listening');}finally{reuse.close();}
    fs.writeFileSync(path.join(q,'receipt.json'),JSON.stringify({...proof,portReleased:true},null,2));
    console.log('PASS '+path.join(q,'receipt.json'));
  }finally{
    if(hub)await hub.close();
    for(const child of children)if(child.exitCode===null)child.kill();await Promise.allSettled(closes);
    for(const log of logFds)fs.closeSync(log);
  }
}
main().catch(e=>{console.error(e);process.exitCode=1;});
