'use strict';
const assert = require('node:assert/strict');
const { createHmac, createHash, randomBytes, randomUUID } = require('node:crypto');
const path = require('node:path');
const test = require('node:test');
const {installRoomSpawn} = require('./room-spawn.cjs');
const catalog = require('./coverage/data/catalog.json');
const modules = process.env.PROFILE_TEST_MODULES ||
    'C:/BrtWork/SteamLibrary/steamapps/common/Boundary/ProjectBoundary/Binaries/Win64/BoundaryMetaServer-main/node_modules';
const pb = require(path.join(modules,'protobufjs'));

test('原生对局装备目录只供已签名的当前本地房主；过期、冒名、异常正文与 LAN 不发数据', () => {
    const owner='76561198185503627', other='76561198112699624';
    let reads=0, live=true;
    const store = {getRoleDefinition(id) {
        reads++;
        const def=catalog.roles[id], scope={};
        for (const key of ['podScope','meleeWeaponScope','mobilityScope','weaponScope']) scope[key]=new Set(def[key]);
        return {role:scope};
    }};
    const key=randomBytes(32), id=randomUUID(), run=randomUUID();
    const verifier=createHash('sha256').update(randomBytes(32)).digest('hex');
    const signature=createHmac('sha256',key).update(['brt-local-room-v1',run,id,owner,verifier].join('\n')).digest('hex');
    const register=installRoomSpawn({post(){}},()=>store,()=>{throw Error('catalog does not read weapons');},null,key.toString('hex'));
    const socket={joinAuthenticated:true,playerId:owner,localAddress:'127.0.0.1',remoteAddress:'::ffff:127.0.0.1'};
    const req=who=>pb.Writer.create().uint32(10).string(who).finish();
    const read=(s=socket,body=req(owner))=>register.matchCatalog(s,body,pb);
    assert.equal(read(),null);
    assert.equal(register(id,owner,{owner,verifier,signature:'0'.repeat(64)},run,()=>live),false);
    assert.equal(read(),null);
    assert.equal(register(id,owner,{owner,verifier,signature},run,()=>live),true);
    assert.equal(read({...socket,playerId:null}),null);
    assert.equal(read({...socket,joinAuthenticated:false}),null);
    assert.equal(read({...socket,playerId:other},req(other)),null);
    assert.equal(read(socket,req(other)),null);
    assert.equal(read({...socket,remoteAddress:'192.168.10.4'}),null);
    assert.equal(read({...socket,localAddress:'192.168.10.1'}),null);
    for (const malformed of [Buffer.alloc(0),Buffer.from([0x0a,0x40,1]),Buffer.alloc(129)]) assert.equal(read(socket,malformed),null);
    assert.equal(reads,0);
    const verified=read();
    assert.ok(Buffer.isBuffer(verified) && verified.length>0);
    assert.equal(reads,6);
    // 结束房间后就算绑定连接和请求的账号仍一致也不可读；不需要任何档案写方法。
    live=false;
    assert.equal(read(),null);
    assert.equal(reads,6);
});
