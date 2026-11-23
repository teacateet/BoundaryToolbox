'use strict';

const fs = require('node:fs');
const crypto = require('node:crypto');
const path = require('node:path');
const tableName = 'DT_MatchmakingRegionAddress';
const tablePackage = 'ProjectBoundary/Content/GameData/CSVConfig/Matchmaking/' + tableName;

function readAddressTable(exports, file = '') {
  const tables = Array.isArray(exports) ? exports.filter(o => o?.Name === tableName) : [];
  if (tables.length !== 1) throw new Error('Expected one ' + tableName + ' export');
  const table = tables[0];
  if (table.Package !== undefined && table.Package !== tablePackage) throw new Error('Unexpected address table package');
  const struct = table.Properties?.RowStruct;
  if (!['DataTable', 'PBDataTable', 'CompositeDataTable'].includes(table.Type) || struct?.ObjectName !== "ScriptStruct'PBMatchmakingRegionAddressRow'" ||
      struct.ObjectPath !== '/Script/ProjectBoundary' || !table.Rows || typeof table.Rows !== 'object' || Array.isArray(table.Rows)) {
    throw new Error('Expected PBMatchmakingRegionAddressRow and an exported Rows object');
  }
  if (table.Type === 'CompositeDataTable' && table.Properties.ParentTables?.length) {
    throw new Error('Composite address table has parent tables; their resolved rows are required');
  }
  for (const row of Object.values(table.Rows)) {
    if (!row || typeof row !== 'object' || Array.isArray(row)) throw new Error('Incomplete exported address row');
  }
  const underPackage = path.resolve(file).replaceAll('\\', '/').endsWith('/' + tablePackage + '.json');
  const packageEvidence = table.Package ? 'export' : underPackage ? 'export-path' : 'unspecified';
  return { package: packageEvidence === 'unspecified' ? null : tablePackage, packageEvidence, keys: Object.keys(table.Rows) };
}

function compareKeys(keys, reply) {
  if (reply?.StatusCode !== undefined && reply.StatusCode !== 0) throw new Error('Region RPC status was not success');
  if (!Array.isArray(reply?.Regions)) throw new Error('Expected a decoded region reply with an explicit Regions array');
  const exact = new Set(keys);
  const ascii = /^[\x20-\x7e]+$/;
  const asciiKeys = keys.filter(key => ascii.test(key));
  const folded = new Map(asciiKeys.map(key => [key.toLowerCase(), key]));
  if (folded.size !== asciiKeys.length) throw new Error('Ambiguous case-insensitive row names');
  function match(value) {
    if (value === undefined || value === '') return null;
    if (typeof value !== 'string') throw new Error('Expected string region fields');
    if (exact.has(value)) return value;
    return ascii.test(value) ? folded.get(value.toLowerCase()) ?? null : null;
  }
  return reply.Regions.map((row, index) => {
    if (!row || typeof row !== 'object' || Array.isArray(row)) throw new Error('Expected a region message');
    return { index, byRegionName: match(row.RegionName), byRegionId: match(row.RegionId) };
  });
}

module.exports = { readAddressTable, compareKeys };

if (require.main === module) {
  try {
    const [tableFile, responseFile] = process.argv.slice(2);
    if (!tableFile || process.argv.length > 4) throw new Error('Usage: node region-keys.cjs EXPORTED_TABLE_JSON [DECODED_REGION_REPLY_JSON]');
    const bytes = fs.readFileSync(tableFile);
    function json(data, label) {
      try { return JSON.parse(data.toString('utf8').replace(/^\uFEFF/, '')); }
      catch { throw new Error('Invalid JSON in ' + label); }
    }
    const result = readAddressTable(json(bytes, 'address table export'), tableFile);
    result.tableSha256 = crypto.createHash('sha256').update(bytes).digest('hex');
    if (responseFile) result.matches = compareKeys(result.keys, json(fs.readFileSync(responseFile), 'decoded region reply'));
    console.log(JSON.stringify(result, null, 2));
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
