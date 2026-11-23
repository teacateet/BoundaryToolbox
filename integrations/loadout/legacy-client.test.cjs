'use strict';
const {test} = require('node:test'), assert = require('node:assert/strict');
const {EventEmitter} = require('node:events');
const {createClientReader} = require('./legacy-client.cjs');
const fs=require('node:fs'), path=require('node:path');
const {execFileSync}=require('node:child_process');
const {fixture}=require('./fixture.cjs'), {managed}=require('./fixture-kit.cjs');
const sample=require('./probe-minifarm-sample.json');
const fixed='76561198950613585', a='76561198088975659', b='76561198069447979';
const socket=(remote,local)=>Object.assign(new EventEmitter(),{remoteAddress:'127.0.0.1',localAddress:'127.0.0.1',remotePort:remote,localPort:local});
const request=(http)=>({method:'GET',get:()=> 'BoundaryDLL/1.0',params:{playerId:fixed},socket:http});
const bind=(router,s,id)=>{s.joinAuthenticated=true;s.playerId=id;router.bind(s);};

test('示例账号读取与所属进程和当前 Steam 账号绑定，换人或断线后不沿用旧证明',async()=>{
  let proof, unpause, seen;
  const router=createClientReader(async(http,rows)=>{seen={http,rows};return typeof proof==='function'?proof():proof;});
  const x=socket(50001,6969), y=socket(50002,6969), get=request(socket(50300,8000));
  router.watch(x);router.watch(y);
  bind(router,x,a);bind(router,y,b);
  proof={steam:a,matches:[0]};assert.equal(await router.owner(get),a);
  assert.deepEqual(seen,{http:[50300,8000],rows:[[50001,6969],[50002,6969]]});
  proof={steam:b,matches:[1]};assert.equal(await router.owner(get),b);
  for(const invalid of [{steam:a,matches:[0,1]}, {steam:b,matches:[0]}, {steam:b,matches:[]}, {steam:a,matches:[9]},{}]){
    proof=invalid;assert.equal(await router.owner(get),null);
  }
  proof=()=>new Promise(r=>{unpause=r});
  const pending=router.owner(get);
  bind(router,x,b);unpause({steam:a,matches:[0]});assert.equal(await pending,null);
  const closed=router.owner(get);y.emit('close');unpause({steam:b,matches:[1]});assert.equal(await closed,null);
  const concurrent=router.owner(get), z=socket(50003,6969);
  router.watch(z);bind(router,z,a);unpause({steam:b,matches:[0]});assert.equal(await concurrent,null);
  z.emit('close');
  bind(router,x,fixed);x.joinAuthenticated=false;router.bind(x);
  assert.equal(await router.owner(get),null);
  assert.equal(router.matches({...get,method:'PUT'}),false);
  assert.equal(router.matches({...get,get:()=> 'Browser'}),false);
  assert.equal(router.matches({...get,socket:{...get.socket,remoteAddress:'192.0.2.9'}}),false);
});

// 用例只改变预期的客户端映像：由当前 Node 测试进程正常登录并 GET，运行完整的 Windows 归属检查。
test('托管服务读出正确干员的整套配装，并拒绝混淆示例账号和普通读写', {
  skip:!process.env.BRT_QA_ARCHIVE_SOURCE || !process.env.BRT_QA_BRT_EXE, timeout:60000,
}, async()=>{
  const source=path.resolve(process.env.BRT_QA_ARCHIVE_SOURCE);
  const scratch=process.env.BRT_QA_SCRATCH || require('node:os').tmpdir();
  const reg=execFileSync('reg.exe',['query','HKCU\\Software\\Valve\\Steam\\ActiveProcess','/v','ActiveUser'],{encoding:'utf8',windowsHide:true});
  const current=/REG_DWORD\s+(0x[\da-f]+)/i.exec(reg);
  assert.ok(current && BigInt(current[1])>0n,'测试需已登录 Steam，不会读写玩家存档');
  const owner=String(76561197960265728n+BigInt(current[1]));
  const peer=owner===b?a:b;
  const helper=fs.realpathSync(process.env.BRT_QA_BRT_EXE);
  await fixture(source,async f=>{
    fs.cpSync(fs.realpathSync(path.join(source,'node_modules')),path.join(f.dir,'node_modules'),{recursive:true});
    // 在正式启用目录前重现 Windows 的一次占用；后续所有读取均使用完整验签后的应用。
    const rename=fs.renameSync;
    let busy=0, appDir;
    try {
      fs.renameSync=(from,to)=>{
        if (!busy && path.basename(from).startsWith('.profile-') && /^[a-f0-9]{64}$/.test(path.basename(to))) {
          busy++;
          throw Object.assign(new Error('simulated Windows directory busy'), {code:'EPERM'});
        }
        return rename(from,to);
      };
      ({appDir}=managed(f.root,f.dir));
    } finally { fs.renameSync=rename; }
    assert.equal(busy,1);
    const start=async(expected)=>{
      const keys=['BRT_ARCHIVE_OWNER_HELPER','BRT_ARCHIVE_OWNER_GAME'],before=keys.map(k=>process.env[k]);
      process.env[keys[0]]=helper;process.env[keys[1]]=expected;
      try{await f.start(appDir);}finally{for(const [i,k] of keys.entries())if(before[i]===undefined)delete process.env[k];else process.env[k]=before[i];}
    };
    await start(process.execPath);
    const rpc=await f.login(owner);
    const roles=['PEACE','PROBE','SNIPER','FORT','FIXER','SPIKE'];
    const get=async(fixedId=fixed,ua='BoundaryDLL/1.0')=>{
      const res=await fetch(f.origin()+'/api/loadout/'+fixedId,{headers:{'user-agent':ua}});
      return {status:res.status,cache:res.headers.get('cache-control'),json:await res.json()};
    };
    const accountFile=path.join(f.dir,'data/loadouts',owner+'.json');
    const fresh=await get();assert.equal(fresh.status,200);
    assert.deepEqual(Object.keys(fresh.json.roles).sort(),[...roles].sort());
    assert.ok(!fs.existsSync(accountFile),'读取原厂配置不能建立个人存档');
    await rpc.equip(sample.role,1,sample.primary);await rpc.equip(sample.role,2,sample.secondary);
    await rpc.equip(sample.role,4,sample.mobility);await rpc.equip(sample.role,5,sample.left);await rpc.equip(sample.role,6,sample.right);
    for(const w of sample.weapons) await rpc.parts(sample.role,w);
    const savedBytes=fs.readFileSync(accountFile);
    const result=await get();
    assert.equal(result.status,200);assert.equal(result.cache,'no-store');assert.ok(result.json.playerId===owner);
    assert.deepEqual(Object.keys(result.json.roles).sort(),[...roles].sort());
    const canonical=await rpc.archive(...roles);
    for(const role of canonical) {
      const r=result.json.roles[role.RoleID];
      assert.deepEqual([r.primaryWeapon,r.secondaryWeapon,r.leftPylon,r.rightPylon,r.meleeWeapon,r.mobilityModule],
        [role.PrimaryWeapon,role.SecondWeapon,role.LeftPylon,role.RightPylon,role.MeleeWeapon,role.MobilityModule]);
      assert.deepEqual(r.WeaponConfig.Weapons.map(w=>w.WeaponId),role.WeaponConfig.Weapons.map(w=>w.WeaponId));
      for(const [i,raw] of r.WeaponConfig.Weapons.entries()){
        const w=role.WeaponConfig.Weapons[i];
        assert.deepEqual(raw.Parts.map(p=>[p.SlotId,p.PartId||'']),w.Parts.map(p=>[p.SlotId,p.PartId||'']));
        assert.equal(raw.Skin?.SkinInfo?.Id || '', w.Skin?.SuitData?.PaintItem || '');
        assert.equal(raw.Skin?.WeaponOrnament || '', w.Skin?.Ornament || '');
      }
    }
    const role=result.json.roles.PROBE;assert.equal(role.leftPylon,sample.left);assert.equal(role.rightPylon,sample.right);
    const weapons=role.WeaponConfig.Weapons;
    for(const saved of sample.weapons){
      const w=weapons.find(it=>it.WeaponId===saved.WeaponId);
      assert.equal(w.Skin.SkinInfo.Id,saved.Skin.SkinInfo.Id);
      for(const part of saved.Parts)assert.equal(
        (w.Parts.find(p=>p.SlotId===part.SlotId)?.PartId || '').replace(/^None$/,''),
        (part.PartId || '').replace(/^None$/,''));
    }
    assert.deepEqual(fs.readFileSync(accountFile),savedBytes);
    const remote=await f.login(peer);await remote.equip(sample.role,5,'None');
    assert.equal((await get()).status,404); // 同一进程同时绑定两个不同身份，拒绝猜测。
    assert.equal((await get(peer,'Browser')).json.roles.PROBE.leftPylon,'None');
    assert.equal((await get(fixed,'Browser')).status,404);
    assert.equal((await f.http('PUT','/api/loadout/'+fixed,{})).status,400);
    assert.ok(!fs.existsSync(path.join(f.dir,'data/loadouts',fixed+'.json')));
    rpc.close();
    // 等服务确认旧 RPC 断开。另一名确实在线的玩家也不能认领原账号。
    let after; for(let i=0;i<12;i++){after=await get();if(after.status===404)break;await new Promise(r=>setTimeout(r,30));}
    assert.equal(after.status,404);
    await f.stop();await start(helper);
    const again=await f.login(owner);
    assert.equal((await get()).status,404); // 磁盘路径与实际发起连接的映像不符。
    assert.equal((await get(owner,'Browser')).status,200);
    again.close();
  },{roleSuccess:200,scratch});
});
