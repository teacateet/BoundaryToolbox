'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { createHash } = require('node:crypto');
const { gzipSync } = require('node:zlib');

const archive = path.resolve(process.argv[2]);
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
if (hash(fs.readFileSync(archive)) !== 'fff143b06c2782d559a19c86d32979c09fdeaa29afcccc99a16637c70fc16094') {
  throw new Error('Expected ProjectRebound V0.8.4 Release.zip');
}
const prefix = 'BoundaryMetaServer-main/';
const names = execFileSync('tar', ['-tf', archive], { encoding: 'utf8' }).split(/\r?\n/)
  .filter(name => name.startsWith(prefix)).map(name => name.slice(prefix.length))
  .filter(name => /^(?:index\.js|game\/(?:definitionIndex|loadoutStore)\.js|game\/proto\/(?:Request|Response)\/[\w]+\.proto)$/.test(name)).sort();
if (names.length !== 44 || new Set(names).size !== names.length) throw new Error('Unexpected release code inventory');
const read = name => execFileSync('tar', ['-xOf', archive, prefix + name], { windowsHide: true });
const files = Object.fromEntries(names.map(name => [name, read(name).toString('utf8')]));
const bundle = gzipSync(Buffer.from(JSON.stringify(files)), { level: 9 });
fs.writeFileSync(path.join(__dirname, 'release-084-code.json.gz'), bundle);
fs.writeFileSync(path.join(__dirname, 'release-084-LICENSE.txt'), read('LICENSE.txt'));
console.log('Public source files:', names.length, 'inventory SHA-256:',
  hash(names.map(name => name + '\0' + hash(files[name]) + '\n').join('')));
