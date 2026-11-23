'use strict';

const fs = require('node:fs');
const [role, ...args] = process.argv.slice(2);

if (role === 'reader') {
  const [modulePath, app, mode, elapsed] = args;
  const { roomReader, createReadyRoomSession } = require(modulePath);
  const rows = {};
  for (const [level, maps] of Object.entries({ Easy: ['warehouse'], Normal: ['CircularX'], Hard: ['OSS'] })) {
    rows['Rush_PVE_' + level] = { MatchmakingString: level.toUpperCase() + ' BOT',
      GameModeName: { SourceString: level }, GameModeSimpleDescreiption: { SourceString: level },
      MapsForMatch: maps.map(RowName => ({ RowName })) };
  }
  const read = roomReader(app);
  const session = createReadyRoomSession(() => [{ Name: 'DT_GameModeDefinition', Rows: rows }],
    read, { now: () => Date.now() + Number(elapsed) });
  process.stdout.write(JSON.stringify(session.start('fixture', 'fixture', mode, 'ticket', 1)));
} else {
  const deadline = Date.now() + 30000;
  const stop = () => fs.existsSync('stop') || Date.now() > deadline;
  if (role === 'wrapper') {
    const child = require('node:child_process').spawn(args[0], [__filename, 'game'], {
      windowsHide: true, stdio: 'ignore', cwd: process.cwd(),
    });
    const timer = setInterval(() => {
      if (fs.existsSync('stop-wrapper')) process.exit();
      if (stop()) clearInterval(timer);
    }, 35);
    child.on('error', () => process.exit(1));
  } else if (role === 'game') {
    let socket = require('node:dgram').createSocket('udp4');
    if (fs.existsSync('test-spawn-check')) {
      const { createHash } = require('node:crypto'), key = process.env.BRT_LOCAL_SPAWN_TOKEN || '';
      if (!/^[a-f0-9]{64}$/.test(key) || process.env.BRT_PROFILE_ROOM_SIGNER || process.env.BRT_SPAWN_ROOMS)
        throw Error('Unexpected dedicated-room environment');
      // 只记录校验值和房间名，绝不把能使用的密钥写到磁盘。
      fs.writeFileSync('test-spawn-child',JSON.stringify({room:process.env.BRT_LOCAL_SPAWN_ROOM,
        verifier:createHash('sha256').update(Buffer.from(key,'hex')).digest('hex')}));
    }
    socket.bind(Number(args[0] || 0), '127.0.0.1', () => {
      fs.writeFileSync('listening.tmp', JSON.stringify([process.pid, socket.address().port]));
      fs.renameSync('listening.tmp', 'listening.json');
    });
    const timer = setInterval(() => {
      if (socket && fs.existsSync('release-udp')) { socket.close(); socket = null; }
      if (stop() || fs.existsSync('stop-game')) { clearInterval(timer); process.exit(); }
    }, 35);
  } else throw new Error('Unknown fixture role');
}
