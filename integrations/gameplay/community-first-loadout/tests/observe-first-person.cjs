// 对本地联调客户端做只读采样。请先启动联调、进入选角页，再运行此脚本并正常选角。
// node observe-first-person.cjs <联调目录> [观察秒数]
const fs = require("node:fs");
const path = require("node:path");
const { setTimeout: sleep } = require("node:timers/promises");

async function main() {
    if (!process.argv[2]) throw Error("Missing test directory");
    const lab = path.resolve(process.argv[2]);
    const run = fs.readFileSync(path.join(lab, "current.txt"), "utf8").trim();
    const journal = path.join(lab, "retail/ProjectBoundary/Binaries/Win64/logs/BRT-rig-client");
    const read = () => fs.readFileSync(journal + ".out", "utf8").replaceAll("\r", "");
    const clientPid = JSON.parse(fs.readFileSync(path.join(run, "pids.json"), "utf8"))[2];
    if (!read().includes("rig-ready side=client pid=" + clientPid)) throw Error("Client PID does not match the run");
    const log = path.join(run, "first-person-" + Date.now() + ".log");

    async function query(commands) {
        const offset = read().length;
        fs.appendFileSync(journal + ".cmd", commands.join("\n") + "\n");
        const end = Date.now() + 4000;
        while (Date.now() < end) {
            await sleep(100);
            const extra = read().slice(offset);
            if (extra.includes("> " + commands.at(-1) + "\n")) {
                await sleep(60);
                const result = read().slice(offset);
                fs.appendFileSync(log, result);
                return result;
            }
        }
        throw Error("No client response");
    }

    const props = (obj, names) => names.map(name => "props " + obj + " " + name);
    const ref = (output, property) => output.match(new RegExp("\\b" + property + "=(#\\d+)"))?.[1];
    const seconds = Number(process.argv[3] || 35);
    if (!(seconds > 0 && seconds <= 300)) throw Error("Duration must be 1..300 seconds");
    const stop = Date.now() + seconds * 1000;
    const photographed = new Set();
    while (Date.now() < stop) {
        const state = await query([...props("pawn", ["CurrentWeapon", "PendingWeapon", "Inventory", "HandsState", "Mesh1P"]),
            ...props("pc", ["bShowSelectRole", "HiddenPrimitiveComponents"]), "call pc GetViewTarget", "call pawn IsFirstPerson",
            "call pawn IsAlive", "props ps PossessedCharacterId", "call gs IsRoundInProgress"]);
        const gun = ref(state, "CurrentWeapon") || ref(state, "PendingWeapon") || state.match(/Inventory=\[\d+\]:(#\d+)/)?.[1];
        if (gun) {
            const held = await query([...props(gun, ["Mesh1P", "MyPBCharacter", "TopOwner", "bHidden"]), "call " + gun + " IsDeployed",
                "call " + gun + " GetCurrentStateString", "call " + gun + " GetCurrentAmmoInClip"]);
            const mesh = ref(held, "Mesh1P");
            if (mesh) await query([...props(mesh, ["AttachParent", "AttachSocketName", "SkeletalMesh", "bHiddenInGame",
                "bVisible", "bOwnerNoSee", "bOnlyOwnerSee"]), "call " + mesh + " GetNumChildrenComponents"]);
            // 开场动画也会部署武器。只截摄像机已归还给存活玩家自己的那几帧。
            const view = state.match(/GetViewTarget -> ReturnValue=(#\d+):/)?.[1];
            const subject = state.match(/called (#\d+):[^\s]+\.IsAlive -> ReturnValue=1,/)?.[1];
            const active = state.includes("IsRoundInProgress -> ReturnValue=1,");
            const picture = ref(state, "CurrentWeapon") ? "equipped" : "empty";
            if (active && !photographed.has(picture) && view && view === subject) {
                await query(["call cdo:KismetSystemLibrary ExecuteConsoleCommand world Shot pc"]);
                photographed.add(picture);
            }
        }
        await sleep(500);
    }
    console.log(log);
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
