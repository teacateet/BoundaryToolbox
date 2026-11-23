'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { createHash, randomUUID } = require('node:crypto');

const baseHash = 'c85d1cc7a0426b8c42a22ef4c6fe60562ce58e3fdb3c41debebbd987bfae866e';
const dependencies = {
    'index.js': '3961c933f8a3396fb01d9bb7e190db2312ce5a4c2320ea78348aed84df5ce10d',
    'game/definitionIndex.js': '67ecfb27713497cc6277e3710cbe5cc5827c5001a5915c60d127f67c79fbc4bd',
    'game/proto/Request/UpdateWeaponArchiveV2Request.proto':
        '425d0cafe2df613e16ade0a7c0b1a487bb72eaefc737031cd5c87f889cd51308',
    'game/proto/Request/UpdateRoleArchiveV2Request.proto':
        '1bf7d325cd095bddeafafd4f7becc3393c40d5b8e7a1383f9ab07465cf80dc90',
    'game/proto/Response/GetPlayerArchiveV2Response.proto':
        'e76482fcc9b801c2e6ba7188cd55c5fdd901765d4692037299fadc9cbeb3786c',
};
const hash = bytes => createHash('sha256').update(bytes).digest('hex');

function buildStore(original) {
    if (hash(original) !== baseHash) throw new Error('Unsupported loadoutStore.js');
    const source = original.toString('utf8');
    const eol = source.includes('\r\n') ? '\r\n' : '\n';
    let code = source.replaceAll('\r\n', '\n');
    function replace(from, to) {
        if (code.split(from).length !== 2) throw new Error('Unexpected source layout: ' + from.slice(0, 60));
        code = code.replace(from, () => to);
    }
    function replaceBlock(first, after, body) {
        const start = code.indexOf(first);
        const end = code.indexOf(after, start + first.length);
        if (start < 0 || end < 0) throw new Error('Missing store method: ' + first);
        replace(code.slice(start, end), body + '\n\n');
    }
    const factory = require('./projection.cjs').createLoadoutProjection.toString().replaceAll('\r\n', '\n');
    replace('class LoadoutStore {', [factory, '', 'let archiveType;',
        'const projection = createLoadoutProjection(() => archiveType ??= protobuf.loadSync(WEAPON_ARCHIVE_PROTO)',
        '    .lookupType("ProjectBoundary.UpdateWeaponArchiveV2Request"), WEAPON_ARCHIVE_SLOT_SCOPES);',
        '', 'class LoadoutStore {'].join('\n'));
    replace('            this.attachDecodedMetadata(data);',
        '            projection.hydrate(this, data);\n            this.attachDecodedMetadata(data);');
    replace('...Object.keys(archives), legacy && legacy.WeaponId,',
        '...Object.keys(archives), ...Object.keys(roleData.loadoutSnapshot?.weaponConfigs || {}), legacy && legacy.WeaponId,');
    replace('if (!archive) archive = decode(this.buildDefaultWeaponArchiveRaw(roleId, weaponId));',
        'if (!archive) archive = decode(projection.legacyRaw(this, roleId, weaponId, roleData.loadoutSnapshot));');
    replaceBlock('        if (weaponId && archives[weaponId])', '    getRoleDefinition(roleId)', [
        '        if (this.isNoneish(weaponId)) return "";',
        '        for (const raw of [archives[weaponId], roleData._weaponArchiveRaw]) {',
        '            if (projection.decode(raw, roleId, weaponId)) return raw;',
        '        }',
        '        return projection.legacyRaw(this, roleId, weaponId, roleData.loadoutSnapshot);',
        '    }'].join('\n'));
    replace('const result = { ...(snapshot || {}) };', 'const result = projection.read(this, roleId, snapshot, roleData);');
    replace('this.withSnapshotMetadata(roleId, roleData.loadoutSnapshot, normalizedRoleData)',
        'this.withSnapshotMetadata(roleId, roleData.loadoutSnapshot, roleData)');
    replace('        return snapshot;\n    }\n    /**',
        '        return projection.read(this, roleId, snapshot, roleData);\n    }\n    /**');
    replaceBlock('    setRoleLoadoutSnapshot(playerId, roleId, snapshot) {', '    /**\n     * Get all stored role', [
        '    setRoleLoadoutSnapshot(playerId, roleId, snapshot) {',
        '        if (snapshot) return this.setFullLoadout(playerId, { roles: { [roleId]: { loadoutSnapshot: snapshot } } });',
        '        const data = this.load(playerId) || { playerId, roles: {} };',
        '        this.save(playerId, { ...data, roles: { ...data.roles, [roleId]: { ...data.roles[roleId], loadoutSnapshot: null } } });',
        '    }'].join('\n'));
    replaceBlock('    setFullLoadout(playerId, fullLoadout) {', '}\n\n// Singleton', [
        '    setFullLoadout(playerId, fullLoadout) {',
        '        const data = this.load(playerId) || { playerId, roles: {} };',
        '        const roles = { ...data.roles };',
        '        for (const [roleId, input] of Object.entries(this.normalizeFullLoadout(fullLoadout).roles)) {',
        '            roles[roleId] = projection.write(this, roleId, roles[roleId] || {}, input);',
        '        }',
        '        this.save(playerId, { ...data, roles });',
        '    }'].join('\n'));
    return Buffer.from(code.replaceAll('\n', eol));
}

function deploy(action, root) {
    if (!['status', 'apply', 'rollback'].includes(action) || !root) {
        throw new Error('Usage: node deploy.cjs status|apply|rollback <metaserver-directory>');
    }
    const target = path.resolve(root, 'game/loadoutStore.js');
    const backup = target + '.boundary-toolbox-v1.orig';
    const lock = target + '.boundary-toolbox.lock';
    const inspect = () => {
        const current = fs.readFileSync(target);
        const original = fs.existsSync(backup) ? fs.readFileSync(backup) : current;
        const currentHash = hash(current);
        const compatible = Object.entries(dependencies).every(([name, digest]) => {
            const file = path.resolve(root, name);
            return fs.existsSync(file) && hash(fs.readFileSync(file)) === digest;
        });
        const candidate = hash(original) === baseHash ? buildStore(original) : null;
        const installed = candidate && hash(candidate) === currentHash;
        const state = installed ? 'installed' : compatible && candidate && currentHash === baseHash ? 'ready' : 'unsupported';
        return { state, compatible, currentHash, candidate, original };
    };
    const result = info => ({ state: info.state, compatible: info.compatible, target,
        sha256: info.currentHash, candidateSha256: info.candidate ? hash(info.candidate) : null });
    if (action === 'status') return result(inspect());
    const fd = fs.openSync(lock, 'wx');
    const temp = target + '.' + randomUUID() + '.tmp';
    try {
        const info = inspect();
        if (info.state === 'unsupported' || (action === 'apply' && !info.compatible)) {
            throw new Error('Service code differs from the supported version: ' + target);
        }
        if ((action === 'apply' && info.state === 'installed') ||
            (action === 'rollback' && info.currentHash === baseHash)) return result(info);
        if (action === 'apply' && !fs.existsSync(backup)) {
            fs.copyFileSync(target, backup, fs.constants.COPYFILE_EXCL);
        }
        if (hash(fs.readFileSync(backup)) !== baseHash) throw new Error('Original store backup differs');
        fs.writeFileSync(temp, action === 'apply' ? info.candidate : info.original, { flag: 'wx' });
        if (hash(fs.readFileSync(target)) !== info.currentHash) throw new Error('Store changed during deployment');
        fs.renameSync(temp, target);
        return result(inspect());
    } finally {
        fs.rmSync(temp, { force: true });
        fs.closeSync(fd);
        fs.unlinkSync(lock);
    }
}

module.exports = { deploy };
if (require.main === module) {
    try {
        const result = deploy(...process.argv.slice(2));
        console.log(JSON.stringify(result));
        if (result.state === 'unsupported' || !result.compatible) process.exitCode = 2;
    } catch (error) {
        console.error(JSON.stringify({ error: error.message }));
        process.exitCode = 1;
    }
}
