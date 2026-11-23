'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {fixture}=require('./fixture.cjs'),{managed}=require('./fixture-kit.cjs');
const {forCommunity}=require('./legacy-client.cjs');
const probe=require('./probe-minifarm-sample.json');

const roles=['PEACE','PROBE','SNIPER','FORT','SPIKE','FIXER'];
const columns=['primaryWeapon','secondaryWeapon','leftPylon','rightPylon','meleeWeapon','mobilityModule'];
const rpcColumns=['PrimaryWeapon','SecondWeapon','LeftPylon','RightPylon','MeleeWeapon','MobilityModule'];

test('旧社区档案：没改过的干员返回出厂配置，读取不建立或改写存档',{
  skip:!process.env.BRT_QA_ARCHIVE_SOURCE,timeout:60000,
},async()=>{
  const source=fs.realpathSync(process.env.BRT_QA_ARCHIVE_SOURCE);
  await fixture(source,async f=>{
    fs.cpSync(fs.realpathSync(path.join(source,'node_modules')),path.join(f.dir,'node_modules'),{recursive:true});
    const {appDir,loadoutDir}=managed(f.root,f.dir);
    const {LoadoutStore}=require(path.join(appDir,'game/loadoutStore.js'));
    await f.start(appDir);
    const owner='76561198151616198',rpc=await f.login(owner),file=path.join(loadoutDir,owner+'.json');
    const stored=()=>fs.existsSync(file)?fs.readFileSync(file):null;
    const compare=async()=>{
      const before=stored(),read=forCommunity(new LoadoutStore(),owner),expected=await rpc.archive(...roles);
      assert.deepEqual(Object.keys(read.roles).sort(),[...roles].sort());
      assert.equal(expected.length,roles.length);
      for(const native of expected)assert.deepEqual(columns.map(k=>read.roles[native.RoleID][k]),rpcColumns.map(k=>native[k]));
      assert.deepEqual(stored(),before);return read.roles.PROBE;
    };
    assert.equal(stored(),null);await compare();assert.equal(stored(),null);
    await rpc.equip('PROBE',5,probe.left);await rpc.equip('PROBE',6,probe.right);
    for(const w of probe.weapons)await rpc.parts('PROBE',w);
    const saved=await compare();assert.deepEqual(Object.keys(JSON.parse(stored()).roles),['PROBE']);
    for(const w of probe.weapons){
      const weapon=saved.WeaponConfig.Weapons.find(k=>k.WeaponId===w.WeaponId);
      for(const part of w.Parts)assert.equal(weapon.Parts.find(p=>p.SlotId===part.SlotId)?.PartId||'',part.PartId||'');
    }
    await rpc.equip('PROBE',6,'None');assert.equal((await compare()).rightPylon,'None');
  },{roleSuccess:200,scratch:process.env.BRT_QA_SCRATCH||os.tmpdir()});
});
