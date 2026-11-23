'use strict';
const { randomUUID } = require('node:crypto');

// 必须由已完成玩家认证的专服调用。凭据只交给同机专服后台。
function createRoomSpawnClient({ origin, roomId, credential }) {
    const url = new URL(origin);
    if (url.protocol !== 'http:' || url.hostname !== '127.0.0.1' || url.username || url.password ||
        url.pathname !== '/' || url.search || url.hash || !/^[A-Za-z0-9_-]{1,80}$/.test(roomId) ||
        !/^[a-f0-9]{64}$/.test(credential)) throw Error('Invalid local match configuration');
    async function send(op, body) {
        const response = await fetch(new URL('/api/brt/v1/rooms/' + roomId + '/spawn/' + op, url), {
            method:'POST', redirect:'error', signal:AbortSignal.timeout(4000),
            headers:{'Content-Type':'application/json', Authorization:'Bearer ' + credential}, body:JSON.stringify(body),
        });
        if (response.status === 204) return;
        const payload = await response.json();
        if (!response.ok) throw Error(payload.error);
        return payload;
    }
    return {
        async admit(authenticatedSteamId, connectionId = randomUUID()) {
            await send('admit', {authenticatedSteamId,connectionId});
            // 先验证应答确属这一位和这一次出生，再交玩法拥有的实际生成函数。
            async function prepareEnvelope(life, selection) {
                    const result = await send('prepare', {connectionId,life,selection});
                    if (result.version !== 1 || result.roomId !== roomId || result.connectionId !== connectionId ||
                        result.authenticatedSteamId !== authenticatedSteamId || result.life !== life ||
                        result.loadout?.CharacterID !== selection.role.toUpperCase()) throw Error('Unexpected spawn response');
                    return result;
            }
            async function initialMenu() {
                const result = await send('menu', {connectionId});
                if (result.version !== 1 || result.roomId !== roomId || result.connectionId !== connectionId ||
                    result.authenticatedSteamId !== authenticatedSteamId || !Array.isArray(result.roles))
                    throw Error('Unexpected initial role menu');
                return result;
            }
            return { prepareEnvelope, initialMenu,
                prepareSaved: async (life, role) => (await prepareEnvelope(life, {role})).loadout,
                prepare: async (life, selection) => (await prepareEnvelope(life, selection)).loadout,
                leave: () => send('leave', {connectionId}),
            };
        },
    };
}

module.exports = { createRoomSpawnClient };
