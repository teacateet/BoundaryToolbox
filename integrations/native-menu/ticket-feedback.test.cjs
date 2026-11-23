'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { createReadyRoomSession, lobbyRoomReader } = require('./ready-room.cjs');
const { pvePlan } = require('./native-menu.cjs');
const Rows = Object.fromEntries(['Easy','Normal','Hard'].map(k => ['Rush_PVE_'+k, {
  MapsForMatch:[{ RowName:'MiniFarm' }], MatchmakingString:k, GameModeName:{SourceString:k},
  GameModeSimpleDescreiption:{SourceString:k},
}]));
const table = [{ Name:'DT_GameModeDefinition', Rows }];
function fixture() {
  let now=1, receipt={v:1, runId:'room1', mode:'pve', offline:true, difficulty:'normal', port:32556,
    expiresAt:10001, map:'MiniFarm', gameClass:pvePlan(table,'Normal','MiniFarm').gameClass};
  const events=[], frames=[];
  const session=createReadyRoomSession(()=>table,()=>receipt,{now:()=>now,report:e=>events.push(e.event)});
  function start(ticket) {
    const selected=session.start('alice','alice','Normal',ticket,1,{ids:['region'],published:[{RegionId:'region'}]});
    let callback;
    const sender=session.ticketPublisher('alice',ticket,cb=>{frames.push('progress');callback=cb;},item=>{frames.push(item.body.ticketId);item.written();});
    return {selected,sender,complete:err=>callback(err)};
  }
  return {session,events,frames,start,tick:()=>now+=9999,renew:()=>receipt.expiresAt=now+10000,
    remote:()=>receipt=undefined};
}
test('room ready while progress write is pending: require write success, issue once',()=>{
  const f=fixture(), {sender,complete}=f.start('ticket');
  sender.start();sender.start();sender.ready();
  assert.deepEqual(f.frames,['progress']);complete();sender.ready();
  assert.deepEqual(f.frames,['progress','ticket']);
  assert.deepEqual(f.events.slice(-2),['progress-written','ticket-created']);
});
test('failed progress write cannot be followed by a success ticket',()=>{
  const f=fixture(), {sender,complete}=f.start('a');
  sender.start();sender.ready();complete(new Error('closed'));sender.ready();
  assert.deepEqual(f.frames,['progress']);assert.equal(f.session.status('alice','a'),null);
  assert.ok(f.events.includes('progress-write-failed'));
});
test('failed ticket write revokes only its own selection',()=>{
  for(const replaced of [false,true]) {
    const f=fixture(); f.start('old');
    const issued=f.session.issueTicket('alice','old');
    if(replaced) f.start('new');
    issued.written(new Error('write failed'));
    assert.equal(f.session.status('alice','old'),null);
    if(replaced) assert.equal(f.session.status('alice','new').status,'found');
  }
});
test('cancel, replace, expire or renew during an outstanding write',()=>{
  for(const change of ['cancel','replace','expire','renew']){
    const f=fixture(), {sender,complete}=f.start('old');
    sender.start();sender.ready();
    if(change==='cancel') f.session.stop('alice','old');
    if(change==='replace') f.start('new');
    if(change==='renew'||change==='expire'){f.tick();if(change==='renew')f.renew();f.tick();}
    complete();
    assert.deepEqual(f.frames,change==='renew'?['progress','old']:['progress'],change);
  }
});
test('no local lease: non-local matching emits neither local progress nor a ticket',()=>{
  const f=fixture();f.remote();const {sender,selected}=f.start('a');assert.equal(selected,null);
  sender.start();sender.ready();assert.deepEqual(f.frames,[]);
});
test('LAN and mixed address RPC cannot consume even a fresh local room',()=>{
  let reads=0;
  for(const [localAddress,remoteAddress,allowed] of [
    ['127.0.0.1','127.0.0.1',true],['::ffff:127.0.0.1','::ffff:127.0.0.1',true],
    ['192.168.1.2','192.168.1.3',false],['127.0.0.1','192.168.1.3',false],
    ['192.168.1.2','127.0.0.1',false],
  ]) {
    const read=lobbyRoomReader({localAddress,remoteAddress},()=>{reads++;return 'local';});
    assert.equal(read(),allowed?'local':undefined);
  }
  assert.equal(reads,2);
});
