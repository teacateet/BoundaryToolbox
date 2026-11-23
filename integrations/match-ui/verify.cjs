"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { createHash } = require("node:crypto");
const { execFileSync } = require("node:child_process");

const meta = path.resolve(process.argv[2] ||
  "C:/BrtWork/SteamLibrary/steamapps/common/Boundary/ProjectBoundary/Binaries/Win64/BoundaryMetaServer-main");
const archiveLog = process.argv[3] && path.resolve(process.argv[3]);
const area = fs.realpathSync(__dirname);
const patch = path.join(area, "role-read-case.patch");
const reviewed = {
  "index.js": "3961c933f8a3396fb01d9bb7e190db2312ce5a4c2320ea78348aed84df5ce10d",
  "game/loadoutStore.js": "c85d1cc7a0426b8c42a22ef4c6fe60562ce58e3fdb3c41debebbd987bfae866e",
  "game/definitionIndex.js": "67ecfb27713497cc6277e3710cbe5cc5827c5001a5915c60d127f67c79fbc4bd",
};

function checkSources() {
  for (const [name, digest] of Object.entries(reviewed)) {
    const bytes = fs.readFileSync(path.join(meta, name));
    assert.equal(createHash("sha256").update(bytes).digest("hex"), digest, name);
  }
}

function replay(stage, version) {
  const args = [path.join(area, "replay.cjs"), version];
  if (archiveLog && version === "baseline") args.push(archiveLog);
  const output = execFileSync(process.execPath, args, {
    cwd: stage, encoding: "utf8", timeout: 45000,
    env: { ...process.env, NODE_PATH: path.join(meta, "node_modules") },
  });
  const result = JSON.parse(output);
  assert.equal(result.version, version);
  console.log(JSON.stringify(result));
  return result;
}

checkSources();
const stage = fs.mkdtempSync(path.join(area, ".verify-"));
try {
  fs.mkdirSync(path.join(stage, "game"));
  for (const name of Object.keys(reviewed)) {
    fs.copyFileSync(path.join(meta, name), path.join(stage, name));
  }
  for (const [name, extension] of [["definitions", ".json"], ["proto", ".proto"]]) {
    const src = path.join(meta, "game", name);
    fs.cpSync(src, path.join(stage, "game", name), {
      recursive: true,
      filter: (file) => {
        const info = fs.lstatSync(file);
        return info.isDirectory() || (info.isFile() && path.extname(file) === extension);
      },
    });
  }
  // 用独立 Git 目录检查及应用补丁，工作树只包含本次复制的文件。
  const git = (...args) => execFileSync("git", args, { cwd: stage, encoding: "utf8" });
  git("init", "--quiet", "--template=");
  const before = replay(stage, "baseline");
  git("apply", "--check", patch);
  git("apply", patch);
  const after = replay(stage, "patched");
  git("apply", "--reverse", "--check", patch);
  assert.equal(before.exactRoleDigest, after.exactRoleDigest);
  assert.equal(before.exactArchiveDigest, after.exactArchiveDigest);
  checkSources();
  console.log("PASS baseline, patched, exact responses, reverse check and installed source fingerprints");
} finally {
  const target = fs.realpathSync(stage);
  assert.equal(path.dirname(target), area);
  assert.match(path.basename(target), /^\.verify-[a-z0-9]+$/i);
  fs.rmSync(target, { recursive: true, force: true });
}
