'use strict';
const {selectorCards}=require('./catalog.cjs');
const confirm=tk=>'BRT_PARTY_Confirm_'+tk;
const nonzero=()=>({StatusCode:1});
function card(id,title,priority) {
  const labels=[{zh:title},{en:title}];
  return {Name:id,Title:labels,BigTitle:labels,Description:labels,BigDescription:labels,SecondaryDescription:[],
    PlotImage:[],LargePlotImage:[],GameModeList:[id],bHasFilter:false,bIsLive:true,Priority:priority,StartTime:0,StopTime:0};
}
// 只在本机已向好友队伍主动登记并完成账号绑定的大厅会话使用。
// 服务已经识别的 Create、Ready、QueryPresence 消息不需要实现猜测中的 party_ext 字段。
function partyReplies(party,peer) {
  const roster=()=>party.roster(peer);
  return {
    create:()=>({StatusCode:0,PartyId:party.id,PartyMembers:roster()}),
    ready:({PartyId})=>{roster();return PartyId===party.id?{StatusCode:0}:nonzero();},
    presence:()=>({StatusCode:0,PartyMembers:party.presence(peer)}),
    setPresence:({Presence})=>{try {party.setPresence(peer,Presence);return {StatusCode:0};} catch {return nonzero();}}
  };
}
function captainMenu(party,peer,pairs) {
  const cards=selectorCards(pairs),lookup=new Map(cards.map(c=>[c.id,c]));
  return {
    list(){const s=party.snapshot(peer);return s.state==='idle' || s.state==='cancelled'
      ? cards.map((c,i)=>card(c.id,c.label,-1000+i)) : [];},
    click(id) {
      const c=lookup.get(id);if (!c) return nonzero();
      try {return {StatusCode:0,selection:party.start(peer,c.mode,c.map)};} catch {return nonzero();}
    }
  };
}
function memberMenu(replica,send) {
  return {
    list() {const s=replica.view();return s?.state==='waiting' ?
      [card(confirm(s.ticket),'参加队长选择的下一局：'+s.map+' · '+s.mode,-1000)]:[];},
    click(id) {
      const s=replica.view();if (s?.state!=='waiting' || id!==confirm(s.ticket)) return nonzero();
      send(replica.accept(s.ticket));return {StatusCode:0};
    },
    stop(tk) {try {send(replica.cancel(tk));return {StatusCode:0};} catch {return nonzero();}}
  };
}
module.exports={partyReplies,captainMenu,memberMenu};
