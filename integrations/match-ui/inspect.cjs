"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { parseArgs } = require("node:util");

// 只报告指定输入段的菜单、角色、标记及相关按键，不输出控制台历史或帐号。
const inputSections = new Map([
  ["/script/projectboundary.pbkeyboardinputsettings", "keyboard"],
  ["/script/engine.inputsettings", "engine"],
]);
const settingsSection = "/script/projectboundary.projectboundaryusersettings";
const arrayOps = { "": "entry", "+": "add", ".": "append", "-": "remove", "!": "clear" };

function text(file) {
  const bytes = fs.readFileSync(file);
  return bytes.toString(bytes[0] === 0xff && bytes[1] === 0xfe ? "utf16le" : "utf8").replace(/^\uFEFF/, "");
}

function* iniEntries(input) {
  let section = "";
  for (const [n, line] of input.split(/\r*\n/).entries()) {
    const header = /^\s*\[([^\]]+)\]\s*(?:[;#].*)?$/.exec(line);
    if (header) { section = header[1].trim().toLowerCase(); continue; }
    const kv = /^\s*([+.!-]?)(\w+)\s*=\s*(.*?)\s*$/.exec(line);
    if (kv) yield { line: n + 1, section, op: arrayOps[kv[1]], key: kv[2].toLowerCase(), value: kv[3] };
  }
}

function bindingReport(input) {
  const result = { actionEntries: { keyboard: 0, engine: 0 }, axisEntries: { keyboard: 0, engine: 0 },
    relevant: [], arrayClears: [], malformed: [] };
  for (const row of iniEntries(input.replace(/^\uFEFF/, ""))) {
    const scope = inputSections.get(row.section);
    if (!scope || !["actionmappings", "axismappings"].includes(row.key)) continue;
    if (row.op === "clear") {
      result.arrayClears.push({ line: row.line, scope, array: row.key });
      continue;
    }
    const axis = row.key === "axismappings";
    (axis ? result.axisEntries : result.actionEntries)[scope]++;
    const kv = new Map([...row.value.matchAll(/(?:[(,])\s*([A-Za-z]\w*)\s*=\s*(?:"([^"]*)"|([^,)\s]+))/g)]
      .map(([, key, quoted, bare]) => [key.toLowerCase(), quoted ?? bare]));
    const action = kv.get(axis ? "axisname" : "actionname"), key = kv.get("key");
    if (!action || !key || ![action, key].every(s => /^[A-Za-z_][A-Za-z0-9_.-]*$/.test(s))) {
      result.malformed.push({ line: row.line, scope, array: row.key });
      continue;
    }
    if (/^gamepad_/i.test(key) || !(/^(F2|Escape)$/i.test(key) || /menu|setting|selectrole|role.?select|deploy|pause|^(?:keyboard_)?mark$/i.test(action))) continue;
    const modifiers = ["Shift", "Ctrl", "Alt", "Cmd"].filter(m => /^true$/i.test(kv.get("b" + m.toLowerCase()) || ""));
    result.relevant.push({ line: row.line, scope, kind: axis ? "axis" : "action", op: row.op, action, key, modifiers });
  }
  return result;
}

function savedSettings(input) {
  const result = { FOV: null, MarkMethod: null };
  for (const row of iniEntries(input)) {
    const name = row.section === settingsSection && { fov: "FOV", markmethod: "MarkMethod" }[row.key];
    if (typeof name !== "string" || row.op !== "entry") continue;
    const n = /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(row.value) ? Number(row.value) : NaN;
    result[name] = { line: row.line, value: Number.isFinite(n) ? n : null };
  }
  return result;
}

function settingDelta(before, after) {
  const a = before.FOV?.value, b = after.FOV?.value;
  return { before: a ?? null, after: b ?? null, changed: a == null || b == null ? null : a !== b };
}

function archiveContext(input) {
  const lines = input.split(/\r*\n/);
  const connection = /^(?:===\s*)?Client (connected|disconnected)\b/i;
  const connections = lines.filter(line => connection.exec(line)?.[1].toLowerCase() === "connected").length;
  const gets = [], scenes = new Set();
  let lastPresence = null;
  for (const [n, line] of lines.entries()) {
    if (connection.test(line)) lastPresence = null;
    const next = /^\[PARTY\] Presence (In\w+) => (In\w+)/.exec(line);
    if (next) {
      scenes.add(next[2]);
      lastPresence = { scene: next[2], reportedAt: n + 1 };
    }
    if (/^\[RECV\] Player Archive V2!/.test(line)) {
      gets.push({ line: n + 1, previousPresence: connections === 1 ? lastPresence : null });
    }
  }
  return { connections, reportedScenes: [...scenes], archiveReads: gets };
}

function matchSessions(input) {
  const sessions = [];
  let match;
  for (const [n, line] of input.split(/\r*\n/).entries()) {
    const field = /^\[SERVER\] Map loaded:\s*([A-Za-z0-9_-]+)\s*$/.exec(line);
    if (field) {
      match = { map: field[1], line: n + 1, mode: null, starts: [], ends: [], selectionFlow: [], peakPlayerCount: null };
      sessions.push(match);
    }
    if (!match) continue;
    const mode = /^\[SERVER\] Mode:\s*.*\bBP_PBGameMode_([a-z0-9_]+)_C\s*$/i.exec(line);
    if (mode) match.mode = mode[1];
    if (/^\[SERVER_LIFECYCLE\] match_started\b/.test(line)) match.starts.push(n + 1);
    if (/^\[SERVER_LIFECYCLE\] match_end_detected\b/.test(line)) match.ends.push(n + 1);
    if (/^All players connected, beginning role selection flow!$/.test(line)) match.selectionFlow.push({ line: n + 1, event: "begin" });
    if (/^Selecting role\.\.\.$/.test(line)) match.selectionFlow.push({ line: n + 1, event: "selecting" });
    const count = /^\[HEARTBEAT\] PlayerCount\s*=\s*(\d+)\b/.exec(line);
    if (count) match.peakPlayerCount = Math.max(match.peakPlayerCount ?? 0, Number(count[1]));
  }
  return sessions;
}

function menuEvidence(menuDir, tablePath) {
  const read = name => JSON.parse(text(path.join(menuDir, name)));
  const menu = read("InGameOption_V2.json"), field = read("UMG_InGameFieldMod.json");
  const table = tablePath && JSON.parse(text(tablePath)).find(x => x.Name === "DT_InputActions" && x.Rows);
  const tableName = "ProjectBoundary/Content/GameData/DataTables/Utility/DT_InputActions.0";
  function binding(ref) {
    if (!ref || ref.DataTable?.ObjectPath !== tableName) return { state: "missing-reference" };
    const row = ref.RowName, record = table?.Rows?.[row];
    if (!tablePath) return { row, state: "table-not-supplied" };
    if (!record) return { row, state: "row-not-found" };
    const key = record.KeyboardInputTypeInfo?.Key;
    const value = typeof key === "string" ? key : key?.KeyName;
    if (typeof value !== "string" || !value) return { row, state: "keyboard-key-not-exported" };
    return { row, state: value === "None" ? "unbound" : "defined", key: value };
  }
  const widget = (exports, type, name) => exports.find(x => x.Type === type && x.Name === name)?.Properties;
  const callbacks = exports => exports.filter(x => x.Type === "ComponentDelegateBinding")
    .flatMap(x => x.Properties?.ComponentDelegateBindings || [])
    .filter(x => exports.some(fn => fn.Type === "Function" && fn.Name === x.FunctionNameToBind));
  const bound = (list, component, event) => list.some(x => x.ComponentPropertyName === component && x.DelegatePropertyName === event);
  const menuBindings = callbacks(menu), fieldBindings = callbacks(field);
  const prompt = widget(menu, "CommonActionWidget", "CommonActionWidget_181")?.InputActions?.find(x => x.RowName === "FieldModSetting");
  return {
    settingsPrompt: binding(prompt),
    settingsClickBound: bound(menuBindings, "Button_Setting", "OnClicked"),
    menuHandlesKeys: menu.some(x => x.Type === "Function" && x.Name === "OnKeyDown"),
    backHint: widget(menu, "BottomTips_C", "EscTips")?.Input ?? null,
    deployAction: binding(widget(field, "UMG_PrerequisiteButton_C", "UMG_PrerequisiteButton")?.TriggeringInputAction),
    roleClickBound: bound(fieldBindings, "CharacterList", "TryToModifyCharacter"),
    deployClickBound: bound(fieldBindings, "UMG_DeployCharacter", "Click") && bound(menuBindings, "UMG_InGameFieldMod", "ClickDeploy"),
    preorderCallback: field.some(x => x.Type === "Function" && x.Name === "K2_OnPreOrderComplete"),
  };
}

function main(args) {
  const { values } = parseArgs({ args, options: Object.fromEntries(
    ["config", "default-input", "before-settings", "archive-log", "server-log", "menu", "actions"].map(key => [key, { type: "string" }])) });
  if (values.actions && !values.menu) throw new Error("--actions requires --menu <original InGameOption JSON directory>");
  if (!values.config && !process.env.LOCALAPPDATA) throw new Error("Pass --config <WindowsClient directory>");
  const config = values.config || path.join(process.env.LOCALAPPDATA, "ProjectBoundary/Saved/Config/WindowsClient");
  const optional = file => {
    try { return text(file); } catch (error) { if (error.code === "ENOENT") return null; throw error; }
  };
  const input = optional(path.join(config, "Input.ini")), settings = optional(path.join(config, "GameUserSettings.ini"));
  const report = { keyboardOverrides: input === null ? null : bindingReport(input),
    savedSettings: settings === null ? null : savedSettings(settings) };
  if (values["default-input"]) report.defaultInput = bindingReport(text(values["default-input"]));
  if (values["before-settings"]) report.fovDelta = settingDelta(savedSettings(text(values["before-settings"])), report.savedSettings || {});
  if (values["archive-log"]) report.profile = archiveContext(text(values["archive-log"]));
  if (values["server-log"]) report.matches = matchSessions(text(values["server-log"]));
  if (values.menu) report.menu = menuEvidence(values.menu, values.actions);
  return report;
}

if (require.main === module) {
  try { console.log(JSON.stringify(main(process.argv.slice(2)), null, 2)); }
  catch (error) { console.error(error.message); process.exitCode = 1; }
}
module.exports = { archiveContext, bindingReport, main, matchSessions, savedSettings, settingDelta };
