// Per-tile icon colors for the Sensor family (Sensor, Number, Select,
// Date/Time), Binary sensor and Energy: a fixed icon color plus up to three
// color rules, first match wins, unavailable states keep the default color.
// Covers the shared record model (native), the sidecar persistence (native,
// with a mocked file system), the Web Admin editor module in a fake DOM, the
// draft/copy/autosave form path through the real type handlers, import,
// HTTP, the runtime color path and the translated labels.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {extractFunction, readRepoFile, repoRoot} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const code = source => source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');

// ---------------------------------------------------------------------------
// Browser editor module in a fake DOM.
const iconColorsJs = read('src/web/admin/tiles/icon-colors.js');

class FakeClassList {
  constructor(initial = []) { this.set = new Set(initial); }
  contains(name) { return this.set.has(name); }
  toggle(name, force) {
    const on = force === undefined ? !this.set.has(name) : !!force;
    if (on) this.set.add(name); else this.set.delete(name);
    return on;
  }
  add(name) { this.set.add(name); }
  remove(name) { this.set.delete(name); }
}

function createEditorDom(tab) {
  const elements = new Map();
  const listeners = {};
  const make = (id, props = {}) => {
    const el = {id, value: '', dataset: {}, classList: new FakeClassList(), parent: null, ...props};
    el.closest = selector => {
      for (let node = el; node; node = node.parent) {
        if (selector === '.tile-icon-color-fields' && node.classList.contains('tile-icon-color-fields')) return node;
        if (selector === 'button[data-icon-color]' && node.tag === 'button' && node.dataset.iconColor) return node;
      }
      return null;
    };
    el.focus = () => { focused = id; };
    if (id) elements.set(id, el);
    return el;
  };
  let focused = '';
  make(tab + '_tile_type', {value: '1'});
  const block = make(tab + '_tile_icon_color_fields', {classList: new FakeClassList(['tile-icon-color-fields', 'hidden'])});
  block.dataset.tab = tab;
  const selects = [];
  const color = make(tab + '_tile_icon_color', {value: '#FFFFFF', parent: block});
  color.dataset.unset = '1';
  color.dataset.iconColor = 'color';
  const clear = make('', {tag: 'button', parent: block});
  clear.dataset.iconColor = 'clear';
  const removes = [];
  for (let index = 0; index < 3; index++) {
    const rowId = `${tab}_tile_icon_rule_${index}`;
    const row = make(rowId, {classList: new FakeClassList(['tile-icon-rule', 'hidden']), parent: block});
    const op = make(rowId + '_op', {value: 'ge', parent: row,
      options: ['ge', 'le', 'eq', 'is', 'has'].map(value => ({value, hidden: false, disabled: false}))});
    op.dataset.iconColor = 'op';
    selects.push(op);
    make(rowId + '_value', {parent: row}).dataset.iconColor = 'value';
    make(rowId + '_color', {value: '#F44336', parent: row}).dataset.iconColor = 'rule-color';
    const remove = make('', {tag: 'button', parent: row});
    remove.dataset.iconColor = 'remove';
    remove.dataset.rule = String(index);
    removes.push(remove);
  }
  const add = make(tab + '_tile_icon_rule_add', {tag: 'button', parent: block});
  add.dataset.iconColor = 'add';
  block.querySelectorAll = selector => (selector === 'select[data-icon-color="op"]' ? selects : []);
  const calls = [];
  const document = {
    getElementById: id => elements.get(id) || null,
    addEventListener: (type, handler) => { (listeners[type] ||= []).push(handler); },
  };
  const context = vm.createContext({
    document, TextEncoder, FormData, Array, Number, Math, String,
    updateTilePreview: t => calls.push(['preview', t]),
    updateDraft: t => calls.push(['draft', t]),
    scheduleAutoSave: t => calls.push(['autosave', t]),
  });
  vm.runInContext(iconColorsJs, context);
  const fire = (type, target) => (listeners[type] || []).forEach(handler => handler({type, target}));
  return {context, elements, calls, fire, clear, removes, add, focused: () => focused,
    el: id => elements.get(id)};
}

const tab = 'tab0';
const dom = createEditorDom(tab);
const js = dom.context;
const saveRecord = () => {
  const fd = new FormData();
  js.saveIconColorFields(tab, fd);
  return fd.get('icon_colors');
};

// Load, save and reset keep the canonical record.
const record = 'FF8800\nge 4CAF50 25\nis F44336 on';
js.loadIconColorFields(tab, {icon_colors: record});
assert.equal(saveRecord(), record, 'Load and save round trip');
assert.equal(dom.el(tab + '_tile_icon_color').dataset.unset, '0');
assert.equal(dom.el(tab + '_tile_icon_rule_1').classList.contains('hidden'), false);
assert.equal(dom.el(tab + '_tile_icon_rule_2').classList.contains('hidden'), true);
assert.equal(dom.el(tab + '_tile_icon_color_fields').classList.contains('hidden'), false, 'Sensor shows the block');
js.resetIconColorFields(tab);
assert.equal(saveRecord(), '', 'Reset clears the fixed color and all rules');
assert.equal(dom.el(tab + '_tile_icon_color').dataset.unset, '1');
js.loadIconColorFields(tab, {});
assert.equal(saveRecord(), '', 'Tiles without icon colors stay empty');

// Only a fixed color, only rules, comma numbers, invalid rules.
js.loadIconColorFields(tab, {icon_colors: '\nle 2196F3 3.5'});
assert.equal(saveRecord(), '\nle 2196F3 3.5', 'Rules without a fixed color keep an empty first line');
dom.el(tab + '_tile_icon_rule_0_value').value = '4,5';
assert.equal(saveRecord(), '\nle 2196F3 4.5', 'A comma becomes a dot like in the firmware');
dom.el(tab + '_tile_icon_rule_0_value').value = 'warm';
assert.equal(saveRecord(), '', 'A numeric rule without a number is dropped like in the firmware');

// Editor events take the shared live-editor path: preview, draft, autosave.
js.resetIconColorFields(tab);
dom.calls.length = 0;
dom.el(tab + '_tile_icon_color').value = '#00BCD4';
dom.fire('input', dom.el(tab + '_tile_icon_color'));
assert.deepEqual(dom.calls, [['preview', tab], ['draft', tab], ['autosave', tab]]);
assert.equal(saveRecord(), '00BCD4');
dom.fire('click', dom.add);
assert.equal(dom.focused(), tab + '_tile_icon_rule_0_value', 'Add focuses the new value');
dom.el(tab + '_tile_icon_rule_0_value').value = '30';
dom.fire('input', dom.el(tab + '_tile_icon_rule_0_value'));
dom.fire('click', dom.add);
dom.el(tab + '_tile_icon_rule_1_op').value = 'le';
dom.el(tab + '_tile_icon_rule_1_value').value = '10';
dom.el(tab + '_tile_icon_rule_1_color').value = '#2196f3';
dom.fire('input', dom.el(tab + '_tile_icon_rule_1_color'));
dom.fire('click', dom.add);
assert.equal(dom.el(tab + '_tile_icon_rule_add').classList.contains('hidden'), true, 'At most three rules');
dom.el(tab + '_tile_icon_rule_2_value').value = '20';
assert.equal(saveRecord(), '00BCD4\nge F44336 30\nle 2196F3 10\nge F44336 20');
dom.fire('click', dom.removes[0]);
assert.equal(saveRecord(), '00BCD4\nle 2196F3 10\nge F44336 20', 'Remove keeps the order of the others');
assert.equal(dom.el(tab + '_tile_icon_rule_add').classList.contains('hidden'), false);
dom.fire('click', dom.clear);
assert.equal(saveRecord(), '\nle 2196F3 10\nge F44336 20', 'Clear keeps the rules');
assert.ok(dom.calls.filter(([kind]) => kind === 'autosave').length >= 6, 'Every change schedules autosave');

// Type visibility and operators: numeric types, text types, other types.
dom.el(tab + '_tile_type').value = '20';
dom.fire('change', dom.el(tab + '_tile_type'));
const opOptions = index => dom.el(`${tab}_tile_icon_rule_${index}_op`).options.filter(o => !o.hidden).map(o => o.value);
assert.deepEqual(opOptions(0), ['is', 'has'], 'Binary sensor offers equals/contains');
assert.equal(dom.el(tab + '_tile_icon_rule_0_op').value, 'is', 'A numeric operator falls back to equals');
for (const [type, ops] of [['14', ['ge', 'le', 'eq']], ['21', ['ge', 'le', 'eq']], ['22', ['is', 'has']],
                           ['23', ['is', 'has']], ['1', ['ge', 'le', 'eq', 'is', 'has']]]) {
  dom.el(tab + '_tile_type').value = type;
  js.syncIconColorFields(tab);
  assert.deepEqual(opOptions(1), ops, `operators for type ${type}`);
  assert.equal(dom.el(tab + '_tile_icon_color_fields').classList.contains('hidden'), false);
}
for (const type of ['0', '2', '4', '5', '8', '9', '10', '12', '15', '16', '17', '18', '19']) {
  dom.el(tab + '_tile_type').value = type;
  js.syncIconColorFields(tab);
  assert.equal(dom.el(tab + '_tile_icon_color_fields').classList.contains('hidden'), true, `type ${type} hides the block`);
}

// ---------------------------------------------------------------------------
// Draft snapshot, copy/paste and autosave use collectTypeFieldValues(), which
// calls the type's own save handler; every icon color type adds the record.
const bundle = read('src/web/assets/admin.js');
assert.ok(bundle.includes(iconColorsJs.trim().split('\n')[0].trim()), 'admin.js bundles icon-colors.js');
const handlers = {
  1: ['Sensor', 'src/types/sensor/admin-editor.js'],
  14: ['Energy', 'src/types/energy/admin.js'],
  20: ['BinarySensor', 'src/types/binary_sensor/admin-editor.js'],
  21: ['Number', 'src/types/number/admin-editor.js'],
  22: ['Select', 'src/types/select/admin-editor.js'],
  23: ['DateTime', 'src/types/datetime/admin-editor.js'],
};
const flowContext = createEditorDom(tab).context;
for (const name of ['getTileTypeMeta', 'callTypeHandler', 'collectTypeFieldValues', 'collectIconDiscFields',
  'tileTypeHasIcon', 'tileTypeHasColoredIcon', 'tileTypeHasDiscToggle', 'iconDiscModeFromCheckbox',
  'normalizeSensorValueFont']) {
  if (bundle.includes(`function ${name}(`)) vm.runInContext(extractFunction(name, bundle), flowContext);
}
const registry = {};
for (const [type, [name, file]] of Object.entries(handlers)) {
  const source = read(file);
  for (const kind of ['load', 'save', 'reset']) {
    const fn = extractFunction(`${kind}${name}Fields`, source);
    const call = {load: 'loadIconColorFields(tab, data);', save: 'saveIconColorFields(tab, formData);',
      reset: 'resetIconColorFields(tab);'}[kind];
    assert.ok(fn.includes(call), `${file}: ${kind} handler calls ${call}`);
    vm.runInContext(fn, flowContext);
  }
  registry[type] = {load: `load${name}Fields`, save: `save${name}Fields`, reset: `reset${name}Fields`};
}
flowContext.TILE_TYPE_REGISTRY = registry;
flowContext.window = flowContext;
vm.runInContext(`function maybeFillTitleFromEnergy() {}
function syncGaugeUi() {}
function resetAllTypes() { Object.values(TILE_TYPE_REGISTRY).forEach(meta => callTypeHandler(meta, 'reset', '${tab}')); }`, flowContext);
for (const type of Object.keys(handlers)) {
  flowContext.document.getElementById(tab + '_tile_type').value = type;
  flowContext.resetAllTypes();
  // Numeric types keep numeric rules; a text rule on Energy/Number falls back
  // to the first numeric operator, and a text value then drops out.
  const typeRecord = ['14', '21'].includes(type) ? '112233\nge 445566 5' : '112233\nis 445566 open';
  flowContext.callTypeHandler(registry[type], 'load', tab, {icon_colors: typeRecord});
  const snapshot = flowContext.collectTypeFieldValues(tab);
  assert.equal(snapshot.icon_colors, typeRecord, `type ${type}: draft snapshot carries the record`);
  flowContext.resetAllTypes();
  assert.equal(flowContext.collectTypeFieldValues(tab).icon_colors, '', `type ${type}: reset clears it`);
}
// Snapshots keep the record as a string when they update the cached grid.
const applySnapshot = extractFunction('applySnapshotToTileData', bundle);
assert.ok(!/numericFields = \[[^\]]*'icon_colors'/.test(applySnapshot), 'icon_colors stays a string');

// Import: exports carry the record from GET; import posts it back.
const importExport = read('src/web/admin/tiles/import-export.js');
assert.ok(importExport.includes("if (typeof tile.icon_colors === 'string') fd.append('icon_colors', tile.icon_colors);"));
assert.match(read('src/web/admin/bundle.json'), /"src\/web\/admin\/tiles\/snapshots\.js",\s*"src\/web\/admin\/tiles\/icon-colors\.js",/);

// ---------------------------------------------------------------------------
// HTTP save/load, persistence contract and runtime color path.
const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.ok(tiles.includes('out += ",\\"icon_colors\\":\\"";\n    appendJsonEscaped(out, tile.icon_colors);'), 'GET exports the record');
assert.match(tiles, /if \(server\.hasArg\("icon_colors"\)\) tile\.icon_colors = server\.arg\("icon_colors"\);\s*tile\.icon_colors = normalizeTileIconColors\(tile\.type, tile\.icon_colors\.c_str\(\)\);/,
  'POST keeps the record on partial requests, normalizes it and clears it for other types');
const config = read('src/tiles/config/tile_config.cpp');
// Overloads (legacy prefix grids) are joined; the checks look for markers.
const fn = name => {
  const found = cppFunctionDefinitions(config).filter(f => f.name === name);
  assert.ok(found.length, name);
  return found.map(f => f.source).join('\n');
};
assert.ok(fn('TileConfig::loadGrid').includes('applyIconColorsFromSd(folder_id, grid);'), 'Load applies the sidecar');
assert.ok(fn('TileConfig::saveGrid').includes('working.tiles[i].icon_colors = normalizeTileIconColors('), 'Save normalizes');
assert.ok(fn('TileConfig::saveGrid').includes('if (!writeIconColorsSd(folder_id, grid_idx, tile.icon_colors)) {'), 'Save writes the sidecar');
assert.ok(fn('TileConfig::deleteFolder').includes('writeIconColorsSd(id, i, "");'), 'Folder deletion removes sidecars');
assert.ok(fn('ensureSidecarIndexBuilt').includes('scanSidecarDir(kIconColorPathDir, g_icon_color_sidecar_keys);'));
assert.ok(config.includes('stored_colors != tile.icon_colors'), 'The S3 no-op check compares the sidecar');
assert.match(read('src/tiles/config/tile_config.h'), /String icon_colors;/);
assert.match(read('src/types/tile_type_policy.h'),
  /tileTypeHasIconColors\(int type\) \{\s*return type == TILE_SENSOR \|\| type == TILE_ENERGY \|\| type == TILE_BINARY_SENSOR \|\|\s*tileTypeIsEditableValue\(type\);/);

const rules = code(read('src/tiles/runtime/tile_icon_color_rules.h'));
assert.ok(rules.includes('tile_icon_disc::set_icon_color(icon, color);'), 'Rule colors go through the disc glow path');
assert.ok(rules.includes('if (lv_color_eq(lv_obj_get_style_text_color(icon, LV_PART_MAIN), color)) return;'), 'Unchanged colors are skipped');
assert.ok(!/\bnew\b|malloc|String\b/.test(rules), 'No allocation per state update');
const renderer = code(read('src/tiles/runtime/tile_renderer.cpp'));
assert.match(renderer, /if \(tile && tile->icon_colors\.length\(\)\) \{\s*tile_icon_color_rules::apply\(icon, tile->icon_colors\.c_str\(\),\s*displayValue != "--", value, nullptr,\s*lv_color_white\(\)\);/,
  'Sensor and Energy updates color the icon; "--" states keep the default');
assert.ok(renderer.includes('target[i].icon_label = nullptr;'), 'Widget clear releases the icon pointer');
for (const file of ['src/types/sensor/renderer.cpp', 'src/types/energy/renderer.cpp']) {
  assert.ok(read(file).includes('target[index].icon_label = icon_lbl;'), `${file} registers its icon`);
}
const control = code(read('src/types/value/value_control.cpp'));
assert.match(control, /const bool known = value\.valid && value\.has_state && value\.available && value\.state != "unknown";\s*tile_icon_color_rules::apply\(widgets\[index\]\.icon_label, tile->icon_colors\.c_str\(\), known,\s*value\.state\.c_str\(\), display\.c_str\(\), lv_color_white\(\)\);/,
  'Number, Select and Date/Time match the raw state and the displayed text');
const binary = code(read('src/types/binary_sensor/renderer.cpp'));
assert.match(binary, /tile_icon_color_rules::apply\(\s*widgets\.icon_label, tile \? tile->icon_colors\.c_str\(\) : nullptr,\s*rule_state_known\(state\), binary_sensor_state_name\(state\.value\),\s*label\.c_str\(\), lv_color_hex\(binary_sensor_visual_color\(state\)\)\);/,
  'Binary sensor matches the raw state and its translation, default is the state color');
assert.match(binary, /if \(rule_state_known\(state\)\) \{\s*tile_icon_colors::resolve\(tile\.icon_colors\.c_str\(\),\s*binary_sensor_state_name\(state\.value\),\s*initial_label\.c_str\(\), icon_color\);/,
  'The first render uses the same colors');
assert.match(binary, /state\.valid && state\.available &&\s*\(state\.value == BinarySensorValue::On \|\| state\.value == BinarySensorValue::Off\)/,
  'Unavailable and unknown binary states keep the default');
const cppFunction = (file, name) => {
  const found = cppFunctionDefinitions(read(file)).find(f => f.name === name);
  assert.ok(found, `${file}: ${name}`);
  return code(found.source);
};
for (const source of [rules, cppFunction('src/types/value/value_control.cpp', 'refresh_editable_tile'),
                      cppFunction('src/types/binary_sensor/renderer.cpp', 'apply_state'),
                      cppFunction('src/tiles/runtime/tile_renderer.cpp', 'update_sensor_tile_value')]) {
  assert.doesNotMatch(source, /lv_obj_set_style_text_color\(/, 'State updates must not bypass set_icon_color');
}

// ---------------------------------------------------------------------------
// Web Admin block and translations for every language.
const html = read('src/web/server/render/tile_icon_colors_html.cpp');
for (const marker of ['tr.tile_icon_color', 'tr.tile_icon_color_rules', 'tr.tile_icon_color_equals',
  'tr.tile_icon_color_contains', 'tr.tile_icon_color_value', 'tr.tile_icon_color_add_rule',
  'tr.tile_icon_color_remove', 'tr.tile_icon_color_hint', '_tile_icon_color_fields" class="tile-icon-color-fields hidden" data-tab="',
  '_tile_icon_color" value="#FFFFFF" data-unset="1" data-icon-color="color">', '_tile_icon_rule_add" data-icon-color="add">']) {
  assert.ok(html.includes(marker), `icon color HTML: ${marker}`);
}
assert.ok(read('src/types/types_registry.cpp').includes('if (ctx.tab_id) append_tile_icon_color_fields_html(html, *ctx.tab_id);'),
  'Full page and lazy folder fragments share the block');
const i18nHeader = read('src/core/i18n/i18n.h');
const fields = ['tile_icon_color', 'tile_icon_color_rules', 'tile_icon_color_equals', 'tile_icon_color_contains',
  'tile_icon_color_value', 'tile_icon_color_add_rule', 'tile_icon_color_remove', 'tile_icon_color_hint'];
for (const field of fields) assert.ok(i18nHeader.includes(`const char* ${field};`), `Strings.${field}`);
const i18n = read('src/core/i18n/i18n.cpp');
const translations = {
  de: ['"Icon-Farbe"', '"Farbregeln"', '"ist gleich"', '"enthält"', '"Wert"', '"Regel hinzufügen"', '"Entfernen"', '"Die erste passende Regel gilt.'],
  en: ['"Icon color"', '"Color rules"', '"equals"', '"contains"', '"Value"', '"Add rule"', '"Remove"', '"The first matching rule wins.'],
  fr: ['"Couleur de l\'icône"', '"Règles de couleur"', '"est égal à"', '"contient"', '"Valeur"', '"Ajouter une règle"', '"Supprimer"', '"La première règle correspondante'],
};
const tables = {de: i18n.slice(i18n.indexOf('kStringsDe = {'), i18n.indexOf('kStringsEn = {')),
  en: i18n.slice(i18n.indexOf('kStringsEn = {'), i18n.indexOf('kStringsFr = {')),
  fr: i18n.slice(i18n.indexOf('kStringsFr = {'), i18n.indexOf('kLocaleDe'))};
assert.match(i18nHeader, /const char\* icon_glow;\s*(?:\/\/[^\n]*\s*)*const char\* tile_icon_color;/,
  'The new members follow the glow label');
const glowLabels = {de: '"Leuchten",', en: '"Glow",', fr: '"Lueur",'};
for (const [language, texts] of Object.entries(translations)) {
  // The new strings follow the glow label of each table in struct order.
  let cursor = tables[language].indexOf(glowLabels[language]);
  assert.ok(cursor >= 0, `${language}: glow label`);
  for (const [position, text] of texts.entries()) {
    const at = tables[language].indexOf(text, cursor + 1);
    assert.ok(at > cursor, `${language}: ${text} in struct order`);
    if (position === 0) assert.match(tables[language].slice(cursor, at), /^"[^"]*",\s*$/, `${language}: directly after the glow label`);
    cursor = at;
  }
}

// ---------------------------------------------------------------------------
// Native: record model, rule evaluation and sidecar persistence.
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version']).status === 0);
if (!compiler) {
  console.log('SKIP: Icon color rules native checks need a C++ compiler');
  console.log('Icon color rules: editor, drafts, import, HTTP, runtime path and translations pass');
  process.exit(0);
}

// Evaluation matrix shared by the firmware and the Web Admin preview helper.
const matrix = [
  ['FF8800\nge F44336 30\nge FF9800 20', '25', null],
  ['FF8800\nge F44336 30\nge FF9800 20', '35', null],
  ['FF8800\nge F44336 30\nge FF9800 20', '10', null],
  ['\nge F44336 30', '10', null],
  ['\nle 2196F3 3.5', '3,5', null],
  ['\nle 2196F3 3.5', '3.49 kWh', null],
  ['\neq 4CAF50 21.5', '21.50', null],
  ['\neq 4CAF50 21.5', '21.6', null],
  ['\nge F44336 0', 'charging', null],
  ['\nis 4CAF50 CHARGING', 'charging', null],
  ['\nhas 4CAF50 arg', 'Charging', null],
  ['\nis FFC107 on', 'on', 'Open'],
  ['\nis FFC107 open', 'on', 'Open'],
  ['\nhas FFC107 geöff', 'on', 'Geöffnet'],
  ['\nis FFC107 GEÖFFNET', 'on', 'geöffnet'],
  ['00BCD4\nis FFC107 on', 'off', 'Closed'],
  ['\nis FFC107 on\nis 9E9E9E off', 'off', 'Closed'],
];
const cStr = value => value === null ? 'nullptr' : JSON.stringify(value);

const header = read('src/tiles/config/tile_config.h');
const normalizeTile = header.slice(header.indexOf('static inline String normalizeTileIconColors('),
  header.indexOf('}\n', header.indexOf('static inline String normalizeTileIconColors(')) + 2);
const cpp = String.raw`
#include "src/tiles/config/tile_icon_colors.h"
#include "src/types/tile_type_policy.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
class String:public std::string {public:using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}
 bool startsWith(const String&s)const{return rfind(s,0)==0;} const char* c_str()const{return std::string::c_str();}};
std::map<String,String> files; std::set<String> directories; bool ready=true,fail_write=false,fail_rename=false;int writes=0;
constexpr int FILE_READ=0,FILE_WRITE=1;
struct File {String path;std::vector<String> entries;size_t cursor=0;bool valid=false,dir=false;
 explicit operator bool()const{return valid;} bool isDirectory()const{return dir;}
 const char* name()const{return path.c_str()+path.find_last_of('/')+1;}
 File openNextFile(){if(cursor>=entries.size())return {};return {entries[cursor++],{},0,true,false};}
 size_t size()const{return files[path].size();} String readString(){return files[path];}
 size_t print(const String&s){++writes;files[path]=fail_write?String(s.substr(0,3)):s;return files[path].size();}
 void close(){}void flush(){}
};
struct FS {bool exists(const String&p){return files.count(p)||directories.count(p);}bool mkdir(const String&p){directories.insert(p);return true;}
 bool remove(const String&p){return files.erase(p)>0;}
 bool rename(const String&a,const String&b){if(fail_rename||!files.count(a)||files.count(b))return false;files[b]=files[a];files.erase(a);return true;}
 File open(const String&p,int mode=FILE_READ){
 if(directories.count(p)){File f;f.path=p;f.valid=true;f.dir=true;for(const auto&e:files)if(e.first.startsWith(p+"/"))f.entries.push_back(e.first);return f;}
 if(mode==FILE_WRITE)files[p]="";
 return {p,{},0,files.count(p)>0,false};
 }} filesystem;
FS& storageFS(){return filesystem;}bool storageReady(){return ready;}
constexpr size_t TILES_PER_GRID=4;
const char*kTitlePathDir="/_tile_titles",*kImagePathDir="/_tile_images",*kEntityPathDir="/_tile_entities",*kIconColorPathDir="/_tile_icon_colors";
bool g_sidecar_index_built=false;
std::vector<uint32_t> g_title_sidecar_keys,g_image_sidecar_keys,g_entity_sidecar_keys,g_icon_color_sidecar_keys;
struct Tile {TileType type=TILE_EMPTY;String icon_colors;};struct TileGridConfig{Tile tiles[TILES_PER_GRID];};
${normalizeTile}
` + ['sidecarKey', 'sidecarKeyPresent', 'sidecarKeyAdd', 'sidecarKeyRemove', 'scanSidecarDir', 'ensureSidecarIndexBuilt',
  'tmpPathFor', 'backupPathFor', 'replaceFileWithPreparedTmp', 'iconColorPathFile', 'readIconColorsSd',
  'writeIconColorsSd', 'applyIconColorsFromSd'].map(fn).join('\n') + String.raw`
std::string norm(const char* in){char out[tile_icon_colors::kMaxRecordBytes+1];tile_icon_colors::normalize(in,out,sizeof(out));return out;}
void reboot(){g_sidecar_index_built=false;g_icon_color_sidecar_keys.clear();}
int main(){
 using namespace tile_icon_colors;
 // Canonical form: uppercase colors, trimmed values, comma numbers, invalid lines dropped.
 assert(norm("#ff8800\nge 00ff00 25\nis FF0000 on")=="FF8800\nge 00FF00 25\nis FF0000 on");
 assert(norm("\r\nle 0000ff 3,5\r\n")=="\nle 0000FF 3.5");
 assert(norm("zz\nge 00ff00 abc\nxx 00ff00 5\nis 00ff0 on\nis 00ff00   \nhas 123456  open door ")=="\nhas 123456 open door");
 assert(norm("\nis 111111 a\nis 222222 b\nis 333333 c\nis 444444 d")=="\nis 111111 a\nis 222222 b\nis 333333 c");
 assert(norm("")==""&&norm("\n")==""&&norm("nothing")=="");
 assert(norm("abcdef")=="ABCDEF");
 assert(norm("\nis 111111 a\tb\x01" "c")=="\nis 111111 abc");
 std::string umlauts="\nis 111111 ";for(int i=0;i<20;++i)umlauts+="\xC3\xA4";
 const std::string clipped=norm(umlauts.c_str());assert(clipped.size()==std::string("\nis 111111 ").size()+32);
 std::string odd="\nis 111111 x";for(int i=0;i<20;++i)odd+="\xC3\xA4";
 const std::string odd_clipped=norm(odd.c_str());assert(odd_clipped.size()==std::string("\nis 111111 x").size()+30);
 std::string worst;for(int i=0;i<3;++i){worst+="\nhas FFFFFF ";worst+=std::string(64,'w');}
 assert(norm(("ffffff"+worst).c_str()).size()==kMaxRecordBytes);
 // Evaluation matrix, printed for the Web Admin preview comparison.
 struct Case{const char* record;const char* state;const char* display;};
 const Case cases[]={${matrix.map(([r, s, d]) => `{${cStr(r)},${cStr(s)},${cStr(d)}}`).join(',')}};
 for(const Case& c:cases){uint32_t rgb=0;const bool hit=resolve(c.record,c.state,c.display,rgb);
  if(hit)std::printf("#%06X\n",static_cast<unsigned>(rgb));else std::printf("-\n");}
 uint32_t rgb=0;assert(!resolve("",  "on",nullptr,rgb)&&!resolve(nullptr,"on",nullptr,rgb)&&!resolve("\nis FFC107 on",nullptr,nullptr,rgb));
 // Persistence: every icon color type, reboot, no-op, removal, recovery.
 const std::string record="FF8800\nge F44336 30\nis 4CAF50 on";
 for(TileType type:{TILE_SENSOR,TILE_ENERGY,TILE_BINARY_SENSOR,TILE_NUMBER,TILE_SELECT,TILE_DATETIME}){
  assert(writeIconColorsSd(3,1,record));TileGridConfig grid;grid.tiles[1].type=type;
  reboot();applyIconColorsFromSd(3,grid);assert(grid.tiles[1].icon_colors==record);
  const int before=writes;assert(writeIconColorsSd(3,1,record));assert(writes==before);
  assert(writeIconColorsSd(3,1,""));reboot();String gone;assert(!readIconColorsSd(3,1,gone));
 }
 // Other types never take a record, even from a stale file.
 assert(writeIconColorsSd(3,2,record));
 for(TileType type:{TILE_EMPTY,TILE_SWITCH,TILE_SCENE,TILE_CLIMATE,TILE_COVER,TILE_BACK,TILE_WEATHER}){
  TileGridConfig grid;grid.tiles[2].type=type;reboot();applyIconColorsFromSd(3,grid);assert(grid.tiles[2].icon_colors.empty());
  assert(normalizeTileIconColors(type,record.c_str()).empty());
 }
 assert(normalizeTileIconColors(TILE_SENSOR,"#ff8800\nge 00ff00 1,5")=="FF8800\nge 00FF00 1.5");
 // A damaged file is normalized on load; a moved tile keeps its own index.
 files[iconColorPathFile(3,2)]="#00ff00\nbogus\nhas 112233 x";TileGridConfig damaged;damaged.tiles[2].type=TILE_SENSOR;
 reboot();applyIconColorsFromSd(3,damaged);assert(damaged.tiles[2].icon_colors=="00FF00\nhas 112233 x");
 // Recovery from .tmp and .bak like the long-title sidecar.
 const String path=iconColorPathFile(5,0);assert(writeIconColorsSd(5,0,record));
 files[tmpPathFor(path)]=record;files.erase(path);reboot();String restored;assert(readIconColorsSd(5,0,restored)&&restored==record);
 files[backupPathFor(path)]=record;files.erase(tmpPathFor(path));reboot();assert(readIconColorsSd(5,0,restored)&&restored==record);
 assert(writeIconColorsSd(5,0,""));reboot();assert(!readIconColorsSd(5,0,restored));
 // Failed writes keep the previous record; oversized records are refused.
 assert(writeIconColorsSd(6,3,record));fail_write=true;assert(!writeIconColorsSd(6,3,"00FF00"));fail_write=false;
 assert(readIconColorsSd(6,3,restored)&&restored==record);
 fail_rename=true;assert(!writeIconColorsSd(6,3,"00FF00"));fail_rename=false;assert(readIconColorsSd(6,3,restored)&&restored==record);
 assert(!writeIconColorsSd(6,3,String(kMaxRecordBytes+1,'x')));ready=false;assert(!writeIconColorsSd(6,3,"00FF00"));
 return 0;
}
`;
const outDir = path.join(repoRoot, 'build/tests/tile-icon-color-rules');
fs.mkdirSync(outDir, {recursive: true});
const cppPath = path.join(outDir, 'test.cpp');
const executable = path.join(outDir, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(cppPath, cpp);
let result = spawnSync(compiler, ['-std=c++17', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-I', repoRoot, cppPath, '-o', executable], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(executable, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const nativeColors = result.stdout.trim().split(/\r?\n/);
const expected = ['#FF9800', '#F44336', '#FF8800', '-', '#2196F3', '#2196F3', '#4CAF50', '-', '-', '#4CAF50',
  '#4CAF50', '#FFC107', '#FFC107', '#FFC107', '#FFC107', '#00BCD4', '#9E9E9E'];
assert.deepEqual(nativeColors, expected, 'Firmware rule order, numeric and text matching');
// The Web Admin preview helper gives the same colors.
matrix.forEach(([record, state, display], index) => {
  const color = js.resolveIconColorRecord(record, state, display) || '-';
  assert.equal(color, nativeColors[index], `preview color for ${JSON.stringify([record, state, display])}`);
});
console.log('Icon color rules: record model, rule order, numeric/text/binary matching, sidecar persistence, editor, drafts, import, HTTP, runtime path and translations pass');
