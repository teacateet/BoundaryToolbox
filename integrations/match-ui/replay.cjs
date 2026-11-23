"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const net = require("node:net");
const http = require("node:http");
const path = require("node:path");
const { once } = require("node:events");
const { createHash } = require("node:crypto");

const stage = fs.realpathSync(".");
assert.equal(path.dirname(stage), fs.realpathSync(__dirname));
assert.match(path.basename(stage), /^\.verify-[a-z0-9]+$/i);
const version = process.argv[2];
assert.ok(["baseline", "patched"].includes(version));
const patched = version === "patched";
const protobuf = require("protobufjs");
const digest = (obj) => createHash("sha256").update(JSON.stringify(obj)).digest("hex");
const rows = (name) => JSON.parse(fs.readFileSync("game/definitions/DT_" + name + ".json", "utf8"))[0].Rows;
const type = (dir, name) => protobuf.loadSync("game/proto/" + dir + "/" + name + ".proto")
  .lookupType("ProjectBoundary." + name);

function compareLoggedRole(file) {
  const { LoadoutStore } = require(path.join(stage, "game", "loadoutStore.js"));
  const store = new LoadoutStore();
  const roleId = "Sniper";
  const defaults = store.getNormalizedRoleData(roleId);
  const slots = {
    LeftPylon: "leftPylon", RightPylon: "rightPylon", MobilityModule: "mobilityModule",
    MeleeWeapon: "meleeWeapon", PrimaryWeapon: "primaryWeapon", SecondWeapon: "secondaryWeapon",
  };
  const results = [];
  const lines = fs.readFileSync(file, "utf8").split(/\r?\n/);
  for (const [index, line] of lines.entries()) {
    const match = line.match(/\[ARCHIVE\] Returning loadout data:\s*(\{.+\})/);
    if (!match) continue;
    const role = JSON.parse(match[1]).PlayerRoleDatas.find((entry) => entry.RoleID === roleId);
    if (!role) continue;
    const nonDefaultSlots = Object.entries(slots)
      .filter(([field, slot]) => role[field] !== store.toFlatItemId(defaults[slot]))
      .map(([field]) => field);
    results.push({ line: index + 1, nonDefaultSlots });
  }
  return results;
}

async function listenLocal(server) {
  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  return server.address().port;
}

async function readRpc(port, name, message) {
  const route = "/assets.Assets/" + name;
  const wrapper = type("Request", "RequestWrapper");
  const payload = wrapper.encode({ MessageId: 1, RPCPath: route, Message: message }).finish();
  const header = Buffer.alloc(4);
  header.writeUInt32BE(payload.length);
  const client = net.createConnection({ host: "127.0.0.1", port });
  client.setTimeout(10000, () => client.destroy(new Error("fixture read timed out")));
  try {
    await once(client, "connect");
    client.write(Buffer.concat([header, payload]));
    let frame = Buffer.alloc(0);
    for await (const chunk of client) {
      frame = Buffer.concat([frame, chunk]);
      if (frame.length < 4 || frame.length < 4 + frame.readUInt32BE(0)) continue;
      assert.equal(frame.length, 4 + frame.readUInt32BE(0));
      const reply = type("Response", "ResponseWrapper").decode(frame.subarray(4));
      assert.equal(reply.MessageId, 1);
      assert.equal(reply.ErrorCode, 0);
      assert.equal(reply.RPCPath, route);
      const response = type("Response", name + "Response");
      return response.toObject(response.decode(reply.Message), { arrays: true, defaults: true });
    }
    throw new Error("fixture closed before its reply");
  } finally {
    client.destroy();
  }
}

async function run() {
  // 仅替换临时副本最后的生产端口启动块。原始路由、RPC 分派和编码器参与回归。
  const source = fs.readFileSync("index.js", "utf8");
  const marker = "app.listen(process.env.PORT || 8000, () => {";
  assert.equal(source.split(marker).length, 2);
  const entry = path.join(stage, "fixture-entry.cjs");
  fs.writeFileSync(entry, source.slice(0, source.indexOf(marker)) +
    "module.exports = { app, server, fixtureId: TEMP_USER_ID };\n");
  console.log = () => {};
  const { app, server, fixtureId } = require(entry);
  assert.match(fixtureId, /^[\w-]+$/);
  const record = (label) => ({ primaryWeapon: "None", secondaryWeapon: "None", _skinToken: label });
  const fixture = { roles: {
    SNIPER: record("fixture-sniper"), Probe: record("fixture-probe"),
    PEACE: record("fixture-upper"), Peace: record("fixture-title"), UNKNOWN: record("fixture-unknown"),
  } };
  const dir = "data/loadouts";
  fs.mkdirSync(dir, { recursive: true });
  const archive = path.join(dir, fixtureId + ".json");
  const bytes = JSON.stringify(fixture);
  if (patched) assert.equal(fs.readFileSync(archive, "utf8"), bytes);
  else fs.writeFileSync(archive, bytes, { flag: "wx" });

  const web = http.createServer(app);
  try {
    const webPort = await listenLocal(web);
    const rpcPort = await listenLocal(server);
    const url = "http://127.0.0.1:" + webPort + "/api/definitions/roles";
    const get = (ending) => fetch(url + ending, { signal: AbortSignal.timeout(10000), redirect: "error" });
    const chars = rows("CharacterDefinition");
    const radars = rows("RadarDefinition");
    const radarIds = Object.keys(radars).sort();
    assert.deepEqual(radarIds, ["11001001", "11001002", "11001003"]);
    assert.equal(Object.keys(chars).length, 18);
    assert.equal(Object.values(chars).filter((role) => role.bIsBot).length, 6);
    assert.ok(Object.values(chars).every((role) => role.RadarID === "11001001" &&
      role.RadarClass.AssetPathName === radars[role.RadarID].RadarClass.AssetPathName));
    const radarTypes = rows("ItemType");
    assert.ok(radarIds.every((id) => radarTypes[id].Type === "EPBItemType::Radar"));
    const modes = rows("GameModeDefinition");
    const maps = modes.Rush_PVE_Normal.MapsForMatch.map((row) => row.RowName);
    assert.deepEqual(maps, ["MiniFarm", "CircularX", "OSS"]);
    assert.equal(Object.values(modes).length, 14);
    assert.ok(Object.values(modes).every((mode) => mode.bTeamChange === false));

    const list = await get("");
    assert.equal(list.status, 200);
    assert.deepEqual((await list.json()).roles.sort(), Object.keys(chars).sort());
    const exactRoles = [];
    for (const id of Object.keys(chars).sort()) {
      const response = await get("/" + id);
      assert.equal(response.status, 200);
      const role = await response.json();
      assert.equal(role.roleId, id);
      assert.equal(role.radarId, chars[id].RadarID);
      exactRoles.push(role);
    }
    const alias = await get("/Sniper");
    assert.equal(alias.status, patched ? 200 : 404);
    if (patched) assert.deepEqual(await alias.json(), exactRoles.find((role) => role.roleId === "SNIPER"));
    else assert.equal((await alias.json()).roleId, "Sniper");
    const missing = await get("/unknown");
    assert.equal(missing.status, 404);
    assert.equal((await missing.json()).roleId, "unknown");

    const items = await readRpc(rpcPort, "QueryAssets", Buffer.alloc(0));
    const itemIds = new Set(items.ItemDatas.map((item) => item.ItemId));
    assert.ok(radarIds.every((id) => itemIds.has(id)));
    const requested = ["SNIPER", "Sniper", "PROBE", "Probe", "PEACE", "Peace", "pEaCe", "FORT", "unknown"];
    const request = type("Request", "GetPlayerArchiveV2Request").encode({ RoleIDs: requested }).finish();
    const archiveSchema = type("Response", "GetPlayerArchiveV2Response").root.lookupType("ProjectBoundary.PlayerRoleData");
    assert.deepEqual(archiveSchema.fieldsArray.map((field) => [field.name, field.id]), [
      ["RoleID", 1], ["LeftPylon", 2], ["RightPylon", 3], ["MobilityModule", 4], ["MeleeWeapon", 5],
      ["PrimaryWeapon", 6], ["SecondWeapon", 7], ["SkinConfig", 8], ["WeaponConfig", 9],
    ]);
    const reply = await readRpc(rpcPort, "GetPlayerArchiveV2", request);
    assert.deepEqual(reply.PlayerRoleDatas.map((r) => r.RoleID), requested);
    const byId = Object.fromEntries(reply.PlayerRoleDatas.map((r) => [r.RoleID, r]));
    const skin = (r) => r.SkinConfig.Suit.SkinItem;
    for (const [aliasId, exactId, sentinel] of [
      ["Sniper", "SNIPER", "fixture-sniper"], ["PROBE", "Probe", "fixture-probe"],
    ]) {
      const exact = byId[exactId], other = byId[aliasId];
      assert.equal(skin(exact), sentinel);
      assert.equal(exact.PrimaryWeapon, "None");
      assert.equal(exact.WeaponConfig.Weapons.length, 0);
      if (patched) assert.deepEqual(other, { ...exact, RoleID: aliasId });
      else {
        assert.notEqual(skin(other), sentinel);
        assert.notEqual(other.PrimaryWeapon, "None");
        assert.ok(other.WeaponConfig.Weapons.length > 0);
      }
    }
    assert.equal(skin(byId.PEACE), "fixture-upper");
    assert.equal(skin(byId.Peace), "fixture-title");
    assert.ok(!skin(byId.pEaCe).startsWith("fixture-"));
    assert.ok(!skin(byId.unknown).startsWith("fixture-"));
    assert.ok(!skin(byId.FORT).startsWith("fixture-"));
    assert.equal(fs.readFileSync(archive, "utf8"), bytes);
    process.stdout.write(JSON.stringify({
      version, roleRows: exactRoles.length, identifiersInArchive: requested.length, radarIds, maps,
      sniperHttp: alias.status, sniperArchiveReadSaved: skin(byId.Sniper) === "fixture-sniper",
      loggedSniper: process.argv[3] ? compareLoggedRole(process.argv[3]) : undefined,
      exactRoleDigest: digest(exactRoles),
      exactArchiveDigest: digest(["SNIPER", "Probe", "PEACE", "Peace", "FORT", "unknown", "pEaCe"].map((id) => byId[id])),
    }));
  } finally {
    web.closeAllConnections();
    for (const listener of [web, server]) {
      if (listener.listening) await new Promise((resolve) => listener.close(resolve));
    }
  }
}

run().catch((error) => { console.error(error); process.exitCode = 1; });
