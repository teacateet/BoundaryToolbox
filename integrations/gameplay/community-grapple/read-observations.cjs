'use strict';
const fs = require('node:fs');
const { createInterface } = require('node:readline');

const row = /^\[BRT-GRAPPLE\] owner=([1-9]\d{0,5}) life=([1-9]\d{0,5}) slot=([1-9]\d{0,5}) ms=(\d{1,8}) event=(baseline|change|followup|slot-end|life-end|limit) hook=([0-3]) cool=([01]) anchor=([01]) permille=(-1|1000|\d{1,3})$/;

async function summarize(input) {
    const result = [];
    const groups = new Map();
    for await (const line of createInterface({ input, crlfDelay: Infinity })) {
        const match = row.exec(line);
        if (!match) continue;
        const [, owner, life, slot, time, event, phase, cooling, anchor] = match;
        const key = [owner, life, slot].join('/');
        const ms = Number(time);
        let group = groups.get(key);
        if (!group && event === 'baseline') {
            const report = {owner:Number(owner), life:Number(life), slot:Number(slot),
                observedAnchorLocksMs:[], coolingBeganMs:[], coolingEndedMs:[], closedBy:null};
            result.push(report);
            group = {report, previousPhase:phase, previousCooling:cooling, acquiring:false, last:ms};
            groups.set(key, group);
            continue;
        }
        if (!group || group.report.closedBy || ms < group.last || event === 'baseline') continue;
        group.last = ms;
        if (event === 'slot-end' || event === 'life-end' || event === 'limit') {
            group.report.closedBy = event;
            continue;
        }
        if (phase !== '2') group.acquiring = false;
        else if (group.previousPhase !== '2') group.acquiring = true;
        if (group.acquiring && anchor === '1') {
            group.report.observedAnchorLocksMs.push(ms);
            group.acquiring = false;
        }
        if (cooling !== group.previousCooling) {
            const times = cooling === '1' ? group.report.coolingBeganMs : group.report.coolingEndedMs;
            times.push(ms);
        }
        group.previousPhase = phase;
        group.previousCooling = cooling;
    }
    return result;
}
module.exports = { summarize };
if (require.main === module) {
    if (process.argv.length !== 3) {
        console.error('Usage: node read-observations.cjs <one-room-log>');
        process.exitCode = 2;
    } else {
        summarize(fs.createReadStream(process.argv[2], {encoding:'utf8'}))
            .then(rows => console.log(JSON.stringify(rows, null, 2)))
            .catch(() => { console.error('Cannot read room log.'); process.exitCode = 1; });
    }
}
