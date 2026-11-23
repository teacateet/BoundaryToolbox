'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {randomBytes, randomUUID} = require('node:crypto');
const deps = process.env.BRT_SPAWN_FIXTURE_KIT || path.resolve(__dirname, '../loadout');
const { fixture } = require(path.join(deps, 'fixture.cjs'));
const { managed } = require(path.join(deps, 'fixture-kit.cjs'));
const sample = require(path.join(deps, 'probe-minifarm-sample.json'));

async function main() {
    const location = process.argv[2] || process.env.BRT_ARCHIVE_SOURCE;
    if (!location) throw Error('Supply the archive source directory or BRT_ARCHIVE_SOURCE');
    const source = fs.realpathSync(location);
    const exe = path.join(process.env.BRT_SPAWN_BUILD || path.join(__dirname,'build'), 'verify.exe');
    const credential = randomBytes(32).toString('hex');
    process.env.BRT_SPAWN_ROOMS = JSON.stringify({minifarm:credential});
    await fixture(source, async f => {
        fs.cpSync(fs.realpathSync(path.join(source,'node_modules')), path.join(f.dir,'node_modules'),{recursive:true});
        const { appDir } = managed(f.root,f.dir);
        await f.start(appDir);
        const steam = '76561198014490387';
        const user = await f.login(steam);
        for (const [slot,id] of [[1,sample.primary],[2,sample.secondary],[4,sample.mobility],[5,sample.left],[6,sample.right]])
            await user.equip(sample.role,slot,id);
        for (const gun of sample.weapons) await user.parts(sample.role,gun);
        const args={port:Number(new URL(f.origin()).port),room:'minifarm',steam,connection:randomUUID(),credential};
        const child = spawn(exe,['--room-http'],{stdio:['pipe','pipe','pipe'],windowsHide:true});
        const completed = new Promise((resolve,reject) => {
            child.on('error',reject);
            child.on('close',code=>code===0?resolve():reject(Error('native client returned '+code)));
        });
        child.stdout.pipe(process.stdout);
        child.stderr.pipe(process.stderr);
        child.stdin.end(JSON.stringify(args));
        await completed;
    },{roleSuccess:200,scratch:process.env.BRT_TEST_TMP || require('node:os').tmpdir()});
}
main().catch(err=>{console.error(err.message);process.exitCode=1;});
