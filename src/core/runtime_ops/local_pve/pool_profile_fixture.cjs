'use strict';
const assert = require('node:assert/strict');
const net = require('node:net');
const fs = require('node:fs');
const [modulePath, lease, table] = process.argv.slice(2);
const {createReadyRoomSession} = require(modulePath);
const read = () => JSON.parse(fs.readFileSync(lease));
const wait = async predicate => {
  const until = Date.now() + 20000;
  while (!predicate()) {
    if (Date.now() > until) throw Error('Timed out waiting for fixture state');
    await new Promise(resolve => setTimeout(resolve, 40));
  }
};
let connections = 0;
const server = net.createServer(async rpc => {
  const connection = ++connections;
  rpc.on('error', () => {});
  try {
    let verified;
    const session = createReadyRoomSession(() => JSON.parse(fs.readFileSync(table)), read, { registerSpawn(id,owner,proof,run,live) {
      assert.ok(live());
      assert.equal(owner,proof.owner);
      const mac = require('node:crypto').createHmac('sha256',Buffer.alloc(32,0x73))
        .update(['brt-local-room-v1',run,id,owner,proof.verifier].join('\n')).digest('hex');
      assert.equal(mac,proof.signature);
      verified=proof.verifier;
    }});
    const user = '76561198185503627', mode = 'Rush_PVE_Normal';
    await wait(() => fs.existsSync('start'));
    const start = (ticket, choice) => {
      const regions = {ids:['region'], published:[{RegionId:'region'}]};
      assert.equal(session.start(user,user,mode,ticket,choice,regions).status,'queued');
    };
    if (connection === 1) {
      start('cancelled',1);
      session.prepare(user,'cancelled',rpc,() => { throw Error('Cancelled choice prepared'); });
      await wait(() => fs.existsSync('staging'));
      session.stop(user,'cancelled');
      fs.writeFileSync('cancelled','');
      await wait(() => fs.existsSync('next'));
    } else {
      const {controlIpc,runId} = read();
      const invalid = net.connect(controlIpc.port, '127.0.0.1', () => {
        invalid.write(JSON.stringify({v:1,op:'prepare',runId,nonce:controlIpc.nonce,
          requestId:require('node:crypto').randomUUID(),mode,clientPort:0,rpcPort:rpc.localPort})+'\n');
      });
      invalid.on('error', () => {});
      await new Promise(resolve => invalid.once('close', resolve));
      fs.writeFileSync('refused','');
      await wait(() => fs.existsSync('replace'));
    }
    start('valid',2);
    session.prepare(user,'valid',rpc,() => {
      const {port,choiceId,map,difficulty,runId} = read();
      assert.match(verified,/^[a-f0-9]{64}$/);
      fs.writeFileSync('assigned-' + connection,JSON.stringify({port,choiceId,map,difficulty,runId,verifier:verified}));
      assert.ok(session.issueTicket(user,'valid'));
      const query = {userId:[user],ticketId:'valid'};
      for (let i = 0; i < 2; i++) {
        const delivery = session.handleTicket(user,query,false,rpc).assignment();
        assert.equal(delivery.body.gameServerIP, '127.0.0.1:' + read().port);
        rpc.write(JSON.stringify(delivery.body) + '\n',delivery.written);
      }
    });
    await wait(() => fs.existsSync('finish-' + connection));
    session.stop(user,'valid');
    rpc.destroy();
  } catch (error) { fs.writeFileSync('failure', String(error)); process.exit(2); }
});
server.listen(0,'127.0.0.1',() => fs.writeFileSync('rpc-port',String(server.address().port)));
