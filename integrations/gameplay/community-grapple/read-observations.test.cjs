'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {Readable} = require('node:stream');
const {summarize} = require('./read-observations.cjs');

test('同一生命和同一装备分别记录两次进入带有效锚型的锁定', async () => {
    const line = (ms, phase, anchor, event='change', slot=1) =>
        `[BRT-GRAPPLE] owner=1 life=1 slot=${slot} ms=${ms} event=${event} hook=${phase} cool=0 anchor=${anchor} permille=0\n`;
    const result = await summarize(Readable.from([
        line(0,2,1,'baseline'), line(80,2,1,'followup'), line(100,3,0),
        line(125,2,0), line(130,2,1), line(200,0,0), line(400,2,1),
        line(410,2,1,'slot-end'), line(420,2,1,'baseline',2),
        line(425,2,1,'followup',2), '[Launcher] private line\n'
    ]));
    assert.deepEqual(result.map(r=>r.observedAnchorLocksMs),[[130,400],[]]);
    assert.equal(result[0].closedBy,'slot-end');
    assert.ok(!JSON.stringify(result).includes('private'));
});
