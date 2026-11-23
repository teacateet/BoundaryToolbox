"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const { archiveContext, bindingReport, main, matchSessions, savedSettings, settingDelta } = require("./inspect.cjs");

const kb = "[/Script/ProjectBoundary.PBKeyboardInputSettings]";
const settings = "[/Script/ProjectBoundary.ProjectBoundaryUserSettings]";
const fov = n => settings + "\nFOV=" + n;
const action = (name, key, extra = "") => 'ActionMappings=(ActionName="' + name + '",Key=' + key + extra + ")";

function scratch(t) {
  const base = fs.realpathSync(__dirname);
  const dir = fs.mkdtempSync(path.join(base, ".inspect-test-"));
  t.after(() => {
    assert.equal(path.dirname(fs.realpathSync(dir)), base);
    assert.match(path.basename(dir), /^\.inspect-test-[a-z0-9]+$/i);
    fs.rmSync(dir, { recursive: true });
  });
  return dir;
}

test("only keyboard and engine input contexts; preserve UE array operators and chords", () => {
  const sample = ["\uFEFF" + kb, "+AxisMappings=(AxisName=Keyboard_MoveUp,Scale=1.0,Key=SpaceBar)",
    "!ActionMappings=ClearArray", "+" + action("Mark", "M"), "-" + action("Menu", "Escape"),
    "." + action("Menu", "F2", ",bCtrl=True"), action("Other", "F2"),
    "[/Script/Engine.InputSettings]", action("EngineMenu", "Escape"), action("MenuPad", "Gamepad_FaceButton_Top"),
    "[/Script/ProjectBoundary.PBXBOXInputSettings]", action("Mark", "None"),
    "[/Script/Engine.PlayerInput]", '+DebugExecBindings=(Key=F2,Command="viewmode unlit")',
    "[/Script/Engine.Console]", "HistoryBuffer=private-string", "; " + action("Menu", "Escape")].join("\r\n");
  const r = bindingReport(sample);
  assert.deepEqual(r.actionEntries, { keyboard: 4, engine: 2 });
  assert.equal(r.axisEntries.keyboard, 1);
  assert.deepEqual(r.arrayClears, [{ line: 3, scope: "keyboard", array: "actionmappings" }]);
  assert.deepEqual(r.relevant.map(x => [x.action, x.key, x.op, x.modifiers]), [
    ["Mark", "M", "add", []], ["Menu", "Escape", "remove", []], ["Menu", "F2", "append", ["Ctrl"]],
    ["Other", "F2", "entry", []], ["EngineMenu", "Escape", "entry", []],
  ]);
  assert.doesNotMatch(JSON.stringify(r), /private-string|viewmode|Gamepad|MenuPad/);
});

test("axes alone do not imply a missing action; bad rows are reported without disclosing content", () => {
  const r = bindingReport(kb + "\nAxisMappings=(Key=C,Scale=-1,AxisName=Keyboard_MoveUp)\n;ActionMappings=(Key=F2)");
  assert.deepEqual([r.actionEntries.keyboard, r.axisEntries.keyboard, r.relevant.length], [0, 1, 0]);
  const bad = bindingReport(kb + "\nActionMappings=(Other=private-text)\n+ActionMappings=(Key=None, ActionName=Mark)");
  assert.deepEqual(bad.malformed, [{ line: 2, scope: "keyboard", array: "actionmappings" }]);
  assert.equal(bad.relevant[0].key, "None");
  assert.doesNotMatch(JSON.stringify(bad), /private-text/);
});

test("FOV comparison uses the game-specific section; unknown is not zero or an observed keypress", () => {
  const a = savedSettings(fov("80.000000") + "\nMarkMethod=0\n[Other]\nFOV=20");
  const b = savedSettings(fov("81") + "\nMarkMethod=0");
  assert.deepEqual(settingDelta(a, b), { before: 80, after: 81, changed: true });
  assert.equal(settingDelta(a, savedSettings(fov("80"))).changed, false);
  for (const input of ["[Other]\nFOV=81", fov(""), fov("unknown"), fov("Infinity")])
    assert.deepEqual(settingDelta(a, savedSettings(input)), { before: 80, after: null, changed: null });
  assert.equal(a.MarkMethod.value, 0);
});

test("per-map sessions preserve source line numbers, distinguish starting selection from confirming a role", () => {
  const r = matchSessions(["[SERVER] Map loaded: Warehouse", "[SERVER] Mode: /Game/BP_PBGameMode_Rush_PVE_Normal.BP_PBGameMode_Rush_PVE_Normal_C",
    "[HEARTBEAT] PlayerCount = 2", "All players connected, beginning role selection flow!", "Selecting role...",
    "[SERVER_LIFECYCLE] match_started", "[HEARTBEAT] PlayerCount = 6", "[SERVER_LIFECYCLE] match_end_detected reason=fixture",
    "[SERVER] Map loaded: CircularX", "[HEARTBEAT] PlayerCount = 0"].join("\r\r\n"));
  assert.deepEqual(r[0], { map: "Warehouse", line: 1, mode: "Rush_PVE_Normal", starts: [6], ends: [8],
    selectionFlow: [{ line: 4, event: "begin" }, { line: 5, event: "selecting" }], peakPlayerCount: 6 });
  assert.deepEqual(r[1], { map: "CircularX", line: 9, mode: null, starts: [], ends: [], selectionFlow: [], peakPlayerCount: 0 });
});

test("profile GETs are not deployment receipts; do not attribute process-wide presence across clients", () => {
  const sample = ["Client connected: private-player", "[RECV] Player Archive V2!", "[PARTY] Presence InShootingRange => InHall",
    "[RECV] Player Archive V2!", "[PARTY] Presence InHall => InShowRoom", "Client disconnected", "[RECV] Player Archive V2!"].join("\n");
  const r = archiveContext(sample);
  assert.deepEqual(r.archiveReads.map(x => x.previousPresence), [null, { scene: "InHall", reportedAt: 3 }, null]);
  assert.deepEqual(r.reportedScenes, ["InHall", "InShowRoom"]);
  assert.ok(archiveContext(sample + "\nClient connected: other-player").archiveReads.every(x => x.previousPresence === null));
  assert.doesNotMatch(JSON.stringify(r), /private-player|other-player/);
});

test("normal CLI smoke with UTF-16 settings, explicit baseline and missing overrides leaves all inputs untouched", t => {
  const dir = scratch(t);
  const live = path.join(dir, "GameUserSettings.ini"), before = path.join(dir, "before.ini");
  const defaults = path.join(dir, "DefaultInput.ini");
  fs.writeFileSync(live, Buffer.from("\uFEFF" + fov("81"), "utf16le"));
  fs.writeFileSync(before, fov("80"));
  fs.writeFileSync(defaults, "[/Script/Engine.InputSettings]\n+" + action("GameMenu", "Escape"));
  const snapshot = () => fs.readdirSync(dir).sort().map(name => [name, fs.readFileSync(path.join(dir, name))]);
  const old = snapshot();
  const report = main(["--config", dir, "--before-settings", before, "--default-input", defaults]);
  assert.equal(report.keyboardOverrides, null);
  assert.equal(report.defaultInput.relevant[0].key, "Escape");
  assert.deepEqual(report.fovDelta, { before: 80, after: 81, changed: true });
  assert.deepEqual(snapshot(), old);
});

test("menu action references use only the keyboard row of the referenced original table", t => {
  const dir = scratch(t);
  const write = (name, data) => fs.writeFileSync(path.join(dir, name), JSON.stringify(data));
  const ref = RowName => ({ RowName, DataTable: { ObjectPath: "ProjectBoundary/Content/GameData/DataTables/Utility/DT_InputActions.0" } });
  const hint = ref("FieldModSetting");
  const menu = [{ Type: "CommonActionWidget", Name: "CommonActionWidget_181", Properties: { InputActions: [hint] } },
    { Type: "BottomTips_C", Name: "EscTips", Properties: { Input: "Back" } }, { Type: "Function", Name: "OnKeyDown" },
    { Type: "ComponentDelegateBinding", Properties: { ComponentDelegateBindings: [
      { ComponentPropertyName: "Button_Setting", DelegatePropertyName: "OnClicked", FunctionNameToBind: "TestClicked" }] } }];
  write("InGameOption_V2.json", menu);
  write("UMG_InGameFieldMod.json", [{ Type: "UMG_PrerequisiteButton_C", Name: "UMG_PrerequisiteButton",
    Properties: { TriggeringInputAction: ref("Deploy") } }]);
  const args = ["--config", dir, "--menu", dir], table = path.join(dir, "actions.json");
  assert.equal(main(args).menu.settingsPrompt.state, "table-not-supplied");
  assert.equal(main(args).menu.settingsClickBound, false);
  menu.push({ Type: "Function", Name: "TestClicked" });
  write("InGameOption_V2.json", menu);
  const run = () => main([...args, "--actions", table]).menu;
  write("actions.json", [{ Name: "DT_InputActions", Rows: {
    FieldModSetting: { KeyboardInputTypeInfo: { Key: { KeyName: "F9" } } },
    Deploy: { DefaultGamepadInputTypeInfo: { Key: { KeyName: "Gamepad_Special_Left" } } },
  } }]);
  assert.deepEqual(run().settingsPrompt, { row: "FieldModSetting", state: "defined", key: "F9" });
  assert.equal(run().deployAction.state, "keyboard-key-not-exported");
  assert.equal(run().settingsClickBound, true);
  assert.equal(run().backHint, "Back");
  assert.equal(run().menuHandlesKeys, true);
  write("actions.json", [{ Name: "DT_InputActions", Rows: { Deploy: { KeyboardInputTypeInfo: { Key: "None" } } } }]);
  assert.equal(run().settingsPrompt.state, "row-not-found");
  assert.equal(run().deployAction.state, "unbound");
  hint.DataTable.ObjectPath = "OtherGame/DT_InputActions.0";
  write("InGameOption_V2.json", menu);
  assert.deepEqual(run().settingsPrompt, { state: "missing-reference" });
});

const originalMenu = process.env.BOUNDARY_MATCH_MENU ||
  "C:/BrtWork/SDK/_ui_stage/ui_export_2026-10-19/raw/ProjectBoundary/Content/UI/Menu_V2/Widgets/InGameOption";
test("original exported match menu: click settings, choose character, deploy; physical key stays unknown without a table",
  { skip: !process.env.BOUNDARY_MATCH_MENU && !fs.existsSync(originalMenu) ? "original menu export not installed" : false }, t => {
    const { menu } = main(["--config", scratch(t), "--menu", originalMenu]);
    assert.deepEqual(menu, {
      settingsPrompt: { row: "FieldModSetting", state: "table-not-supplied" }, settingsClickBound: true,
      menuHandlesKeys: true, backHint: "Back", deployAction: { row: "Deploy", state: "table-not-supplied" },
      roleClickBound: true, deployClickBound: true, preorderCallback: true,
    });
  });
