'use strict';

const fs = require('node:fs');
const path = require('node:path');

const difficulties = ['Easy', 'Normal', 'Hard'];
const tutorialKeys = ['Movement', 'Weapons', 'Combat', 'Scout', 'Simulation'];
const tutorialWorlds = ['Tutorial_Movement', 'Tutorial_Shoot', 'Tutorial_Combat', 'Tutorial_Radar'];
const mapPackages = {
  warehouse: '/Game/Maps/EA/Domination/Warehouse/Warehouse',
  DataCenter: '/Game/Maps/EA/Domination/DataCenter/DataCenter',
  CircularX: '/Game/Maps/EA/Domination/CircularX/CircularX',
  MiniFarm: '/Game/Maps/EA/Purge/MiniFarm/MiniFarm',
  OSS: '/Game/Maps/EA/Purge/OSS/OSS',
};

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
}

// 原作会在东亚和南亚过滤可选行。单端点来源用原 UUID 发布所有地区用户均可选择的原作行。
function managedRegionCatalog(appDir, discovery) {
  let metadata;
  try { metadata = readJson(path.join(appDir, 'profile-location.json')); }
  catch (error) {
    if (error.code !== 'ENOENT') console.warn('[PVE-REGIONS] source metadata unreadable');
    return null;
  }
  const sourceKind = metadata?.sourceKind;
  const main = sourceKind === 'metaserver-main-007850d0';
  if (!main && !['local-archive-3961c933', 'projectrebound-release-v0.8.4'].includes(sourceKind)) return null;
  const first = discovery?.servers?.[0];
  if (!Array.isArray(discovery?.servers) || discovery.servers.length !== 1 ||
      first?.location_id !== 6 || first.region_id !== '336d1f3e-3ecb-11eb-a7dc-3b7705f20f56' ||
      first.ipv4 !== '127.0.0.1' || first.ipv6 !== '' || !Number.isInteger(first.port) ||
      first.port < 1 || first.port > 65535) return null;
  const regions = [{ RegionId: first.region_id, RegionName: main ? 'us-east1' : 'asia-east2' }];
  if (!main) return { discovery, regions };
  const second = { location_id: 10, region_id: '11111111-2222-4333-8444-555555555501',
    ipv4: first.ipv4, ipv6: first.ipv6, port: first.port };
  regions.push({ RegionId: second.region_id, RegionName: 'asia-east2' });
  return { discovery: { ...discovery, servers: [first, second] }, regions };
}

function regionReplySummary(regions) {
  const ids = regions.filter(row => typeof row.RegionId === 'string' && row.RegionId.length > 0).length;
  const names = regions.map(row => ['us-east1', 'asia-east2'].includes(row.RegionName) ? row.RegionName :
    row.RegionName ? 'other' : 'missing');
  return '[PVE-REGIONS] rpc rows=' + regions.length + ' ids=' + ids + ' names=' + (names.join(',') || 'none');
}

function modesFrom(table) {
  const rows = table.find(entry => entry.Name === 'DT_GameModeDefinition')?.Rows;
  return difficulties.map(difficulty => {
    const id = 'Rush_PVE_' + difficulty;
    const row = rows?.[id];
    const maps = row?.MapsForMatch?.map(map => map.RowName);
    if (!row?.MatchmakingString || !maps?.length || maps.some(map => typeof map !== 'string' || !map)) {
      throw new Error('Unsupported mode definition: ' + id);
    }
    return { id, matchName: row.MatchmakingString, title: row.GameModeName.SourceString,
      description: row.GameModeSimpleDescreiption.SourceString, maps };
  });
}

const leaseMs = 15000;
const portOK = value => Number.isInteger(value) && value >= 1024 && value <= 65535 &&
  ![6969, 8000, 9000, 9001].includes(value);
const runOK = value => typeof value === 'string' && /^[a-zA-Z0-9_-]{1,96}$/.test(value);

function roomFileFor(app) {
  const drivePath = app.startsWith('\\\\?\\') ? app.slice(4) : app;
  if (!path.isAbsolute(app) || (process.platform === 'win32' && !/^[A-Z]:[\\/]/i.test(drivePath)) ||
      !/^[a-f0-9]{64}$/.test(path.basename(app)) ||
      path.basename(path.dirname(app)) !== 'apps' ||
      path.basename(path.dirname(path.dirname(app))) !== 'profile-runtime') return null;
  return path.join(path.dirname(path.dirname(app)), 'pve-ready-' + path.basename(app) + '.json');
}

function roomReader(app) {
  const file = roomFileFor(app);
  return () => {
    if (!file) return undefined;
    try {
      const info = fs.lstatSync(file);
      if (!info.isFile() || info.size > 4096) return null;
      return JSON.parse(fs.readFileSync(file, 'utf8'));
    } catch (err) {
      return err.code === 'ENOENT' ? undefined : null;
    }
  };
}

// 每张卡只代表本次点击。候选项来自原表，并须能生成实际的本机开房参数。
const nextPrefix = 'BRT_NEXT_';
function nextCards(table) {
  const names = { Easy: '简单', Normal: '普通', Hard: '困难' };
  const pairs = modesFrom(table).flatMap(mode => [...new Set(mode.maps)].map(row => {
    const plan = pvePlan(table, mode.id, row);
    return { mode: mode.id, map: plan.map };
  }));
  const cards = [{ id: nextPrefix + 'Any', title: '随机地图 · 随机人机难度', pairs }];
  for (const map of new Set(pairs.map(p => p.map))) {
    const eligible = pairs.filter(p => p.map === map);
    if (eligible.length > 1) cards.push({ id: nextPrefix + 'Any_' + map, title: map + ' · 随机难度', pairs: eligible });
    for (const p of eligible) {
      const difficulty = p.mode.slice('Rush_PVE_'.length);
      cards.push({ id: nextPrefix + difficulty + '_' + map, title: map + ' · ' + names[difficulty] + '人机', pairs: [p] });
    }
  }
  return cards;
}

function freshRoom(receipt, time) {
  return receipt?.v === 1 && runOK(receipt.runId) && Number.isSafeInteger(receipt.expiresAt) &&
    receipt.expiresAt > time && receipt.expiresAt - time <= leaseMs;
}

const ephemeralPort = port => Number.isInteger(port) && port >= 1024 && port <= 65535;

function advertisement(receipt, field) {
  const ipc = receipt?.[field];
  if (!ipc || Object.keys(ipc).length !== 3 || ipc.v !== 1 || !ephemeralPort(ipc.port) ||
      ipc.port === receipt.port || typeof ipc.nonce !== 'string' || !/^[0-9a-f]{64}$/.test(ipc.nonce)) return null;
  return ipc;
}

function selectablePool(receipt, time) {
  return freshRoom(receipt, time) && receipt.autoPool === true && receipt.nextMatch === 1 &&
    receipt.mode === 'pve' && receipt.offline === true && !!advertisement(receipt, 'controlIpc');
}

function inspectRoom(receipt, readTable, time, selection) {
  if (receipt === undefined) return { reason: 'absent' };
  if (!freshRoom(receipt, time)) return { reason: 'invalid-or-expired' };
  if (selection && receipt.runId !== selection.runId) return { reason: 'different-run' };
  if (receipt.mode !== 'pve' || receipt.offline !== true) return { reason: 'not-local-pve' };
  if (!portOK(receipt.port)) return { reason: 'port' };
  const difficulty = difficulties.find(name => name.toLowerCase() === receipt.difficulty);
  const mode = 'Rush_PVE_' + difficulty;
  if (!difficulty || (selection && selection.mode !== mode)) return { reason: 'difficulty' };
  let plan;
  try { plan = pvePlan(readTable(), mode, receipt.map); } catch { return { reason: 'mode-map' }; }
  if (receipt.gameClass !== plan.gameClass) return { reason: 'mode-class' };
  return { plan };
}

const readPlaylistRoom = roomReader(__dirname);

function withPvePlaylists(base, table, readRoom = readPlaylistRoom) {
  const existing = base.PVE ?? [];
  if (!Array.isArray(existing)) throw new Error('Expected a PVE playlist array');
  const receipt = readRoom();
  const plan = receipt?.autoPool === true ? null : inspectRoom(receipt, () => table, Date.now()).plan;
  const modes = modesFrom(table);
  const isNative = item => modes.some(mode => item?.Name === mode.id && item.GameModeList?.length === 1 &&
    item.GameModeList[0] === mode.id);
  const native = plan && existing.find(item => isNative(item) && item.Name === plan.mode);
  const additions = [];
  const chineseTitles = ['简单人机', '普通人机', '困难人机'];
  for (const [index, mode] of modes.entries()) {
    if (plan) {
      if (mode.id !== plan.mode || native) continue;
    } else if (existing.some(item => item?.GameModeList?.some(id => id === mode.id || id === mode.matchName))) continue;
    if (existing.some(item => item?.Name === mode.id)) throw new Error('Playlist name collision: ' + mode.id);
    additions.push({
      Name: mode.id, Title: [{ zh: chineseTitles[index] }, { en: mode.title }],
      BigTitle: [{ zh: chineseTitles[index] }, { en: mode.title }],
      Description: [{ zh: '消灭所有敌人。' }, { en: mode.description }],
      BigDescription: [{ zh: '消灭所有敌人。' }, { en: mode.description }],
      SecondaryDescription: [], PlotImage: [], LargePlotImage: [],
      GameModeList: [mode.id], bHasFilter: false, bIsLive: true,
      Priority: difficulties.length - index,
      StartTime: 0, StopTime: 0,
    });
  }
  if (plan) {
    // 准备大厅可按名称选行，旧焦点失效时也可退回首行。
    return { ...base, PVE: [native || additions[0], ...existing.filter(item => !isNative(item))] };
  }
  if (selectablePool(receipt, Date.now())) {
    const cards = nextCards(table).map(({id,title}, i) => {
      const labels = [{zh: title}, {en: id.slice(nextPrefix.length).replaceAll('_', ' / ')}];
      return { Name:id, Title:labels, BigTitle:labels, Description:labels, BigDescription:labels,
        SecondaryDescription:[], PlotImage:[], LargePlotImage:[], GameModeList:[id], bHasFilter:false,
        bIsLive:true, Priority:-100+i, StartTime:0, StopTime:0 };
    });
    const reserved = new Set(cards.map(card => card.Name));
    const preserved = existing.filter(item => {
      if (!reserved.has(item?.Name)) return true;
      if (item.GameModeList?.length !== 1 || item.GameModeList[0] !== item.Name)
        throw new Error('Playlist name collision: ' + item.Name);
      return false;
    });
    return {...base, PVE:[...cards, ...preserved, ...additions]};
  }
  return additions.length ? { ...base, PVE: [...existing, ...additions] } : base;
}

function pvePlan(table, modeName, mapName) {
  const mode = modesFrom(table).find(entry => entry.id === modeName || entry.matchName === modeName);
  if (!mode) throw new Error('Unknown PvE mode: ' + modeName);
  const mapRow = mode.maps.find(name => name.toLowerCase() === String(mapName).toLowerCase());
  if (!mapRow) throw new Error('Map outside ' + mode.id + ': ' + mapName);
  if (!Object.hasOwn(mapPackages, mapRow)) throw new Error('World path required for map: ' + mapRow);
  const world = mapPackages[mapRow];
  const bp = 'BP_PBGameMode_' + mode.id;
  const gameClass = '/Game/Online/GameMode/' + bp + '.' + bp + '_C';
  return { mode: mode.id, matchName: mode.matchName, mapRow, map: world.split('/').pop(),
    world, gameClass, argv: [world + '?game=' + gameClass] };
}

function packageNames(manifest) {
  return new Set(manifest.split(/\r?\n/).map(line => line.trim())
    .filter(line => line.startsWith('ProjectBoundary/Content/') && line.endsWith('.umap'))
    .map(line => '/Game/' + line.slice('ProjectBoundary/Content/'.length, -'.umap'.length)));
}

function tutorialWorld(name, worlds) {
  const world = '/Game/Maps/EA/Tutorial/' + name;
  if (!tutorialWorlds.includes(name) || !worlds.has(world)) throw new Error('Unknown tutorial world: ' + name);
  return { status: 'explicit-world', world, argv: [world] };
}

function resolveTutorial(key, exports, worlds) {
  if (!tutorialKeys.includes(key)) throw new Error('Unknown tutorial key: ' + key);
  const levels = exports?.find(entry => entry.Name === 'Default__PBLocalPlayer_BP_C')?.Properties?.TutorialLevels;
  const defaultCollapsed = key === 'Simulation';
  const unresolved = { key, defaultCollapsed, status: 'level-mapping-required', argv: [] };
  if (!levels) return unresolved;
  const value = Array.isArray(levels) ? levels.find(entry => entry.Key === key)?.Value : levels[key];
  if (!value) return unresolved;
  const reference = typeof value === 'string' ? value : value.AssetPathName;
  if (typeof reference !== 'string') throw new Error('Unsupported world reference: ' + key);
  const match = /^(\/Game\/(?:[A-Za-z0-9_]+\/)*([A-Za-z0-9_]+))(?:\.\2)?$/.exec(reference);
  if (!match || !worlds.has(match[1])) throw new Error('World missing from package list: ' + key);
  return { key, defaultCollapsed, status: 'mapped-world', world: match[1], argv: [match[1]] };
}

module.exports = { selectablePool, advertisement, ephemeralPort, modesFrom, withPvePlaylists, pvePlan, nextCards, nextPrefix, resolveTutorial, tutorialWorld, packageNames, tutorialKeys,
  managedRegionCatalog, regionReplySummary, roomReader, roomFileFor, leaseMs, runOK, freshRoom, inspectRoom };

if (require.main === module) {
  try {
    const [command, ...args] = process.argv.slice(2);
    let result;
    if (command === 'pve' && args.length === 3) {
      result = pvePlan(readJson(args[0]), args[1], args[2]);
    } else if (command === 'playlists' && args.length === 2) {
      result = withPvePlaylists(readJson(args[1]), readJson(args[0]));
    } else if (command === 'tutorial' && [1, 3].includes(args.length)) {
      const worlds = args[2] && packageNames(fs.readFileSync(args[2], 'utf8'));
      result = resolveTutorial(args[0], args[1] && readJson(args[1]), worlds);
    } else if (command === 'tutorial-world' && args.length === 2) {
      result = tutorialWorld(args[0], packageNames(fs.readFileSync(args[1], 'utf8')));
    } else {
      throw new Error('Usage: pve MODE_TABLE MODE MAP | playlists MODE_TABLE BASE_JSON | tutorial KEY [LOCAL_PLAYER_EXPORT PACKAGE_LIST] | tutorial-world WORLD_NAME PACKAGE_LIST');
    }
    process.stdout.write(JSON.stringify(result, null, 2) + '\n');
    if (result.status === 'level-mapping-required') process.exitCode = 2;
  } catch (error) {
    process.stderr.write(error.message + '\n');
    process.exitCode = 1;
  }
}
