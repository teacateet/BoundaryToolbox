'use strict';
const assert=require('node:assert/strict'),net=require('node:net'),http=require('node:http'),path=require('node:path');
const {once}=require('node:events');
async function httpJson(port, route, body) {
  return new Promise((resolve, reject) => {
    const req = http.request({ host: '127.0.0.1', port, path: route, method: body ? 'POST' : 'GET',
      agent: false, headers: { 'Content-Type': 'application/json' }, timeout: 4000 }, res => {
      let raw = '';
      res.setEncoding('utf8');
      res.on('data', data => { raw += data; });
      res.on('error', reject);
      res.on('end', () => {
        try { assert.equal(res.statusCode, 200); resolve(JSON.parse(raw)); }
        catch (error) { reject(error); }
      });
    });
    req.on('timeout', () => req.destroy(new Error('Local HTTP timeout')));
    req.on('error', reject);
    req.end(body && JSON.stringify(body));
  });
}

async function withLogin(app, ports, account, run, beforeBind, timeoutMs=4000) {
  async function login(playerId) {
    const session = await httpJson(ports.http, '/connectServer', { playerId, platform: 'steam', version: '124840' });
    assert.equal(session.userId, playerId);
    assert.equal(session.endpoint, '127.0.0.1:' + ports.rpc);
    assert.ok(session.gateToken);
    return session;
  }
  const session = await login(account);
  const pb = require(path.join(app, 'node_modules/protobufjs'));
  const type = (dir, name) => pb.loadSync(path.join(app, 'game/proto', dir, name + '.proto'))
    .lookupType('ProjectBoundary.' + name);
  const envelope = type('Request', 'RequestWrapper');
  const response = type('Response', 'ResponseWrapper');
  const socket = net.createConnection(ports.rpc, '127.0.0.1');
  socket.setTimeout(timeoutMs, () => socket.destroy(new Error('Local menu RPC timeout')));
  try {
    await once(socket, 'connect');
    const stream = socket[Symbol.asyncIterator]();
    let pending = Buffer.alloc(0), id = 0;
    async function receive(wrapper) {
      while (pending.length < 4 || pending.length < 4 + pending.readUInt32BE()) {
        const chunk = await stream.next();
        if (chunk.done) throw new Error('Menu RPC closed before response');
        pending = Buffer.concat([pending, chunk.value]);
      }
      const end = 4 + pending.readUInt32BE();
      const reply = wrapper.decode(pending.subarray(4, end));
      pending = pending.subarray(end);
      return reply;
    }
    function frame(messageId, route, request) {
      const bytes = envelope.encode({ MessageId: messageId, RPCPath: route, Message: request }).finish();
      const prefix = Buffer.alloc(4);
      prefix.writeUInt32BE(bytes.length);
      return Buffer.concat([prefix, bytes]);
    }
    async function rpc(route, request, wrapper = response) {
      socket.write(frame(++id, route, request));
      const reply = await receive(wrapper);
      assert.equal(reply.MessageId, id);
      assert.ok(reply.RPCPath === route, 'reply route matches request');
      if (wrapper === response) assert.equal(reply.ErrorCode, 0);
      return reply.Message;
    }
    async function pipeline(calls) {
      const first = id + 1;
      socket.write(Buffer.concat(calls.map(([route, request]) => frame(++id, route, request))));
      for (const [index, [route]] of calls.entries()) {
        const reply = await receive(response);
        assert.deepEqual([reply.MessageId, reply.RPCPath, reply.ErrorCode], [first + index, route, 0]);
      }
    }
    async function push(method) {
      const reply = await receive(response);
      assert.deepEqual([reply.MessageId, reply.RPCPath, reply.ErrorCode], [-1, method, 0]);
      return Buffer.from(reply.Message);
    }
    const bind = session => rpc(session.gateToken, Buffer.alloc(0), envelope);
    if (beforeBind) await beforeBind(rpc, type);
    await bind(session);
    return await run(rpc, type, async playerId => bind(await login(playerId)), push, pipeline, socket.localPort,
      () => socket.destroy());
  } finally { socket.destroy(); }
}

module.exports={httpJson,withLogin};
