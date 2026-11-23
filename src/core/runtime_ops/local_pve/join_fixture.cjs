'use strict';
// 受控大厅：与托管测试服务保持 RPC 连接，并实现公开的两个命令。
const net = require('node:net');
const fs = require('node:fs');
const [pipeArg, rpcPort, log] = process.argv.slice(2);
if (!pipeArg.startsWith('-pipe=')) throw Error('Missing per-launch pipe');
function connectRpc() {
  const rpc = net.connect(Number(rpcPort), '127.0.0.1');
  rpc.on('error', () => {});
  rpc.on('data', () => {});
  rpc.on('close', () => {
    if (process.argv[5] !== 'reconnect') return process.exit();
    fs.writeFileSync(log + '.disconnected', '');
    const timer = setInterval(() => {
      if (!fs.existsSync(log + '.reconnect')) return;
      clearInterval(timer);
      fs.unlinkSync(log + '.reconnect');
      connectRpc();
    }, 30);
  });
}
connectRpc();
if (process.argv[5] === 'reconnect') {
  setTimeout(() => process.exit(3), 75000).unref();
  setInterval(() => { if (fs.existsSync(log + '.exit')) process.exit(); }, 30);
}
net.createServer(socket => {
  socket.on('error', () => {});
  let pending = '';
  socket.on('data', chunk => {
    pending += chunk;
    for (let at; (at = pending.indexOf('\n')) !== -1;) {
      const line = pending.slice(0, at);
      pending = pending.slice(at + 1);
      const [cmd, args] = line.split('\t');
      fs.appendFileSync(log, line + '\n');
      if (cmd === 'ping' && args === '{}') socket.write('pong\t{}\n');
      else if (cmd === 'join' && /^127\.0\.0\.1:\d+$/.test(JSON.parse(args).ip)) {
        if (!fs.existsSync(log + '.silent')) socket.write('join_ack\t{"status":"ok"}\n');
      } else socket.destroy();
    }
  });
}).listen('\\\\.\\pipe\\' + pipeArg.slice(6));
