'use strict';

// 此工厂随存储模块一起发布，依赖由档案端提供。
function createLoadoutProjection(getArchiveType, slotScopes) {
    const scopeBySlot = new Map(slotScopes);
    const partNames = ['UnexistedSlot', 'Muzzle', 'Barrel', 'HandGuard', 'Receiver_Upper',
        'Grip', 'Sight_Optical', 'Pointer', 'Sight_Iron', 'AmmoStorageDevice', 'Stock',
        'SlotTypeMax', 'ReceiverMain'];
    const slots = [
        ['primaryWeapon', 'FirstWeapon'], ['secondaryWeapon', 'SecondWeapon'],
        ['leftPylon', 'LeftPod', 'leftLauncher', 'leftPod'],
        ['rightPylon', 'RightPod', 'rightLauncher', 'rightPod'],
        ['meleeWeapon', 'MeleeWeapon'], ['mobilityModule', 'Mobility'],
    ];
    const record = value => value !== null && typeof value === 'object' && !Array.isArray(value);
    function invalid(message) {
        const error = new Error(message);
        error.status = 400;
        throw error;
    }
    function enumNumber(value, names) {
        if (Number.isInteger(value)) return value;
        if (typeof value !== 'string') return -1;
        if (/^\d+$/.test(value)) return Number(value);
        const name = value.split('::').pop().replaceAll('_', '');
        return names.findIndex(entry => entry.replaceAll('_', '') === name);
    }
    const characterNames = ['None', ...slots.map(([, name]) => name)];
    const characterSlot = row => enumNumber(row?.slotType ?? row?.characterSlot, characterNames);
    const partSlot = row => enumNumber(row?.slotType, partNames);

    function itemId(value) {
        const id = record(value) ? value.id ?? value.mobilityModuleId : value;
        return typeof id === 'string' ? id || 'None' : undefined;
    }
    function writeSlots(store, role, previous, target, input) {
        const inventory = Array.isArray(input.inventory?.slots) ? input.inventory.slots : [];
        const firstSave = Object.keys(target).length === 0;
        const hasWeaponSlots = input.primaryWeapon !== undefined || input.secondaryWeapon !== undefined ||
            inventory.some(row => [1, 2].includes(characterSlot(row)));
        const legacyPair = firstSave && !hasWeaponSlots ? Object.keys(input.weaponConfigs || {}) : [];
        for (const [index, [key, , launcher, pod]] of slots.entries()) {
            const values = [input[key], ...(launcher ? [input[launcher], input[pod]] : []),
                ...inventory.filter(row => characterSlot(row) === index + 1).map(row => row.itemId)];
            const ids = [...new Set(values.map(itemId).filter(id => id !== undefined))];
            if (!ids.length && index < 2 && legacyPair.length >= 2) ids.push(legacyPair[index]);
            if (!ids.length) continue;
            const changed = ids.filter(id => id !== previous[key]);
            if (changed.length > 1) invalid('Conflicting values for ' + key);
            const id = changed[0] ?? ids[0];
            if (id !== 'None' && !store.isAllowedForSlot(role, key, id)) invalid('Invalid ' + key + ': ' + id);
            target[key] = id;
        }
    }

    function decode(raw, roleId, weaponId) {
        if (typeof raw !== 'string' || !/^(?:[0-9a-f]{2})+$/i.test(raw)) return null;
        try {
            const request = getArchiveType().decode(Buffer.from(raw, 'hex'));
            if ((!request.RoleId || !roleId || request.RoleId.toUpperCase() === roleId.toUpperCase()) &&
                request.WeaponArchive?.WeaponId === weaponId) return request;
        } catch {}
        return null;
    }

    function mergeParts(store, roleId, weaponId, config, base, strict) {
        if (!Array.isArray(config?.parts) || !config.parts.length) return base;
        const { index, role } = store.getRoleDefinition(roleId);
        const definition = index.getWeapon(index.resolveBaseWeaponId(weaponId) || weaponId);
        if (!role?.weaponScope.has(weaponId) || !definition) {
            if (strict) invalid('Invalid role weapon: ' + weaponId);
            return base;
        }
        const message = decode(base, roleId, weaponId);
        if (!message) {
            if (strict) invalid('Missing weapon archive: ' + weaponId);
            return base;
        }
        const parts = message.WeaponArchive.Parts;
        const seen = new Set();
        let changed = false;
        for (const row of config.parts) {
            const slot = partSlot(row);
            const id = row?.weaponPartId;
            const scope = definition.slotScopes[scopeBySlot.get(slot)];
            const next = id === 'None' ? '' : id;
            const part = parts.find(entry => entry.SlotId === slot);
            const unchanged = part && part.PartId === next;
            if (typeof id !== 'string' || seen.has(slot) ||
                (!unchanged && (!scopeBySlot.has(slot) || (next && !scope?.has(next))))) {
                if (strict) invalid('Invalid part or duplicate slot for ' + weaponId + ' at ' + slot);
                continue;
            }
            seen.add(slot);
            if (unchanged) continue;
            if (part) part.PartId = next;
            else parts.push({ SlotId: slot, PartId: next });
            changed = true;
        }
        return changed ? Buffer.from(getArchiveType().encode(message).finish()).toString('hex') : base;
    }

    function legacyRaw(store, roleId, weaponId, snapshot) {
        return mergeParts(store, roleId, weaponId, snapshot?.weaponConfigs?.[weaponId],
            store.buildDefaultWeaponArchiveRaw(roleId, weaponId), false);
    }
    // 老档案进入缓存时补齐逐枪索引。只读请求不会调用 save。
    function hydrate(store, document) {
        for (const [roleId, data] of Object.entries(document?.roles || {})) {
            if (!record(data)) continue;
            const saved = { ...data._weaponArchives };
            const legacy = store.decodeWeaponArchiveRaw(data._weaponArchiveRaw);
            const { role } = store.getRoleDefinition(roleId);
            if (!role) continue;
            if (legacy && role.weaponScope.has(legacy.weaponId) && !saved[legacy.weaponId] &&
                decode(data._weaponArchiveRaw, roleId, legacy.weaponId)) {
                saved[legacy.weaponId] = data._weaponArchiveRaw;
            }
            for (const [id, config] of Object.entries(data.loadoutSnapshot?.weaponConfigs || {})) {
                if (saved[id] || !role.weaponScope.has(id) || !config?.parts?.length) continue;
                const raw = legacyRaw(store, roleId, id, data.loadoutSnapshot);
                if (raw) saved[id] = raw;
            }
            if (Object.keys(saved).length) data._weaponArchives = saved;
        }
    }
    function keepShape(previous, id, key) {
        if (!record(previous)) return id;
        return { ...previous, [key === 'mobilityModule' ? 'mobilityModuleId' : 'id']: id };
    }
    function weaponView(prior, weapon) {
        const oldRows = Array.isArray(prior?.parts) ? prior.parts : [];
        const bySlot = new Map(oldRows.map(part => [partSlot(part), part]));
        const parts = weapon.Parts.map(part => {
            const old = bySlot.get(part.SlotId);
            return { ...old, slotType: old?.slotType ?? part.SlotId, weaponPartId: part.PartId || 'None' };
        });
        return { ...prior, parts };
    }
    function read(store, roleId, snapshot, roleData) {
        const current = store.getNormalizedRoleData(roleId, roleData);
        const view = { ...snapshot };
        for (const [key, , launcher, pod] of slots) {
            const id = current[key] ?? 'None';
            view[key] = keepShape(view[key], id, key);
            if (launcher) {
                view[launcher] = keepShape(view[launcher], id, launcher);
                view[pod] = id;
            }
        }
        if (Array.isArray(view.inventory?.slots)) {
            view.inventory = { ...view.inventory, slots: view.inventory.slots.map(row => {
                const slot = slots[characterSlot(row) - 1];
                return slot ? { ...row, itemId: current[slot[0]] } : row;
            }) };
        }
        const configs = { ...view.weaponConfigs };
        for (const weapon of store.getNativeRoleWeaponConfig(roleId, roleData).Weapons) {
            configs[weapon.WeaponId] = weaponView(configs[weapon.WeaponId], weapon);
        }
        view.weaponConfigs = configs;
        return view;
    }

    function write(store, roleId, previous, envelope) {
        const input = record(envelope.loadoutSnapshot) ? envelope.loadoutSnapshot : envelope;
        const { role } = store.getRoleDefinition(roleId);
        if (!role || (input.roleId && input.roleId !== roleId) || (input.RoleID && input.RoleID !== roleId)) {
            invalid('Invalid role: ' + roleId);
        }
        const target = { ...previous };
        writeSlots(store, role, store.getNormalizedRoleData(roleId, previous), target, input);
        const archives = { ...previous._weaponArchives, ...input._weaponArchives, ...envelope._weaponArchives };
        for (const source of new Set([input, envelope])) {
            if (source._weaponArchives === undefined) continue;
            if (!record(source._weaponArchives)) invalid('Expected per-weapon archives object');
            for (const [id, raw] of Object.entries(source._weaponArchives)) {
                if (!role.weaponScope.has(id) || !decode(raw, roleId, id)) invalid('Mismatched weapon archive: ' + id);
            }
        }
        store.copyArchiveMetadata(target, input);
        if (envelope !== input) store.copyArchiveMetadata(target, envelope);
        if (input.weaponConfigs !== undefined && !record(input.weaponConfigs)) invalid('Expected weaponConfigs object');
        const priorSnapshot = previous.loadoutSnapshot;
        const configs = { ...priorSnapshot?.weaponConfigs };
        for (const [weaponId, config] of Object.entries(input.weaponConfigs || {})) {
            if (!role.weaponScope.has(weaponId) || !record(config)) invalid('Invalid weapon config: ' + weaponId);
            if (config.parts !== undefined && !Array.isArray(config.parts)) invalid('Expected parts array: ' + weaponId);
            const oldRows = configs[weaponId]?.parts;
            configs[weaponId] = { ...configs[weaponId], ...config };
            if (!config.parts?.length) continue;
            const stored = [archives[weaponId], target._weaponArchiveRaw].find(raw => decode(raw, roleId, weaponId));
            const base = stored || legacyRaw(store, roleId, weaponId, priorSnapshot);
            archives[weaponId] = mergeParts(store, roleId, weaponId, config, base, true);
            if (Array.isArray(oldRows)) {
                const rows = new Map(oldRows.map(row => [partSlot(row), row]));
                for (const row of config.parts) rows.set(partSlot(row), { ...rows.get(partSlot(row)), ...row });
                configs[weaponId].parts = [...rows.values()];
            }
        }
        if (Object.keys(archives).length) target._weaponArchives = archives;
        if (priorSnapshot || store.isStructuredRoleSnapshot(input) || envelope !== input) {
            const snapshot = { ...priorSnapshot, ...input };
            if (Object.keys(configs).length) snapshot.weaponConfigs = configs;
            for (const key of ['leftLauncher', 'rightLauncher', 'mobilityModule', 'meleeWeapon']) {
                if (record(priorSnapshot?.[key])) {
                    snapshot[key] = { ...priorSnapshot[key], ...(record(input[key]) ? input[key] : {}) };
                }
            }
            // 原始档案只在角色层维护，避免把旧快照里的字节再次当作当前档案。
            for (const key of Object.keys(snapshot)) {
                if (key.startsWith('_weaponArchive')) delete snapshot[key];
            }
            target.loadoutSnapshot = snapshot;
        }
        return target;
    }
    return { read, write, legacyRaw, decode, hydrate };
}

module.exports = { createLoadoutProjection };
