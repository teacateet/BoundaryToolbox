'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const net = require('node:net');
const { once } = require('node:events');
const { spawn } = require('node:child_process');
const { setTimeout: delay } = require('node:timers/promises');
const { deploy } = require('./deploy.cjs');

async function allocatePorts() {
    const reservations = [];
    try {
        for (let i = 0; i < 3; i++) {
            const server = net.createServer();
            server.listen(0, '127.0.0.1');
            await once(server, 'listening');
            reservations.push(server);
        }
        return reservations.map(server => server.address().port);
    } finally {
        await Promise.all(reservations.map(server => new Promise(resolve => server.close(resolve))));
    }
}

async function fixture(source, run, { roleSuccess = 0, scratch = __dirname } = {}) {
    source = fs.realpathSync(source);
    scratch = fs.realpathSync(scratch);
    const root = fs.mkdtempSync(path.join(scratch, '.fixture-'));
    const dir = path.join(root, 'source');
    let child = null, closed;
    const sockets = new Set();
    async function stop() {
        for (const socket of sockets) socket.destroy();
        sockets.clear();
        if (child) {
            if (child.exitCode === null && child.signalCode === null) child.kill();
            await closed;
            child = null;
        }
    }
    try {
        fs.mkdirSync(dir);
        for (const rel of ['index.js', 'game/definitionIndex.js', 'game/loadoutStore.js',
            'game/proto', 'game/definitions']) {
            fs.cpSync(path.join(source, rel), path.join(dir, rel), { recursive: true });
        }
        if (deploy('status', source).state === 'installed') {
            fs.copyFileSync(path.join(source, 'game/loadoutStore.js.boundary-toolbox-v1.orig'),
                path.join(dir, 'game/loadoutStore.js'));
        }
        assert.equal(deploy('status', dir).state, 'ready');
        const protobuf = require(path.join(source, 'node_modules/protobufjs'));
        function type(relative, name) {
            return protobuf.loadSync(path.join(dir, 'game/proto', relative + '.proto')).lookupType('ProjectBoundary.' + name);
        }
        const request = type('Request/RequestWrapper', 'RequestWrapper');
        const response = type('Response/ResponseWrapper', 'ResponseWrapper');
        const roleUpdate = type('Request/UpdateRoleArchiveV2Request', 'UpdateRoleArchiveV2Request');
        const weaponUpdate = type('Request/UpdateWeaponArchiveV2Request', 'UpdateWeaponArchiveV2Request');
        const suit = protobuf.loadSync(path.join(__dirname, 'native-read.proto')).lookupType('fixture.Suit');
        const status = type('Response/UpdateRoleArchiveV2', 'UpdateRoleArchiveV2Response');
        const getArchive = type('Request/GetPlayerArchiveV2Request', 'GetPlayerArchiveV2Request');
        const archive = protobuf.loadSync(path.join(__dirname, 'native-read.proto')).lookupType('fixture.Archive');
        let httpPort, rpcPort, mmPort;
        async function http(method, route, payload) {
            const operation = await fetch('http://127.0.0.1:' + httpPort + route, {
                method, redirect: 'error', signal: AbortSignal.timeout(5000),
                headers: { 'content-type': 'application/json' }, body: JSON.stringify(payload),
            });
            const body = await operation.text();
            return { status: operation.status, body: operation.headers.get('content-type')?.includes('application/json')
                ? JSON.parse(body) : body };
        }
        async function start(appDir = dir) {
            assert.equal(child, null);
            [httpPort, rpcPort, mmPort] = await allocatePorts();
            const args = appDir === dir ? ['index.js'] : ['--no-global-search-paths', path.join(appDir, 'index.js')];
            child = spawn(process.execPath, args, { cwd: appDir, windowsHide: true,
                env: { ...process.env, PORT: String(httpPort), RPC_PORT: String(rpcPort),
                    MATCHMAKING_PORT: String(mmPort), NODE_PATH: path.join(source, 'node_modules') },
                stdio: ['ignore', 'pipe', 'pipe'] });
            closed = once(child, 'close');
            let log = '', ready = false;
            child.stdout.on('data', chunk => {
                log = (log + chunk).slice(-8192);
                if (log.includes('miau >:3 - ' + rpcPort) && log.includes(' - ' + mmPort)) ready = true;
            });
            child.stderr.on('data', chunk => { log = (log + chunk).slice(-8192); });
            for (let i = 0; i < 100 && !ready; i++) {
                if (child.exitCode !== null || child.signalCode !== null) throw new Error('Fixture exited: ' + log);
                await delay(50);
            }
            if (!ready) throw new Error('Fixture did not report its own ports: ' + log);
        }
        async function login(playerId) {
            const auth = await http('POST', '/connectServer', { playerId, platform: 'steam', version: '124840' });
            assert.equal(auth.status, 200);
            assert.equal(auth.body.userId, playerId);
            assert.equal(auth.body.endpoint, '127.0.0.1:' + rpcPort);
            assert.ok(auth.body.gateToken);
            const socket = net.createConnection(rpcPort, '127.0.0.1');
            sockets.add(socket);
            await once(socket, 'connect');
            const stream = socket[Symbol.asyncIterator]();
            let tail = Buffer.alloc(0), id = 0;
            async function exchange(rpc, message, wrapper = response) {
                const bytes = request.encode({ MessageId: ++id, RPCPath: rpc, Message: message }).finish();
                const header = Buffer.alloc(4);
                header.writeUInt32BE(bytes.length);
                const timeout = setTimeout(() => socket.destroy(new Error('Fixture RPC timed out')), 5000);
                try {
                    socket.write(Buffer.concat([header, bytes]));
                    while (tail.length < 4 || tail.length < 4 + tail.readUInt32BE()) {
                        const chunk = await stream.next();
                        if (chunk.done) throw new Error('Fixture RPC closed');
                        tail = Buffer.concat([tail, chunk.value]);
                    }
                    const end = 4 + tail.readUInt32BE();
                    const reply = wrapper.decode(tail.subarray(4, end));
                    tail = tail.subarray(end);
                    assert.equal(reply.MessageId, id);
                    assert.equal(reply.RPCPath, rpc);
                    if (wrapper === response) assert.equal(reply.ErrorCode, 0);
                    return reply.Message;
                } finally {
                    clearTimeout(timeout);
                }
            }
            await exchange(auth.body.gateToken, Buffer.alloc(0), request);
            async function call(name, input, payload, output) {
                const bytes = await exchange('/assets.Assets/' + name, input.encode(payload).finish());
                return output.decode(bytes);
            }
            return {
                close() { socket.destroy(); sockets.delete(socket); },
                async roleStatus(roleId, op, change) {
                    const input = typeof change === 'string' ? { ItemId: change } : { SkinData: suit.encode(change).finish() };
                    return (await call('UpdateRoleArchiveV2', roleUpdate, { RoleId: roleId, Operation: op, ...input }, status)).StatusCode;
                },
                async weaponStatus(roleId, weapon) {
                    return (await call('UpdateWeaponArchiveV2', weaponUpdate, { RoleId: roleId, WeaponArchive: weapon }, status)).StatusCode;
                },
                async equip(roleId, op, item) {
                    const reply = await call('UpdateRoleArchiveV2', roleUpdate,
                        { RoleId: roleId, Operation: op, ItemId: item }, status);
                    assert.equal(reply.StatusCode, roleSuccess, 'UpdateRoleArchiveV2 inner StatusCode');
                },
                async parts(roleId, weapon) {
                    const reply = await call('UpdateWeaponArchiveV2', weaponUpdate,
                        { RoleId: roleId, WeaponArchive: weapon }, status);
                    assert.equal(reply.StatusCode, 0);
                },
                async archive(...roleIds) {
                    const result = await call('GetPlayerArchiveV2', getArchive, { RoleIDs: roleIds }, archive);
                    return result.PlayerRoleDatas;
                },
            };
        }
        return await run({ root, dir, http, start, stop, login, weaponUpdate, origin: () => 'http://127.0.0.1:' + httpPort });
    } finally {
        await stop();
        if (path.dirname(root) === scratch && path.basename(root).startsWith('.fixture-')) {
            fs.rmSync(root, { recursive: true, force: true, maxRetries: 4 });
        }
    }
}

module.exports = { fixture };
