'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { fixture } = require('./fixture.cjs');
const source = process.argv[2];
const baseline = process.argv[3];
const { managed } = baseline ? require(path.resolve(baseline, 'integrations/loadout/fixture-kit.cjs'))
    : require('./fixture-kit.cjs');
const HIVE = 'PROBE_MISSILE-HIVE', MOTION = 'PROBE_INFO-MOTION', SENSOR = 'PROBE_INFO-PROBE';
const account = 'fixture_pod_owner';
const roles = ['PROBE', 'Sniper', 'PEACE', 'FIXER', 'FORT', 'SPIKE'];
const pods = row => [row.LeftPylon, row.RightPylon];

async function run(f) {
    fs.cpSync(fs.realpathSync(path.join(source, 'node_modules')), path.join(f.dir, 'node_modules'), { recursive: true });
    const { appDir, loadoutDir } = managed(f.root, f.dir);
    assert.ok(!fs.existsSync(loadoutDir));
    const file = path.join(loadoutDir, account + '.json');
    await f.start(appDir);
    const client = await f.login(account);
    const initial = await client.archive(...roles);
    assert.deepEqual(pods(initial[0]), [HIVE, SENSOR]);
    await client.equip('PROBE', 5, HIVE);
    await client.equip('PROBE', 6, SENSOR);
    const before = fs.readFileSync(file);
    const left = await client.roleStatus('PROBE', 5, 'MISSILE-HIVE');
    const right = await client.roleStatus('PROBE', 6, 'INFO-MOTION');
    assert.deepEqual([left, right], baseline ? [1, 1] : [200, 200]);
    const expected = [HIVE, baseline ? SENSOR : MOTION];
    if (baseline) assert.deepEqual(fs.readFileSync(file), before);
    const rows = await client.archive(...roles);
    assert.deepEqual(pods(rows[0]), expected);
    assert.deepEqual(rows.slice(1), initial.slice(1));
    const disk = JSON.parse(fs.readFileSync(file)).roles.PROBE;
    assert.deepEqual([disk.leftPylon, disk.rightPylon], expected);
    async function checkViews(client, id, pair) {
        assert.deepEqual(pods((await client.archive('PROBE'))[0]), pair);
        const single = await f.http('GET', '/api/loadout/' + id + '/PROBE');
        const full = await f.http('GET', '/api/loadout/' + id);
        assert.equal(single.status, 200);
        assert.equal(full.status, 200);
        for (const view of [single.body, full.body.roles.PROBE]) {
            assert.deepEqual([view.leftPylon, view.rightPylon], pair);
            assert.deepEqual([view.leftLauncher, view.rightLauncher], pair);
            assert.deepEqual([view.leftPod, view.rightPod], pair);
        }
    }
    await checkViews(client, account, expected);
    await client.equip('PROBE', 6, MOTION);
    await checkViews(client, account, [HIVE, MOTION]);
    const accepted = fs.readFileSync(file);
    for (const invalid of ['SNIPER_INFO-MOTION', 'GSW-DMR', 'NO-SUCH-POD']) {
        assert.equal(await client.roleStatus('PROBE', 6, invalid), 1);
    }
    assert.deepEqual(fs.readFileSync(file), accepted);
    const peer = await f.login('fixture_pod_peer');
    assert.deepEqual(await peer.archive(...roles), initial);
    assert.equal((await f.http('GET', '/api/loadout/fixture_unused')).status, 404);
    await f.stop();
    const oldId = 'fixture_pod_legacy', oldFile = path.join(loadoutDir, oldId + '.json');
    const oldBytes = JSON.stringify({ playerId: oldId, roles: { PROBE: { leftPylon: 'INFO-MOTION', rightPylon: 'MISSILE-HIVE' } } });
    fs.writeFileSync(oldFile, oldBytes, { flag: 'wx' });
    await f.start(appDir);
    const reopened = await f.login(account);
    await checkViews(reopened, account, [HIVE, MOTION]);
    assert.deepEqual(fs.readFileSync(file), accepted);
    await checkViews(await f.login(oldId), oldId, baseline ? [HIVE, SENSOR] : [MOTION, HIVE]);
    assert.equal(fs.readFileSync(oldFile, 'utf8'), oldBytes);
    if (!baseline) {
        await reopened.equip('PROBE', 5, 'INFO-MOTION');
        await reopened.equip('PROBE', 6, 'MISSILE-HIVE');
        await checkViews(reopened, account, [MOTION, HIVE]);
        await reopened.equip('PROBE', 5, 'None');
        await reopened.equip('PROBE', 6, '');
        await checkViews(reopened, account, ['None', 'None']);
        await reopened.equip('Sniper', 6, 'INFO-MOTION');
        assert.equal((await reopened.archive('Sniper'))[0].RightPylon, 'SNIPER_INFO-MOTION');
        await checkViews(reopened, account, ['None', 'None']);
    }
    return { variant: baseline ? 'baseline' : 'candidate', rawStatus: [left, right], rawReadback: expected,
        canonicalReadbackAfterRestart: [HIVE, MOTION], legacyReadback: baseline ? [HIVE, SENSOR] : [MOTION, HIVE],
        sourceKind: JSON.parse(fs.readFileSync(path.join(appDir, 'profile-location.json'))).sourceKind,
        status: 'PASS' };
}

fixture(source, run, { roleSuccess: 200 }).then(result => console.log(JSON.stringify(result)))
    .catch(error => { console.error(error); process.exitCode = 1; });
