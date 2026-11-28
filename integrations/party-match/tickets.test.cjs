'use strict';
const test=require('node:test'), assert=require('node:assert/strict'), dgram=require('node:dgram');
const {catalog,selectorCards}=require('./catalog.cjs'),{createParty}=require('./tickets.cjs');
const {guest,command,reply,handle}=require('./wire.cjs'),{partyReplies,captainMenu,memberMenu}=require('./native.cjs');
const table=[{Name:'DT_GameModeDefinition',Rows:{
  Rush:{MapsForMatch:[{RowName:'warehouse'},{RowName:'OSS'}]},
  Rush_PVE_Normal:{MapsForMatch:[{RowName:'MiniFarm'},{RowName:'OSS'}]},
  QuickPlay:{MapsForMatch:[{RowName:'OSS'}]}
}}];
const allowed=new Set(['Rush/Warehouse','Rush_PVE_Normal/OSS']);
const pairs=catalog(table,allowed),accounts=['76561000000000001','76561000000000002','76561000000000003'];
function setup() {
  const peers=[{},{},{}],clock={ms:1000};
  const party=createParty({captain:peers[0],account:accounts[0],pairs,now:()=>clock.ms,ttl:5000});
  party.enroll(peers[1],accounts[1]);party.enroll(peers[2],accounts[2]);
  return {party,peers,clock,ui:captainMenu(party,peers[0],pairs)};
}
test('native capabilities restrict exact and random choices; only captain can start',()=>{
  const {party,peers,ui}=setup();
  assert.deepEqual(pairs,[{mode:'Rush',map:'Warehouse'},{mode:'Rush_PVE_Normal',map:'OSS'}]);
  assert(!selectorCards(pairs).some(c=>c.id.includes('QuickPlay')));
  assert.equal(ui.click('BRT_PARTY_Pair_Rush_OSS').StatusCode,1);
  assert.equal(captainMenu(party,peers[2],pairs).click('BRT_PARTY_Any').StatusCode,1);
  const state=ui.click('BRT_PARTY_Mode_Rush');
  assert.equal(state.StatusCode,0);assert.equal(state.selection.map,'Warehouse');
  assert.throws(()=>party.enroll({},'76561000000000004'),/locked/);
  assert.equal(ui.list().length,0);
});
test('a late room, a wrong room and a disconnected member cannot commit',()=>{
  const {party,peers,clock,ui}=setup();
  const s=ui.click('BRT_PARTY_Pair_Rush_Warehouse').selection;
  const room={mode:'Rush',map:'Warehouse',live:()=>true};
  assert.equal(party.prepared(s.ticket,{...room,map:'OSS'}),false);
  assert.equal(party.snapshot(peers[0]).state,'cancelled');
  const next=ui.click('BRT_PARTY_Any').selection;
  assert.equal(party.prepared(s.ticket,room),false);
  clock.ms+=5100;assert.equal(party.snapshot(peers[0]).state,'cancelled');
  const third=ui.click('BRT_PARTY_Pair_Rush_Warehouse').selection;
  party.acknowledge(peers[1],third.ticket);
  party.leave(peers[2]);assert.equal(party.prepared(third.ticket,room),false);
  assert.throws(()=>party.roomFor(peers[1],third.ticket));assert.equal(handle(party,peers[2],command('Q',party.id)),null);
  assert.equal(next.state,'waiting');assert.equal(party.snapshot(peers[0]).state,'cancelled');
});
test('native party response preserves one roster and distinct presence',()=>{
  const {party,peers}=setup(), r=peers.map(peer=>partyReplies(party,peer));
  for(const ui of r) {
    assert.deepEqual(ui.create(),{StatusCode:0,PartyId:party.id,PartyMembers:accounts});
    assert.equal(ui.ready({PartyId:party.id}).StatusCode,0);assert.equal(ui.ready({PartyId:'old'}).StatusCode,1);
  }
  assert.equal(r[1].setPresence({Presence:'InMatching'}).StatusCode,0);
  assert.deepEqual(r[2].presence().PartyMembers.map(m=>m.Status),['InHall','InMatching','InParty']);
});
test('no command frames can travel, select a host, carry extra fields or impersonate a peer',()=>{
  const {party,peers,ui}=setup(), {ticket,mode,map}=ui.click('BRT_PARTY_Any').selection, outsider={};
  assert.equal(handle(party,outsider,command('Q',party.id)),null);
  for(const text of ['Z|'+party.id,'A|'+party.id+'|'+ticket+'|'+accounts[2],
    'Q|'+party.id+'|127.0.0.1:8000','Q|'+party.id+'\x00','S|'+party.id+'|1|idle'])
    assert.equal(handle(party,peers[1],Buffer.from('BRTQ1|'+text)),null);
  assert.equal(handle(party,peers[1],Buffer.alloc(193)),null);
  const rep=guest(party.id,allowed);
  rep.receive(reply({party:party.id,rev:3,state:'committed',ticket,mode,map,ack:true}));
  assert.equal(rep.travel(ticket,()=>assert.fail('no local click')),false);
});

function bind(socket) {return new Promise((res,rej)=>{socket.once('error',rej);socket.bind(0,'127.0.0.1',()=>{
  socket.removeListener('error',rej);res(socket.address().port);});});}
// 主机和两个客户在三条不同的回环 UDP socket 上走实际的专用帧。宿主按预先批准的连接句柄识别成员。
test('three active participants: shared cancellable ticket then one owned game room, reordered UDP and duplicate delivery',async t=>{
  const {party,peers,ui}=setup(),host=dgram.createSocket('udp4'),clients=[dgram.createSocket('udp4'),dgram.createSocket('udp4')];
  t.after(()=>{host.close();for(const c of clients)c.close();});
  const ports=await Promise.all([bind(host),...clients.map(bind)]),peerByPort=new Map([[ports[1],peers[1]],[ports[2],peers[2]]]);
  host.on('message',(data,remote)=>{const bytes=handle(party,peerByPort.get(remote.port),data);if(bytes)host.send(bytes,remote.port,'127.0.0.1');});
  function exchange(i,bytes) {return new Promise((res,rej)=>{const c=clients[i],timer=setTimeout(()=>{c.off('message',done);rej(Error('Timeout'));},2000);
    function done(data,peer){if(peer.port!==ports[0])return;clearTimeout(timer);c.off('message',done);res(data);}
    c.on('message',done);c.send(bytes,ports[0],'127.0.0.1');});}
  const reps=clients.map(()=>guest(party.id,allowed)),commands=[[],[]];
  const menus=reps.map((r,i)=>memberMenu(r,cmd=>commands[i].push(cmd)));
  const poll=i=>exchange(i,reps[i].query()).then(w=>{reps[i].receive(w);return w;});
  const click=async i=>{const card=menus[i].list()[0];assert.equal(menus[i].click(card.Name).StatusCode,0);
    reps[i].receive(await exchange(i,commands[i].shift()));};
  const first=ui.click('BRT_PARTY_Pair_Rush_Warehouse').selection;
  const old=await poll(0);await poll(1);assert.equal(reps[1].view().ticket,first.ticket);
  await click(0);assert.equal(menus[1].stop(first.ticket).StatusCode,0);
  await exchange(1,commands[1].shift());await poll(0);await poll(1);
  assert.equal(reps[0].view().state,'cancelled');assert.equal(reps[0].receive(old),false);
  const s=ui.click('BRT_PARTY_Pair_Rush_PVE_Normal_OSS').selection;
  let up=true;const room=Object.freeze({mode:s.mode,map:s.map,live:()=>up});
  const stale=handle(party,peers[2],command('A',party.id,first.ticket));assert.equal(stale,null);
  await poll(0);await poll(1);await click(0);assert(party.prepared(s.ticket,room));
  assert.throws(()=>party.roomFor(peers[0],s.ticket));assert.equal(reps[1].travel(s.ticket,()=>assert.fail()),false);
  await click(1);await poll(0);await poll(1);
  assert.equal(party.roomFor(peers[0],s.ticket),room);
  const arrivals=[];for(const [i,r] of reps.entries()) {
    const join=()=>{arrivals.push(party.roomFor(peers[i+1],s.ticket));return true;};
    assert.equal(r.travel(s.ticket,join),true);await poll(i);assert.equal(r.travel(s.ticket,join),false);
  }
  assert.deepEqual(arrivals,[room,room]);up=false;assert.throws(()=>party.roomFor(peers[0],s.ticket));
});
