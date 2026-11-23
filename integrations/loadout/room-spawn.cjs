'use strict';
const crypto = require('node:crypto');
const { scopes, checkedParts, priorFor, decodeSaved } = require('./spawn-parts.cjs');
const receiverCatalog = require('./receiver-main.json');

const roles = new Set(['PEACE', 'PROBE', 'FIXER', 'SNIPER', 'FORT', 'SPIKE']);
const roomId = /^[A-Za-z0-9_-]{1,80}$/;
const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
function fail(status, code) { const error = new Error(code); error.status = status; throw error; }
function shape(value, keys) {
    if (!value || typeof value !== 'object' || Array.isArray(value) ||
        Object.keys(value).length !== keys.length || !keys.every(k => Object.hasOwn(value, k))) fail(400, 'invalid-fields');
}
function account(value) {
    if (typeof value !== 'string' || !/^76561\d{12}$/.test(value)) return false;
    const number = BigInt(value);
    return number > 76561197960265728n && number <= 76561202255233023n;
}

const nameKey = value => typeof value === 'string' ? value.toLowerCase() : '';

function rememberReceiverSelection(role, id, original, merged, type) {
    const input = type.decode(original).WeaponArchive?.Skin?.SkinInfo;
    // 仅改枪饰、准星或部件时，原始 protobuf 中没有整枪颜色字段。
    if (!input || !Object.hasOwn(input, 'Id')) return;
    const value = type.decode(merged).WeaponArchive?.Skin?.SkinInfo;
    const colors = { ...role._explicitReceiverColors };
    if (input.Id && nameKey(input.Id) !== 'none' && value?.Type && nameKey(input.Id) === nameKey(value.Id))
        colors[id] = [value.Type, value.Id];
    else delete colors[id];
    role._explicitReceiverColors = colors;
}

function receiverAppearance(index, id, skin, factory, explicitColor) {
    // 只有同一枪档案实际保存套装和涂装时才申请更换机匣材质。
    const look = skin?.SkinInfo;
    if (!look || !Object.hasOwn(look, 'Type') || !Object.hasOwn(look, 'Id')) return;
    // 老写入器给未改整枪的存档补了出厂外观。它与手选出厂色同值，交给原生默认机匣。
    const recorded = Array.isArray(explicitColor) && nameKey(explicitColor[0]) === nameKey(look.Type) &&
        nameKey(explicitColor[1]) === nameKey(look.Id);
    if (!recorded && nameKey(look.Type) === nameKey(factory?.SkinInfo?.Type) &&
        nameKey(look.Id) === nameKey(factory?.SkinInfo?.Id)) return;
    const base = index.resolveBaseWeaponId(id);
    if (!Object.hasOwn(receiverCatalog.weapons, base)) return;
    const gun = receiverCatalog.weapons[base];
    const suitId = gun.suits.find(s => nameKey(s) === nameKey(look.Type));
    if (!suitId) return;
    const suit = receiverCatalog.suits[suitId];
    const paintId = Object.keys(suit.paints).find(p => nameKey(p) === nameKey(look.Id));
    if (!paintId) return;
    return { WeaponPartID: gun.part, WeaponPartSkinID: suit.skin, WeaponPartSkinPaintingID: suit.paints[paintId] };
}

function weapon(store, type, role, data, selected) {
    if (selected === 'None') return { WeaponID: 'None', WeaponPartSlotTypeArray: [], WeaponPartConfigs: [] };
    const id = store.nativeWeaponId(role, selected);
    if (typeof selected !== 'string' || !id) fail(422, 'weapon-outside-selected-role');
    const stock = decodeSaved(store, type, store.buildDefaultWeaponArchiveRaw(role, id), role, id);
    const parts = checkedParts(store, id, stock.WeaponArchive.Parts);
    if (parts.size !== scopes.length) fail(409, 'incomplete-retail-parts');
    // 出厂表只补齐从未修改的槽，同枪明确卸下也是有效选择。
    const saved = priorFor(store, data, role, id);
    let appearance = stock.WeaponArchive.Skin;
    let receiver;
    if (saved !== null) {
        const source = decodeSaved(store, type, saved, role, id);
        for (const part of source.WeaponArchive.Parts) parts.set(part.SlotId, part);
        appearance = source.WeaponArchive.Skin || appearance;
        receiver = receiverAppearance(store.getRoleDefinition(role).index, id, source.WeaponArchive.Skin,
            stock.WeaponArchive.Skin, data._explicitReceiverColors?.[id]);
    }
    const slots = [...parts.keys()].sort((a, b) => a - b);
    const compact = value => typeof value === 'string' && value ? value : 'None';
    const view = item => ({ SkinItem:compact(item?.Info?.Type ?? item?.SkinInfo?.Type),
        PaintItem:compact(item?.Info?.Id ?? item?.SkinInfo?.Id), SpecialSlot:compact(item?.SpecialSlot),
        Ornament:compact(item?.Ornament ?? item?.WeaponOrnament) });
    const result = { WeaponID: id, OrnamentID: compact(appearance?.WeaponOrnament), ArchiveAppearance:view(appearance),
        WeaponPartSlotTypeArray: slots, WeaponPartConfigs: slots.map(key => {
            const row = parts.get(key), look = view(row.Ornament);
            return { WeaponPartID:compact(row.PartId), WeaponPartSkinID:look.SkinItem, WeaponPartSpecialSkinID:look.SpecialSlot,
                WeaponPartSkinPaintingID:look.PaintItem, ArchiveOrnament:look.Ornament };
        }) };
    if (receiver) result.ReceiverMainAppearance = receiver;
    // 原生 protobuf 没有 WeaponClassID；结构化存档若明确提供才发布，其他情况由当前实际武器保留。
    const legacy = data.loadoutSnapshot?.weaponConfigs?.[id];
    const knownClass = legacy?.weaponClassId ?? legacy?.WeaponClassID;
    if (knownClass !== undefined) {
        if (typeof knownClass !== 'string' || !/^[A-Za-z0-9_-]{1,128}$/.test(knownClass)) fail(409,'invalid-saved-class-id');
        result.WeaponClassID = knownClass;
    }
    return result;
}

function playerRoles(store, player, localOwner) {
    let document = store.load(player);
    if (document === null && localOwner === player) {
        // 已登录的本机房主可能从未改装；只有确实不存在的个人档才能使用只读出厂装备。
        try {
            if (!require('node:fs').lstatSync(store._playerPath(player), {throwIfNoEntry:false}))
                document = {playerId:player, roles:{}};
        } catch { /* 读取错误仍按原行为拒绝。 */ }
    }
    if (!document || document.playerId !== player || !document.roles) fail(404, 'player-profile-missing');
    if (typeof document.roles !== 'object' || Array.isArray(document.roles)) fail(409, 'invalid-saved-role');
    const result = new Map();
    for (const [name, data] of Object.entries(document.roles)) {
        const role = name.toUpperCase();
        if (!roles.has(role)) continue;
        if (result.has(role) || !data || typeof data !== 'object' || Array.isArray(data))
            fail(409, 'invalid-saved-role');
        result.set(role, data);
    }
    // 已认证的个人档只保存编辑过的干员；其余人用各自原厂装备，不能从别的玩家补。
    for (const role of roles) if (!result.has(role)) result.set(role, {});
    return result;
}

function normalizedEquipment(store, role, data) {
    const effective = store.getNormalizedRoleData(role, data);
    for (const key of ['leftPylon', 'rightPylon', 'mobilityModule', 'meleeWeapon']) {
        const actual = store.toFlatItemId(data[key], null);
        if (actual === null || actual === 'None') continue;
        const scoped = key === 'meleeWeapon' ?
            (store.getRoleDefinition(role).role.meleeWeaponScope.has(actual) ? actual : null) :
            store.scopedEquipmentId(role, key, actual);
        if (!scoped || effective[key] !== scoped) fail(409, 'invalid-saved-equipment');
    }
    return effective;
}

// 只投递本人明确保存的角色外观。缺少字段与选择「卸下」在原作档案里含义不同。
function characterAppearance(store, role, data) {
    const { role:definition } = store.getRoleDefinition(role);
    const result = {};
    if (['_skinData','_skinToken','_ornamentId'].some(key => Object.hasOwn(data, key))) {
        const suit = store.getNativeRoleSkinConfig(role, data).Suit;
        const legacy = role === 'PEACE' && suit.SkinItem === 'PEACE_TOK-DOS-01';
        if (!legacy && !definition.spaceSuitSkinScope.has(suit.SkinItem)) fail(409, 'invalid-saved-character-suit');
        const raw = suit.PaintItem || '';
        const paint = legacy ? (raw === '' || raw === 'None' || raw === suit.SkinItem + '_PTOriginal' ? raw : null)
            : require('./role-paint-scope.cjs').canonicalCharacterPaint(suit.SkinItem, raw);
        // 老版本允许所有同前缀且属于角色涂装类型的值。仅对这种已经落盘的旧值跳过配色，保留原始档案和当次其余装备。
        const staleLegacyPaint = paint === null && raw.startsWith(suit.SkinItem + '_') &&
            store.getRoleDefinition(role).index.getItemType(raw) === 'EPBItemType::CharacterSuitePainting';
        if (paint === null && !staleLegacyPaint) fail(409, 'invalid-saved-character-paint');
        result.SpacesuitID = suit.SkinItem;
        result.SpacesuitSkinPaintingID = paint || 'None';
    }
    for (const [key, field, scope] of [['_armBadgeId','ArmBadgeID','armBadgeScope'],
                                      ['_headAccessoryId','HeadAccessoryID','headAccessoryScope']]) {
        if (!Object.hasOwn(data, key)) continue;
        const value = data[key] === '' ? 'None' : data[key];
        if (typeof value !== 'string' || (value !== 'None' && !definition[scope].has(value)))
            fail(409, 'invalid-saved-character-accessory');
        result[field] = value;
    }
    return Object.keys(result).length ? result : undefined;
}

// 选角使用的六个槽按游戏 EPBCharacterSlotType 排列，与档案写入的操作编号不同。
const menuSlots = ['primaryWeapon', 'secondaryWeapon', 'leftPylon', 'rightPylon', 'meleeWeapon', 'mobilityModule'];
function initialMenu(store, player, localOwner) {
    const entries = [];
    for (const [role, data] of playerRoles(store, player, localOwner)) {
        const effective = normalizedEquipment(store, role, data);
        const inventory = menuSlots.map(key => effective[key]);
        if (inventory.some(id => typeof id !== 'string' || !id.length)) fail(409, 'invalid-role-menu');
        entries.push({ CharacterID:role, InventoryItems:inventory });
    }
    return entries;
}

function spawnSelection(input) {
    const saved = input != null && !Object.hasOwn(input, 'primary') && !Object.hasOwn(input, 'secondary');
    shape(input, saved ? ['role'] : ['role', 'primary', 'secondary']);
    if (typeof input.role !== 'string' || !roles.has(input.role.toUpperCase())) fail(422, 'invalid-selected-role');
    const role = input.role.toUpperCase();
    if (saved) return {mode:'saved', role};
    if (typeof input.primary !== 'string' || typeof input.secondary !== 'string') fail(422, 'weapon-outside-selected-role');
    return {mode:'chosen', role, primary:input.primary, secondary:input.secondary};
}

function savedWeapon(store, role, data, effective, key) {
    if (Object.hasOwn(data, key)) {
        const raw = store.toFlatItemId(data[key], null);
        const id = raw === 'None' ? raw : store.nativeWeaponId(role, raw);
        // 档案存在但不属于这个干员时，不能用正常化器补的原厂枪冒充读档成功。
        if (!id || effective[key] !== id) fail(409, 'invalid-saved-weapon-slot');
    }
    return effective[key];
}

function selectedLoadout(store, type, player, selection, localOwner) {
    const role = selection.role;
    const data = playerRoles(store, player, localOwner).get(role);
    if (!data) fail(409, 'selected-role-not-uniquely-saved');
    const effective = normalizedEquipment(store, role, data);
    try {
        const character = characterAppearance(store, role, data);
        return { CharacterID: role, ...(character ? { CharacterData:character } : {}),
            FirstWeaponPartData: weapon(store, type, role, data, selection.mode === 'saved' ?
                savedWeapon(store, role, data, effective, 'primaryWeapon') : selection.primary),
            SecondWeaponPartData: weapon(store, type, role, data, selection.mode === 'saved' ?
                savedWeapon(store, role, data, effective, 'secondaryWeapon') : selection.secondary),
            LeftLauncherData: { ID: effective.leftPylon }, RightLauncherData: { ID: effective.rightPylon },
            MobilityModuleData: { MobilityModuleID: effective.mobilityModule }, MeleeWeaponData: { ID: effective.meleeWeapon } };
    } catch (error) {
        if (error.status) throw error;
        fail(409, 'invalid-saved-weapon-config');
    }
}

// 普通专服应用使用各自的房间密钥。旧大厅票据、客户端提供的玩家 ID 和公共 REST 不参与出生认证。
function installRoomSpawn(app, getStore, type, configured = process.env.BRT_SPAWN_ROOMS,
    signer = process.env.BRT_PROFILE_ROOM_SIGNER) {
    delete process.env.BRT_PROFILE_ROOM_SIGNER;
    const signingKey = typeof signer === 'string' && /^[a-f0-9]{64}$/.test(signer) ? Buffer.from(signer, 'hex') : null;
    const definitions = configured ? JSON.parse(configured) : {};
    if (!definitions || typeof definitions !== 'object' || Array.isArray(definitions) ||
        (configured && !Object.keys(definitions).length) || Object.keys(definitions).length > 64) throw Error('Invalid BRT spawn rooms');
    const digest = key => crypto.createHash('sha256').update(Buffer.from(key, 'hex')).digest();
    const rooms = new Map(), distinct = new Set();
    for (const [id, secret] of Object.entries(definitions)) {
        if (!roomId.test(id) || typeof secret !== 'string' || !/^[0-9a-f]{64}$/.test(secret) || distinct.has(secret))
            throw Error('Invalid or reused BRT room credential');
        rooms.set(id, { verifier:digest(secret), sessions:new Map(), accounts:new Map() });
        distinct.add(secret);
    }
    // 本机菜单只有一个正在交接或游玩的专服。登记来自大厅控制连接，HTTP 没有登记接口。
    let local;
    function retire() {
        if (!local) return;
        clearInterval(local.timer);
        local.sessions.clear(); local.accounts.clear();
        local = undefined;
    }
    function current() {
        if (local) {
            try { if (!local.live()) retire(); } catch { retire(); }
        }
        return local;
    }
    function trace(room, event, life) {
        if (room?.owner) console.log('[PVE-SPAWN] ' + JSON.stringify({run:room.run,choice:room.id.slice(4), event, life}));
    }
    function registerLocal(choice, owner, proof, run, live) {
        if (!uuid.test(choice) || !account(owner) || owner !== proof?.owner || typeof proof.verifier !== 'string' ||
            !/^[0-9a-f]{64}$/.test(proof.verifier) || !signingKey || !uuid.test(run) ||
            typeof proof.signature !== 'string' || !/^[0-9a-f]{64}$/.test(proof.signature) || typeof live !== 'function') return false;
        const mac = crypto.createHmac('sha256', signingKey).update(
            ['brt-local-room-v1',run,choice,owner,proof.verifier].join('\n')).digest();
        if (!crypto.timingSafeEqual(mac,Buffer.from(proof.signature,'hex')) || !live()) return false;
        const id = 'pve-' + choice;
        if (rooms.has(id)) return false;
        if (current()?.id === id) return false;
        retire();
        local = { id, run, owner, verifier:Buffer.from(proof.verifier,'hex'), sessions:new Map(), accounts:new Map(), live,
            timer:setInterval(current, 1000).unref() };
        trace(local, 'room-registered');
        return true;
    }
    const cutoff = 2 * 60 * 60 * 1000;
    app.post('/api/brt/v1/rooms/:room/spawn/:op', (req, res) => {
        res.set('Cache-Control', 'no-store');
        try {
            const supplied = /^Bearer ([0-9a-f]{64})$/.exec(req.headers.authorization || '');
            if (!['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(req.socket.remoteAddress) || !supplied) fail(403, 'room-auth-required');
            const room = rooms.get(req.params.room) || (local?.id === req.params.room ? current() : undefined);
            if (!room || !crypto.timingSafeEqual(room.verifier, digest(supplied[1]))) fail(403, 'room-auth-required');
            const now = Date.now(), body = req.body;
            function drop(key, member) {
                room.sessions.delete(key);
                if (room.accounts.get(member.player) === key) room.accounts.delete(member.player);
            }
            for (const [key, member] of room.sessions) if (now - member.touched > cutoff) drop(key, member);
            switch (req.params.op) {
            case 'birth-plan': {
                shape(body, ['connectionId', 'authenticatedSteamId', 'life', 'selection']);
                if (!room.owner || room.owner !== body.authenticatedSteamId) fail(403, 'wrong-local-player');
                if (!uuid.test(body.connectionId) || !Number.isSafeInteger(body.life) || body.life < 1)
                    fail(400, 'invalid-birth-request');
                const selected = spawnSelection(body.selection);
                const loadout = selectedLoadout(getStore(), type(), room.owner, selected, room.owner);
                return res.json({ version:1, roomId:room.id, connectionId:body.connectionId,
                    authenticatedSteamId:room.owner, life:body.life, loadout });
            }
            case 'session-menu': {
                shape(body, ['connectionId', 'previousConnectionId', 'authenticatedSteamId']);
                if (!room.owner || room.owner !== body.authenticatedSteamId) fail(403, 'wrong-local-player');
                const { connectionId, previousConnectionId } = body;
                if (!uuid.test(connectionId) || (previousConnectionId !== null && !uuid.test(previousConnectionId)) ||
                    previousConnectionId === connectionId) fail(400, 'invalid-login-request');
                const previous = room.loginSession;
                const active = room.sessions.get(room.accounts.get(room.owner));
                if (previous && previous.connectionId === connectionId) {
                    if (previous.previousConnectionId !== previousConnectionId || previous.applied) fail(409, 'initial-menu-already-used');
                    return res.json(previous.snapshot);
                }
                if (previous ? previous.connectionId !== previousConnectionId :
                    previousConnectionId !== null || room.loginMenuSent || active?.menu || active?.life)
                    fail(409, 'login-connection-mismatch');
                const roles = initialMenu(getStore(), room.owner, room.owner);
                const snapshot = {version:1, roomId:room.id, connectionId, authenticatedSteamId:room.owner, roles};
                room.loginSession = {connectionId, previousConnectionId, applied:false, snapshot};
                room.loginMenuSent = true;
                return res.json(snapshot);
            }
            case 'session-menu-applied': {
                shape(body, ['connectionId', 'authenticatedSteamId']);
                if (!room.owner || room.owner !== body.authenticatedSteamId) fail(403, 'wrong-local-player');
                if (!uuid.test(body.connectionId)) fail(400, 'invalid-login-request');
                if (!room.loginSession || room.loginSession.connectionId !== body.connectionId)
                    fail(409, 'login-connection-mismatch');
                room.loginSession.applied = true;
                delete room.loginSession.snapshot;
                return res.status(204).end();
            }
            case 'login-menu': {
                shape(body, ['connectionId', 'authenticatedSteamId']);
                if (!room.owner || room.owner !== body.authenticatedSteamId) fail(403, 'wrong-local-player');
                if (!uuid.test(body.connectionId)) fail(400, 'invalid-login-request');
                const active = room.sessions.get(room.accounts.get(room.owner));
                if (room.loginMenuSent || active?.menu || active?.life) fail(409, 'initial-menu-already-used');
                const roles = initialMenu(getStore(), room.owner, room.owner);
                room.loginMenuSent = true;
                return res.json({version:1, roomId:room.id, connectionId:body.connectionId,
                    authenticatedSteamId:room.owner, roles});
            }
            case 'admit': {
                shape(body, ['connectionId', 'authenticatedSteamId']);
                if (!uuid.test(body.connectionId) || !account(body.authenticatedSteamId)) fail(400, 'invalid-authenticated-member');
                if (room.owner && room.owner !== body.authenticatedSteamId) fail(403, 'wrong-local-player');
                const old = room.sessions.get(body.connectionId);
                if (old && (old.player !== body.authenticatedSteamId || old.closed)) fail(409, 'connection-already-bound');
                if (old) { old.touched = now; break; }
                if (room.sessions.size >= 1024) fail(503, 'room-member-limit');
                const previous = room.sessions.get(room.accounts.get(body.authenticatedSteamId));
                if (previous) previous.closed = true;
                room.sessions.set(body.connectionId, {player:body.authenticatedSteamId, touched:now, closed:false, life:0});
                room.accounts.set(body.authenticatedSteamId, body.connectionId);
                trace(room, 'member-admitted');
                break;
            }
            case 'menu':
            case 'leave':
            case 'prepare': {
                const leave = req.params.op === 'leave';
                shape(body, req.params.op === 'prepare' ? ['connectionId', 'life', 'selection'] : ['connectionId']);
                const member = room.sessions.get(body.connectionId);
                if (!member || member.closed || room.accounts.get(member.player) !== body.connectionId) fail(403, 'inactive-member');
                member.touched = now;
                if (leave) { member.closed = true; room.accounts.delete(member.player); break; }
                if (req.params.op === 'menu') {
                    if (member.life !== 0) fail(409, 'first-menu-closed');
                    if (!member.menu) {
                        member.menu = { version:1, roomId:req.params.room, connectionId:body.connectionId,
                            authenticatedSteamId:member.player, roles:initialMenu(getStore(), member.player, room.owner) };
                        trace(room, 'menu-read');
                    }
                    return res.json(member.menu);
                }
                if (!Number.isSafeInteger(body.life) || body.life < 1) fail(400, 'invalid-life');
                const selected = spawnSelection(body.selection);
                const signature = JSON.stringify(selected);
                const deliver = value => {
                    if (room.owner) res.once('finish', () => {
                        if (!member.closed && local === room && (member.sentLife || 0) < value.life) {
                            member.sentLife = value.life; trace(room, 'loadout-returned', value.life);
                        }
                    });
                    return res.json(value);
                };
                if (body.life === member.life && member.signature === signature) return deliver(member.output);
                if (body.life !== member.life + 1) fail(409, 'stale-or-conflicting-life');
                const loadout = selectedLoadout(getStore(), type(), member.player, selected, room.owner);
                const result = { version:1, roomId:req.params.room, connectionId:body.connectionId,
                    authenticatedSteamId:member.player, life:body.life, loadout };
                member.signature = signature;
                member.life = body.life;
                member.output = result;
                return deliver(result);
            }
            default: fail(404, 'unknown-operation');
            }
            return res.status(204).end();
        } catch (error) {
            return res.status(error.status || 500).json({ error: error.status ? error.message : 'spawn-service-failed' });
        }
    });
    // 这条原生 RPC 返回可用装备目录，无配件、槽位占用或出生回执。
    let matchCatalogTypes;
    registerLocal.matchCatalog = (socket, request, protobuf) => {
        const room = current();
        const owner = room?.owner;
        const loopback = ip => ip === '127.0.0.1' || ip === '::ffff:127.0.0.1' || ip === '::1';
        if (!owner || socket?.joinAuthenticated !== true || socket.playerId !== owner || !loopback(socket.localAddress) ||
            !loopback(socket.remoteAddress) || !request || request.length > 128) return null;
        if (!matchCatalogTypes) {
            const schema = protobuf.parse(`syntax = "proto3";
                message MatchCatalogRequest { string userId = 1; }
                message RoleItems { string characterId = 1; repeated string pods = 2;
                    repeated string mobility = 3; repeated string melee = 4; repeated string weapons = 5; }
                message MatchCatalogResponse { string userId = 1; repeated RoleItems equipment = 2; }`).root;
            matchCatalogTypes = [schema.lookupType('MatchCatalogRequest'), schema.lookupType('MatchCatalogResponse')];
        }
        const [input, output] = matchCatalogTypes;
        try { if (input.decode(request).userId !== owner) return null; }
        catch { return null; }
        const store = getStore(), equipment = [];
        for (const role of roles) {
            const def = store.getRoleDefinition(role).role;
            if (!def) return null;
            equipment.push({ characterId:role, pods:[...def.podScope], mobility:[...def.mobilityScope],
                melee:[...def.meleeWeaponScope], weapons:[...def.weaponScope] });
        }
        // 普通本机房使用和原生 QueryAssets 一致的全解锁目录。原作另有一个语义未确定的数字字段，保留零值。
        return Buffer.from(output.encode({userId:owner, equipment}).finish());
    };
    return registerLocal;
}

module.exports = { installRoomSpawn, rememberReceiverSelection };
