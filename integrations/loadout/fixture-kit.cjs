'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { createHash } = require('node:crypto');
const { prepare } = require('../profile-runtime/prepare.cjs');

function managed(root, source, disabled = []) {
    const kit = path.join(root, 'kit');
    const runtime = path.join(root, 'runtime');
    fs.mkdirSync(kit);
    fs.mkdirSync(runtime);
    const core = path.resolve(__dirname, '../../src/core');
    const manifest = fs.readFileSync(path.join(core, 'local_profile.rs'), 'utf8');
    for (const [,relative] of manifest.matchAll(/include_bytes!\(\s*"(\.\.\/\.\.\/integrations\/[^"]+)"\s*\)/g)) {
        fs.copyFileSync(path.resolve(core, relative), path.join(kit, path.basename(relative)));
    }
    // 对照组只把待测补丁换成唯一匹配的恒等变换，其余内容走同一发行器。
    // 装备默认值补丁扩展新版装备写入器，对照旧写入器时两者一起关闭。
    const omitted = new Set(disabled);
    if (omitted.has('native-equip-appearance.patch')) {
        omitted.add('retail-equipment-defaults.patch');
        omitted.add('room-spawn.patch');
    }
    if (omitted.has('room-spawn.patch') || omitted.has('local-ticket.patch')) omitted.add('legacy-client.patch');
    for (const name of omitted) {
        const file = path.join(kit, name);
        assert.ok(fs.statSync(file).isFile());
        fs.writeFileSync(file, '+++ b/game/loadoutStore.js\n@@ -1,1 +1,1 @@\n     getDefaultRoleSkinMetadata(roleId) {\n');
    }
    const result = prepare(source, runtime, kit);
    assert.equal(result.kind, 'managed', JSON.stringify(result));
    assert.equal(result.loadoutDir, path.join(fs.realpathSync(source), 'data', 'loadouts'));
    return result;
}

function extractRelease(root, archive = path.join(__dirname, '../../.workflow/qa/rebound-084/Release.zip')) {
    const zip = path.resolve(archive);
    assert.equal(createHash('sha256').update(fs.readFileSync(zip)).digest('hex'),
        'fff143b06c2782d559a19c86d32979c09fdeaa29afcccc99a16637c70fc16094');
    execFileSync('tar', ['-xf', zip, '-C', root, ...['index.js', 'game', 'node_modules']
        .map(name => 'BoundaryMetaServer-main/' + name)], { windowsHide: true });
    return path.join(root, 'BoundaryMetaServer-main');
}

if (require.main === module) {
    const root = fs.mkdtempSync(path.join(__dirname, '.fixture-'));
    try {
        const source = extractRelease(root, process.argv[2]);
        const { appDir, loadoutDir } = managed(root, source);
        assert.ok(!fs.existsSync(loadoutDir));
        assert.match(fs.readFileSync(path.join(appDir, 'game/loadoutStore.js'), 'utf8'), /nativeWeaponId\(roleId, id\)/);
        for (const file of ['index.js', 'game/loadoutStore.js']) {
            execFileSync(process.execPath, ['--check', path.join(appDir, file)], { windowsHide: true });
        }
        console.log('PASS: V0.8.4 + production KIT + native equipment patch, empty fixture storage');
    } finally {
        fs.rmSync(root, { recursive: true, force: true, maxRetries: 4 });
    }
}

module.exports = { managed, extractRelease };
