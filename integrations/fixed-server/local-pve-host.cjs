'use strict';
// 此入口只由 BRT 为自己创建的单机 PvE 房间运行。没有网络管理端口。
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');
const { createInterface } = require('node:readline');

// 受支持零售版 DT_GameModeDefinition 的 MapsForMatch；不能把五张图与三个难度任意组合。
const modes = new Map([
  ['easy',   {suffix: 'Easy',   maps: ['Warehouse', 'DataCenter']}],
  ['normal', {suffix: 'Normal', maps: ['MiniFarm', 'CircularX', 'OSS']}],
  ['hard',   {suffix: 'Hard',   maps: ['OSS', 'CircularX']}]
]);

function offlineRoom(values) {
  const [map, difficulty, rawPort, archive] = values;
  const port = Number(rawPort);
  const mode = modes.get(difficulty);
  if (values.length !== 4 || !mode ||
      !Number.isInteger(port) || String(port) !== rawPort || port < 1024 || port > 65535 ||
      [6969, 8000, 9000, 9001].includes(port) || archive !== 'http://127.0.0.1:8000')
    throw Error('Invalid local room');
  if (!mode.maps.includes(map)) throw Error('Map not supported at this difficulty');
  const bp = 'BP_PBGameMode_Rush_PVE_' + mode.suffix;
  const args = ['-log', '-server', '-nullrhi', '-nosplash', '-NoWindow', '-map=' + map,
    '-mode=/Game/Online/GameMode/' + bp + '.' + bp + '_C', '-port=' + port, '-external=' + port,
    '-pve', '-MULTIHOME=127.0.0.1', '-LogicServerURL=' + archive, '-servername=BRT', '-serverregion=LOCAL'];
  return {map, difficulty, port, args};
}

// Birth.cpp 的业务回执；账号、物品自由文本和异常内容不进入房间日志。
const birthLines = [
  /^observed-birth life=[1-9]\d{0,5} pawn-age=\d{1,8}\.\d{4} inventory=\d{1,3} holding=[01] acknowledged=[01]$/,
  /^newborn-weapon life=[1-9]\d{0,5} slot=[12] submitted=[01] parts-readback=[01]$/,
  /^newborn-slot-skipped life=[1-9]\d{0,5} slot=[12] reason=weapon-in-use$/,
  /^newborn-skipped life=[1-9]\d{0,5} reason=[a-z-]{1,48}$/,
  /^newborn-wait life=[1-9]\d{0,5} reason=[a-z-]{1,48}$/,
  /^newborn-plan-returned life=[1-9]\d{0,5} present-guns=[0-2]$/
];
// 专服写入原生角色档案的回执：事件名加固定键值，值只含物品 ID 字符。
const archiveLine = /^\[BRT-ARCHIVE\] event=[a-z-]{1,24}(?: [a-z-]{1,20}=[A-Za-z0-9_.-]{1,96}){0,10}$/;
function isBirthLine(line) {
  const prefix = '[BRT-INVENTORY] ';
  if (!line.startsWith(prefix) || line.length > 160 || /[\r\n\u2028\u2029]/.test(line)) return false;
  const message = line.slice(prefix.length);
  return birthLines.some(pattern => pattern.test(message));
}

function roomJournal(write) {
  let remaining = 4096;
  function append(line) {
    if (remaining > 0) { remaining--; write(line); }
    else if (remaining === 0) { remaining--; write('[Launcher] Room event limit reached.'); }
  }
  return line => {
    if (isBirthLine(line) || (line.length <= 420 && archiveLine.test(line))) return append(line);
    // 原服会在此行附上结束原因，只传递确认过的生命周期事件。
    if (line.startsWith('[SERVER_LIFECYCLE] match_end_detected reason=')) {
      line = '[SERVER_LIFECYCLE] match_end_detected';
    }
    const ownerPreorder = line === '[BRT-INVENTORY] owner-preorder-submitted roles=6 slots=36';
    const grappleObservation = line.length < 160 && /^\[BRT-GRAPPLE\] owner=[1-9]\d{0,5} life=[1-9]\d{0,5} slot=[1-9]\d{0,5} ms=\d{1,8} event=(?:baseline|change|followup|slot-end|life-end|limit) hook=[0-3] cool=[01] anchor=[01] permille=(?:-1|1000|\d{1,3})$/.test(line) && !/[\r\n]/.test(line);
    if (line.length > 512 || !(grappleObservation || ownerPreorder || /^\[SERVER\] (?:Map loaded: |Mode: |Port: )/.test(line) ||
        line === '[SERVER] Server is now listening.' || /^\[SERVER_LIFECYCLE\] [a-z_]+$/.test(line))) return;
    // 同图重新加载也要交给 BRT，使上一场的开赛状态失效。
    append(line);
  };
}

function launch(values) {
  const {map, difficulty, port, args} = offlineRoom(values);
  const dir = fs.realpathSync.native('.');
  if (path.basename(dir) !== `BRT-Auto-${difficulty}-${map}-${port}`) throw Error('Wrong room directory');
  const cfg = JSON.parse(fs.readFileSync(path.join(dir, 'serverconfig.json'), 'utf8'));
  if (cfg.map !== map || cfg.difficulty !== difficulty || cfg.mode !== 'pve' || cfg.offline !== true ||
      cfg.port !== port || cfg.externalPort !== port || cfg.serverName !== 'BRT' || cfg.serverRegion !== 'LOCAL')
    throw Error('Room configuration changed');
  const game = path.join(path.dirname(dir), 'ProjectBoundarySteam-Win64-Shipping.exe');
  if (fs.realpathSync.native(game).toLowerCase() !== game.toLowerCase()) throw Error('Wrong dedicated executable');
  const logDir = path.join(dir, 'logs');
  fs.mkdirSync(logDir, {recursive: true});
  if (fs.realpathSync.native(logDir).toLowerCase() !== logDir.toLowerCase()) throw Error('Wrong log directory');
  const fd = fs.openSync(path.join(logDir, `log-brt-${crypto.randomUUID()}.txt`), 'wx');
  const write = s => fs.writeSync(fd, s + '\n');
  const idPath = path.join(dir, 'serverId.json');
  if (!fs.existsSync(idPath)) {
    fs.writeFileSync(idPath, JSON.stringify({serverId: crypto.randomBytes(4).toString('hex')}), {flag: 'wx'});
  }
  if (fs.realpathSync.native(idPath).toLowerCase() !== idPath.toLowerCase() || fs.statSync(idPath).size > 256)
    throw Error('Invalid local server ID file');
  const id = JSON.parse(fs.readFileSync(idPath, 'utf8')).serverId;
  if (typeof id !== 'string' || !/^[a-f0-9]{8}$/.test(id)) throw Error('Invalid local server ID');
  args.push('-serverid=' + id);
  const server = spawn(game, args, {
    cwd: dir, shell: false, windowsHide: true, detached: true, stdio: ['ignore', 'pipe', 'pipe']
  });
  server.once('error', () => { write('[Launcher] Failed to launch server'); process.exitCode = 1; });
  server.once('spawn', () => write('[Launcher] Server launched. PID = ' + server.pid));
  // 就绪和开赛文字必须来自专服自身。日志不记录玩家发言、账号或参数。
  const record = roomJournal(write);
  for (const pipe of [server.stdout, server.stderr]) {
    createInterface({input: pipe, crlfDelay: Infinity}).on('line', record);
  }
  server.once('close', code => {
    write('[Launcher] Server exited.'); fs.closeSync(fd);
    process.exitCode = code === 0 ? 0 : 1;
  });
}

// BRT 先将本进程纳入本次房间的 Windows 作业，再放行。父进程提前退出不会留下孤儿服。
if (module.parent) {
  module.exports = { offlineRoom, roomJournal };
} else {
  const input = createInterface({input: process.stdin, crlfDelay: Infinity});
  input.once('line', line => {
    input.close();
    if (line !== 'start') { process.exitCode = 1; return; }
    try { launch(process.argv.slice(1)); } catch { process.exitCode = 1; }
  });
}
