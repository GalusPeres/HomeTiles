// Per-tile icon colors for the Sensor family (Sensor, Number, Select,
// Date/Time), Binary sensor and Energy, record format v2: a fixed icon color,
// a color bar for numeric states (smooth or steps), up to six state colors
// for text states and the Binary On/Off colors; unavailable states keep the
// default color. Covers the shared record model and color formulas (native
// firmware header against the Web Admin module, identical outputs), the b39
// migration, the sidecar persistence (native, mocked file system), the Web
// Admin editor in headless Chrome (bar presets, drag, add, remove, state rows,
// Binary On/Off, drafts through the real type handlers), import, HTTP, the
// runtime color path including hidden folder builds and the translations.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readAdminDeliverySource, readRepoFile, repoRoot, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const code = source => source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');

// ---------------------------------------------------------------------------
// Web Admin record model, run in a bare VM (no DOM needed for these helpers).
const iconColorsJs = read('src/web/admin/tiles/icon-colors.js');
const js = vm.createContext({document: {addEventListener() {}, getElementById: () => null},
  TextEncoder, TextDecoder, Number, Math, String, Array});
vm.runInContext(iconColorsJs, js);

// Normalization cases: [input, allowBar, allowRows, expected].
const umlauts = 'ä'.repeat(20);
const normalizeCases = [
  ['v2\nff8800\nbar smooth 0 100 0:3b82f6 450:22c55e 700:f59e0b 1000:ef4444', true, true,
    'v2\nFF8800\nbar smooth 0 100 0:3B82F6 450:22C55E 700:F59E0B 1000:EF4444'],
  ['v2\n\nbar steps -10,5 40 1000:EF4444 0:3B82F6 5000:111111 x:222222 300:22C55E', true, true,
    'v2\n\nbar steps -10.5 40 0:3B82F6 300:22C55E 1000:EF4444 1000:111111'],
  ['v2\nABCDEF\nbar smooth 5 5 0:000000 1000:FFFFFF', true, true, 'v2\nABCDEF'],
  ['v2\n\nbar smooth 0 1 0:000000', true, true, ''],
  ['v2\n\nbar smooth 0 1e3 0:000000 1000:FFFFFF', true, true, ''],
  ['v2\n\nbar smooth 0 6 0:000000 100:111111 200:222222 300:333333 400:444444 500:555555 600:666666', true, true,
    'v2\n\nbar smooth 0 6 0:000000 100:111111 200:222222 300:333333 400:444444 500:555555'],
  ['v2\n\nis 00ff00 on\nhas FF0000  6 \nxx 00ff00 a\nge 00ff00 5\nis 00ff0 b\nis 123456   ', true, true,
    'v2\n\nis 00FF00 on\nhas FF0000 6'],
  ['v2\n\n' + [1, 2, 3, 4, 5, 6, 7].map(n => `is 11111${n} s${n}`).join('\n'), false, true,
    'v2\n\n' + [1, 2, 3, 4, 5, 6].map(n => `is 11111${n} s${n}`).join('\n')],
  ['v2\n123456\nbar steps 0 10 0:000000 1000:FFFFFF\nis 00FF00 on', true, false, 'v2\n123456\nbar steps 0 10 0:000000 1000:FFFFFF'],
  ['v2\n123456\nbar steps 0 10 0:000000 1000:FFFFFF\nis 00FF00 on', false, true, 'v2\n123456\nis 00FF00 on'],
  ['v2\n123456\nbar steps 0 10 0:000000 1000:FFFFFF\nis 00FF00 on', false, false, 'v2\n123456'],
  ['v2\n\nis 111111 ' + umlauts, false, true, 'v2\n\nis 111111 ' + 'ä'.repeat(16)],
  ['v2\n\nis 111111 x' + umlauts, false, true, 'v2\n\nis 111111 x' + 'ä'.repeat(15)],
  ['v2\r\nff0000\r\nis 00ff00 on\r\n', true, true, 'v2\nFF0000\nis 00FF00 on'],
  // b39 records: plain descending ">=" whole-number thresholds become steps.
  ['FF8800\nge F44336 30\nge FF9800 20', true, true, 'v2\nFF8800\nbar steps 10 30 0:FF8800 500:FF9800 1000:F44336'],
  ['\nge 4CAF50 25', true, true, 'v2\n\nbar steps 24 25 0:FFFFFF 1000:4CAF50'],
  ['\nge 111111 30\nge 222222 20\nge 333333 10', true, true, 'v2\n\nbar steps -10 30 0:FFFFFF 500:333333 750:222222 1000:111111'],
  ['\nge 111111 25\nge 222222 20\nge 333333 10', true, true, ''],
  ['\nge 111111 20\nge 222222 30', true, true, ''],
  ['\nge 111111 2.5', true, true, ''],
  ['00BCD4\nle 2196F3 3.5\nis FFC107 on', true, true, 'v2\n00BCD4\nis FFC107 on'],
  ['\nis FFC107 on\nhas 9E9E9E geöff', false, true, 'v2\n\nis FFC107 on\nhas 9E9E9E geöff'],
  ['\nis 111111 a\nge 222222 5\nis 333333 c\nis 444444 d', false, true, 'v2\n\nis 111111 a\nis 333333 c'],
  ['FF8800\nge F44336 30\nge FF9800 20', false, true, 'v2\nFF8800'],
  ['abcdef', true, true, 'v2\nABCDEF'],
  ['', true, true, ''], ['v2', true, true, ''], ['v2\n', true, true, ''], ['nothing', true, true, ''],
];
for (const [input, bar, rows, expected] of normalizeCases) {
  assert.equal(js.normalizeIconColorRecord(input, bar, rows), expected, `JS normalize ${JSON.stringify(input)}`);
}
// Rule layer: [input, allowBar, allowRows, allowSelf, expected]; the firmware
// gives the same records (checked natively below).
const layerCases = [
  ['v2\n\nsrc rules self\nhas F44336 6', false, true, true, 'v2\n\nhas F44336 6'],
  ['v2\n\nsrc rules self off\nhas F44336 6', false, true, true, 'v2\n\nsrc rules self off\nhas F44336 6'],
  ['v2\n\nsrc auto self noicon tile=5\nhas F44336 6', false, true, true, 'v2\n\nsrc auto self tile=10 noicon'],
  ['v2\n\nsrc rules self tile=30 off noicon\nis 4CAF50 on', false, false, true, 'v2\n\nsrc rules self tile=30 noicon off\nis 4CAF50 on'],
  ['v2\nFF0000\nsrc rules self', false, false, false, 'v2\nFF0000'],
  ['v2\n\nsrc rules sensor.waste tile=99\nhas f44336 6', false, false, false, 'v2\n\nsrc rules sensor.waste tile=50\nhas F44336 6'],
  ['v2\n\nsrc rules sensor.waste bogus', false, false, false, ''],
];
for (const [input, bar, rows, self, expected] of layerCases) {
  assert.equal(js.normalizeIconColorRecord(input, bar, rows, true, self), expected, `JS layer ${JSON.stringify(input)}`);
}
const tintCases = [['#2A2A2A', '#FFC107', 25], ['#2A2A2A', '#FFFFFF', 50], ['#4A148C', '#F44336', 30], ['#FFFFFF', '#FFE082', 10]];
const previewTints = tintCases.map(([base, color, percent]) => js.tileTintBackground(base, color, percent));

// Evaluation matrix: [record, state, display]. The firmware must give the
// same colors (checked natively below).
const coldWarm = 'v2\n\nbar smooth 0 100 0:3B82F6 450:22C55E 700:F59E0B 1000:EF4444';
const steps = 'v2\n\nbar steps 10 30 0:FFFFFF 500:FF9800 1000:F44336';
const resolveCases = [
  [coldWarm, '-5', null], [coldWarm, '0', null], [coldWarm, '22.5', null], [coldWarm, '45', null],
  [coldWarm, '57.5', null], [coldWarm, '21,5 °C', null], [coldWarm, '85', null], [coldWarm, '100', null],
  [coldWarm, '150', null], [coldWarm, '33.333', null], [coldWarm, 'unknown', null],
  [steps, '5', null], [steps, '19.99', null], [steps, '20', null], [steps, '29.9', null], [steps, '30', null],
  ['v2\n\nbar smooth -20 40 0:0000FF 1000:FF0000', '-3.7', null],
  ['v2\n\nbar smooth 0 1 0:000000 1000:FFFFFF', '0.5', null],
  // Maintainer case: text Sensor, contains "6".
  ['v2\n\nhas F44336 6', 'in 6 Tagen rausstellen', null],
  ['v2\n\nhas F44336 6', 'in 5 Tagen rausstellen', null],
  ['v2\n\nis 4CAF50 CHARGING', 'charging', null],
  ['v2\n\nhas 4CAF50 arg', 'Charging', null],
  ['v2\n\nis 4CAF50 on\nis 607D8B off', 'on', 'Open'],
  ['v2\n\nis 4CAF50 on\nis 607D8B off', 'off', 'Closed'],
  ['v2\n\nis FFC107 open', 'on', 'Open'],
  ['v2\n\nhas FFC107 geöff', 'on', 'Geöffnet'],
  ['v2\n\nis FFC107 GEÖFFNET', 'on', 'geöffnet'],
  ['v2\n00BCD4\nis FFC107 on', 'off', 'Closed'],
  ['v2\n\nbar smooth 0 100 0:000000 1000:FFFFFF\nis FF0000 open', '50', null],
  ['v2\n\nbar smooth 0 100 0:000000 1000:FFFFFF\nis FF0000 open', 'open', null],
  ['v2\nABCDEF', 'anything', null],
  ['FF8800\nge F44336 30', '35', null],
];
const previewColors = resolveCases.map(([record, state, display]) => js.resolveIconColorRecord(record, state, display) || '-');
assert.deepEqual(previewColors.slice(0, 11),
  ['#3B82F6', '#3B82F6', '#2FA4AA', '#22C55E', '#8CB235', '#2FA2AD', '#F27128', '#EF4444', '#EF4444', '#28B485', '-'],
  'Smooth bar: clamped ends, exact stops and rounded 8-bit mixes');
assert.deepEqual(previewColors.slice(11, 16), ['#FFFFFF', '#FFFFFF', '#FF9800', '#FF9800', '#F44336'],
  'Steps bar: the last stop at or below the value');
assert.equal(previewColors[18], '#F44336', 'A text Sensor state matches "contains 6"');
assert.equal(previewColors[19], '-');
assert.deepEqual(previewColors.slice(22, 28), ['#4CAF50', '#607D8B', '#FFC107', '#FFC107', '#FFC107', '#00BCD4'],
  'Binary matches the raw state and the translated label; the fixed color is the fallback');
assert.deepEqual(previewColors.slice(28), ['#808080', '#FF0000', '#ABCDEF', '-'],
  'Numeric states use the bar, text states the state lines; b39 records are never evaluated unnormalized');

// ---------------------------------------------------------------------------
// Source contracts: HTTP, persistence, handlers, runtime path, translations.
const bundle = read('src/web/assets/admin.js');
assert.ok(bundle.includes(iconColorsJs.trim().split('\n')[0].trim()), 'admin.js bundles icon-colors.js');
assert.match(read('src/web/admin/bundle.json'), /"src\/web\/admin\/tiles\/snapshots\.js",\s*"src\/web\/admin\/tiles\/icon-colors\.js",/);
const handlers = {
  1: ['Sensor', 'src/types/sensor/admin-editor.js'],
  14: ['Energy', 'src/types/energy/admin.js'],
  20: ['BinarySensor', 'src/types/binary_sensor/admin-editor.js'],
  21: ['Number', 'src/types/number/admin-editor.js'],
  22: ['Select', 'src/types/select/admin-editor.js'],
  23: ['DateTime', 'src/types/datetime/admin-editor.js'],
  2: ['Scene', 'src/types/scene/admin.js'],
  4: ['Navigate', 'src/types/navigate/admin.js'],
  5: ['Switch', 'src/types/switch/admin.js'],
};
for (const [name, file] of Object.values(handlers)) {
  const source = read(file);
  for (const [kind, call] of [['load', 'loadIconColorFields(tab, data);'], ['save', 'saveIconColorFields(tab, formData);'],
                              ['reset', 'resetIconColorFields(tab);']]) {
    const start = source.indexOf(`function ${kind}${name}Fields(`);
    assert.ok(start >= 0 && source.slice(start, source.indexOf('\n  }\n', start)).includes(call), `${file}: ${kind} calls ${call}`);
  }
}
assert.ok(read('src/types/sensor/admin-editor.js').includes('syncGaugeUi(tab);\n    // The entity is known now'),
  'Sensor re-syncs the bar/state choice once its entity is loaded');
const importExport = read('src/web/admin/tiles/import-export.js');
assert.ok(importExport.includes("if (typeof tile.icon_colors === 'string') fd.append('icon_colors', tile.icon_colors);"));
assert.ok(!/numericFields = \[[^\]]*'icon_colors'/.test(bundle), 'icon_colors stays a string in snapshots');

const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.ok(tiles.includes('out += ",\\"icon_colors\\":\\"";\n    appendJsonEscaped(out, tile.icon_colors);'), 'GET exports the record');
assert.match(tiles, /if \(server\.hasArg\("icon_colors"\)\) tile\.icon_colors = server\.arg\("icon_colors"\);\s*tile\.icon_colors = normalizeTileIconColors\(tile\.type, tile\.icon_colors\.c_str\(\)\);/,
  'POST keeps the record on partial requests, normalizes it and clears it for other types');
const config = read('src/tiles/config/tile_config.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(config).filter(f => f.name === name);
  assert.ok(found.length, name);
  return found.map(f => f.source).join('\n');
};
assert.ok(fn('TileConfig::loadGrid').includes('applyIconColorsFromSd(folder_id, grid);'), 'Load applies the sidecar');
assert.ok(fn('TileConfig::saveGridInPlace').includes('working.tiles[i].icon_colors = normalizeTileIconColors('), 'Save normalizes');
assert.ok(fn('TileConfig::saveGridInPlace').includes('if (!writeIconColorsSd(folder_id, grid_idx, tile.icon_colors)) {'), 'Save writes the sidecar');
assert.ok(fn('TileConfig::deleteFolder').includes('writeIconColorsSd(id, i, "");'), 'Folder deletion removes sidecars');
assert.ok(fn('ensureSidecarIndexBuilt').includes('scanSidecarDir(kIconColorPathDir, g_icon_color_sidecar_keys);'));
assert.ok(config.includes('stored_colors != tile.icon_colors'), 'The S3 no-op check compares the sidecar');
const policy = read('src/types/tile_type_policy.h');
assert.match(policy, /tileTypeIconColorsByValue\(int type\) \{\s*return type == TILE_SENSOR \|\| type == TILE_ENERGY \|\| type == TILE_NUMBER;/);
assert.match(policy, /tileTypeIconColorsByState\(int type\) \{\s*return type == TILE_SENSOR \|\| type == TILE_BINARY_SENSOR \|\| type == TILE_SELECT \|\|\s*type == TILE_DATETIME;/);
assert.match(read('src/tiles/config/tile_config.h'),
  /tile_icon_colors::normalize\(\s*record, out, sizeof\(out\), tileTypeIconColorsByValue\(type\), tileTypeIconColorsByState\(type\),\s*true, tileTypeRulesUseOwnEntity\(type\)\);/);

const rules = code(read('src/tiles/runtime/tile_icon_color_rules.h'));
assert.ok(rules.includes('tile_icon_disc::set_icon_color(icon, color);'), 'Colors go through the disc glow path');
assert.match(rules, /if \(!tile_icon_disc::forced_color\(icon, forced\) &&\s*lv_color_eq\(lv_obj_get_style_text_color\(icon, LV_PART_MAIN\), color\)\) \{\s*return;/,
  'Unchanged colors are skipped unless a rule forces the icon');
const header = read('src/tiles/config/tile_icon_colors.h');
assert.ok(!/\bnew\b|malloc|String\b/.test(code(rules) + code(header)), 'No allocation per state update');
const renderer = code(read('src/tiles/runtime/tile_renderer.cpp'));
assert.match(renderer, /if \(tile && tile->icon_colors\.length\(\)\) \{\s*tile_icon_color_rules::apply\(icon, tile->icon_colors\.c_str\(\),\s*displayValue != "--", value, nullptr,\s*lv_color_white\(\)\);/,
  'Sensor and Energy updates color the icon from the raw state; "--" states keep the default');
assert.match(renderer, /if \(g_build_grid\) return &g_build_grid->tiles\[index\];\s*return &tileConfig\.getActiveGrid\(\)\.tiles\[index\];/,
  'State updates read the grid that is being built');
const unified = code(read('src/ui/tabs/tiles/tab_tiles_unified.cpp'));
const build = unified.slice(unified.indexOf('static void build_folder_cache_entry('), unified.indexOf('tile_renderer_snapshot_tab0(&entry.widgets);'));
assert.match(build, /tile_renderer_set_build_grid\(&config\);\s*render_tile_grid\(entry\.grid, config,[\s\S]*apply_cached_states\(grid_type, config, false\);[\s\S]*process_binary_sensor_update_queue\(\);[\s\S]*tile_renderer_set_build_grid\(nullptr\);/,
  'Hidden folder builds (preloads) resolve icon colors from their own tiles, not the visible folder');
const control = code(read('src/types/value/value_control.cpp'));
assert.match(control, /const bool known = value\.valid && value\.has_state && value\.available && value\.state != "unknown";\s*tiles_request_rule_refresh\(grid, index\);\s*tile_icon_color_rules::apply\(widgets\[index\]\.icon_label, tile->icon_colors\.c_str\(\), known,\s*value\.state\.c_str\(\), display\.c_str\(\), lv_color_white\(\)\);/,
  'Number, Select and Date/Time match the raw state and the displayed text');
const binary = code(read('src/types/binary_sensor/renderer.cpp'));
assert.match(binary, /tile_icon_color_rules::apply\(\s*widgets\.icon_label, tile \? tile->icon_colors\.c_str\(\) : nullptr,\s*rule_state_known\(state\), binary_sensor_state_name\(state\.value\),\s*label\.c_str\(\), lv_color_hex\(binary_sensor_visual_color\(state\)\)\);/,
  'Binary sensor matches the raw state and its translation, default is the state color');
assert.match(binary, /const uint32_t icon_color = tile_icon_colors::state_icon_color\(\s*tile\.icon_colors\.c_str\(\), rule_state_known\(state\), binary_sensor_state_name\(state\.value\),\s*initial_label\.c_str\(\), binary_sensor_visual_color\(state\)\);/,
  'The first Binary render uses the same rule as every state update ("Color icon" off keeps the fixed color)');
assert.match(binary, /\? 0xFFC107\s*: 0x9E9E9E;/, 'Binary defaults stay amber and grey');
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

// Web Admin block markup (mirrored by the DOM harness below) and translations.
const html = read('src/web/server/render/tile_icon_colors_html.cpp');
for (const marker of ['_tile_icon_color_fields" class="tile-icon-color-fields hidden" data-tab="',
  '_tile_icon_color" value="#FFFFFF" data-unset="1" data-icon-color="color">', '"_tile_icon_bar_section\\">\\n"',
  '_tile_icon_bar" value="">', '"mode", "data-mode", "off"', '"mode", "data-mode", "smooth"', '"mode", "data-mode", "steps"',
  '"preset", "data-preset", "cold_warm"', '"preset", "data-preset", "traffic"', '"preset", "data-preset", "battery"',
  '"preset", "data-preset", "humidity"', '"preset", "data-preset", "single"', '_tile_icon_bar_strip" data-icon-color="bar">',
  '_tile_icon_bar_handles" data-selected="-1"><button type="button" class="icon-color-stop-remove hidden" data-icon-color="stop-remove"',
  '_tile_icon_stop_color" data-icon-color="stop-color"', '_tile_icon_bar_min" maxlength=', '" data-icon-color="min">',
  '"_tile_icon_state_section\\">\\n"', '_value" autocomplete="off" maxlength=")html";',
  '_has" data-icon-color="has">', '_color" value="#22C55E" data-icon-color="rule-color">', '_tile_icon_rule_add" data-icon-color="add">',
  '"_tile_icon_binary_section\\">\\n"', '{"on", tr.tile_icon_color_state_on, "#FFC107"}', '{"off", tr.tile_icon_color_state_off, "#9E9E9E"}',
  '" data-unset="1" data-icon-color="binary">', 'data-icon-color="binary-clear" data-state="']) {
  assert.ok(html.includes(marker), `icon color HTML: ${marker}`);
}
assert.ok(read('src/types/types_registry.cpp').includes('if (ctx.tab_id) append_tile_icon_color_fields_html(html, *ctx.tab_id);'),
  'Full page and lazy folder fragments share the block');
const i18nHeader = read('src/core/i18n/i18n.h');
const fields = ['tile_icon_color', 'tile_icon_color_by_value', 'tile_icon_color_bar_off', 'tile_icon_color_smooth',
  'tile_icon_color_steps', 'tile_icon_color_min', 'tile_icon_color_max', 'tile_icon_color_preset_cold_warm',
  'tile_icon_color_preset_traffic', 'tile_icon_color_preset_battery', 'tile_icon_color_preset_humidity',
  'tile_icon_color_preset_single', 'tile_icon_color_bar_hint', 'tile_icon_color_by_state', 'tile_icon_color_state',
  'tile_icon_color_contains', 'tile_icon_color_add_state', 'tile_icon_color_remove', 'tile_icon_color_state_hint',
  'tile_icon_color_state_on', 'tile_icon_color_state_off'];
assert.match(i18nHeader, new RegExp(fields.map(field => `const char\\* ${field};`).join('\\s*')),
  'Icon color strings in struct order');
for (const field of fields) assert.ok(html.includes(`tr.${field}`), `HTML uses tr.${field}`);
const i18n = read('src/core/i18n/i18n.cpp');
const tables = {de: i18n.slice(i18n.indexOf('kStringsDe = {'), i18n.indexOf('kStringsEn = {')),
  en: i18n.slice(i18n.indexOf('kStringsEn = {'), i18n.indexOf('kStringsFr = {')),
  fr: i18n.slice(i18n.indexOf('kStringsFr = {'), i18n.indexOf('kLocaleDe'))};
const translations = {
  de: ['Icon-Farbe', 'Icon-Farbe nach Wert', 'Aus', 'Fließend', 'Stufen', 'Min', 'Max', 'Kalt → Warm', 'Ampel', 'Batterie',
    'Luftfeuchte', 'Einfarbig', null, 'Icon-Farbe nach Zustand', 'Zustand', 'enthält', 'Zustand hinzufügen', 'Entfernen', null, 'Ein', 'Aus'],
  en: ['Icon color', 'Icon color by value', 'Off', 'Smooth', 'Steps', 'Min', 'Max', 'Cold → Warm', 'Traffic light', 'Battery',
    'Humidity', 'Single color', null, 'Icon color by state', 'State', 'contains', 'Add state', 'Remove', null, 'On', 'Off'],
  fr: ["Couleur de l'icône", 'Couleur selon la valeur', 'Désactivée', 'Progressif', 'Paliers', 'Min', 'Max', 'Froid → Chaud',
    'Feu tricolore', 'Batterie', 'Humidité', 'Couleur unique', null, "Couleur selon l'état", 'État', 'contient',
    'Ajouter un état', 'Supprimer', null, 'Activé', 'Désactivé'],
};
const glowLabels = {de: '"Kreis in Icon-Farbe",', en: '"Circle in icon color",', fr: '"Cercle couleur de l\'icône",'};
for (const [language, texts] of Object.entries(translations)) {
  // The strings follow the glow label of each table in struct order.
  const start = tables[language].indexOf(glowLabels[language]) + glowLabels[language].length;
  const entries = [...tables[language].slice(start).matchAll(/^\s*"((?:[^"\\]|\\.)*)",/gm)].slice(0, texts.length).map(m => m[1]);
  texts.forEach((text, index) => {
    if (text === null) assert.ok(entries[index].length > 20, `${language}: hint ${index}`);
    else assert.equal(entries[index], text.replace(/'/g, "'"), `${language}: ${fields[index]}`);
  });
}

// ---------------------------------------------------------------------------
// Native: firmware record model, colors and sidecar persistence.
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version']).status === 0);
let nativeChecked = false;
if (!compiler) {
  console.log('SKIP: Icon color native checks need a C++ compiler');
} else {
  const cStr = value => value === null ? 'nullptr' : JSON.stringify(value);
  const tileConfigHeader = read('src/tiles/config/tile_config.h');
  const normalizeStart = tileConfigHeader.indexOf('static inline String normalizeTileIconColors(');
  const sourceStart = tileConfigHeader.indexOf('static inline String tileIconSourceEntity(');
  const normalizeTile = tileConfigHeader.slice(normalizeStart, tileConfigHeader.indexOf('}\n', normalizeStart) + 2) +
    tileConfigHeader.slice(sourceStart, tileConfigHeader.indexOf('\n}\n', sourceStart) + 3);
  const cpp = String.raw`
#include "src/tiles/config/tile_icon_colors.h"
#include "src/tiles/config/tile_tint.h"
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
std::string norm(const char* in,bool bar,bool rows){char out[tile_icon_colors::kMaxRecordBytes+1];tile_icon_colors::normalize(in,out,sizeof(out),bar,rows);return out;}
void reboot(){g_sidecar_index_built=false;g_icon_color_sidecar_keys.clear();}
int main(){
 using namespace tile_icon_colors;
 struct N{const char* in;bool bar;bool rows;};
 const N normalize_cases[]={${normalizeCases.map(([i, b, r]) => `{${cStr(i)},${b},${r}}`).join(',')}};
 for(const N& c:normalize_cases){const std::string out=norm(c.in,c.bar,c.rows);std::printf("N:");for(char ch:out)std::printf("%02X",static_cast<unsigned char>(ch));std::printf("\n");}
 struct R{const char* record;const char* state;const char* display;};
 const R resolve_cases[]={${resolveCases.map(([r, s, d]) => `{${cStr(r)},${cStr(s)},${cStr(d)}}`).join(',')}};
 for(const R& c:resolve_cases){uint32_t rgb=0;if(resolve(c.record,c.state,c.display,rgb))std::printf("R:#%06X\n",static_cast<unsigned>(rgb));else std::printf("R:-\n");}
 struct TT{uint32_t base;uint32_t color;unsigned percent;};
 for(const TT& c:{TT{0x2A2A2A,0xFFC107,25},TT{0x2A2A2A,0xFFFFFF,50},TT{0x4A148C,0xF44336,30},TT{0xFFFFFF,0xFFE082,10}})
  std::printf("T:#%06X\n",static_cast<unsigned>(tile_tint::background(c.base,c.color,c.percent)));
 {uint32_t rgb=0;assert(!resolve("v2\nFF0000\nhas 00FF00 6","5",nullptr,rgb,false));assert(resolve("v2\nFF0000\nhas 00FF00 6","5",nullptr,rgb)&&rgb==0xFF0000);}
 auto normLayer=[](const char* in,bool bar,bool rows,bool self){char out[kMaxRecordBytes+1];normalize(in,out,sizeof(out),bar,rows,true,self);return std::string(out);};
 assert(normLayer("v2\n\nsrc rules self\nhas F44336 6",false,true,true)=="v2\n\nhas F44336 6");
 assert(normLayer("v2\n\nsrc rules self off\nhas F44336 6",false,true,true)=="v2\n\nsrc rules self off\nhas F44336 6");
 assert(normLayer("v2\n\nsrc auto self noicon tile=5\nhas F44336 6",false,true,true)=="v2\n\nsrc auto self tile=10 noicon");
 assert(normLayer("v2\n\nsrc rules self tile=30 off noicon\nis 4CAF50 on",false,false,true)=="v2\n\nsrc rules self tile=30 noicon off\nis 4CAF50 on");
 assert(normLayer("v2\nFF0000\nsrc rules self",false,false,false)=="v2\nFF0000");
 assert(normLayer("v2\n\nsrc rules sensor.waste tile=99\nhas f44336 6",false,false,false)=="v2\n\nsrc rules sensor.waste tile=50\nhas F44336 6");
 assert(normLayer("v2\n\nsrc rules sensor.waste bogus",false,false,false)=="");
 assert(own_state_colors_icon("v2\n\nhas F44336 6")&&!own_state_colors_icon("v2\n\nsrc rules self off")&&!own_state_colors_icon("v2\n\nsrc rules sensor.x"));
 uint32_t rgb=0;assert(!resolve("","on",nullptr,rgb)&&!resolve(nullptr,"on",nullptr,rgb)&&!resolve("v2\n\nis FFC107 on",nullptr,nullptr,rgb));
 // Worst case (source, bar and six states) fits the sidecar limit.
 std::string worst="v2\nFFFFFF\nfill 50\nsrc rules a."+std::string(126,'b')+" tile=50 noicon off\nbar smooth -12345678901 999999999999 1000:000000 1000:000000 1000:000000 1000:000000 1000:000000 1000:000000";
 for(int i=0;i<6;++i){worst+="\nhas FFFFFF ";worst+=std::string(64,'w');}
 {char out[kMaxRecordBytes+1];assert(normalize(worst.c_str(),out,sizeof(out),false,false,true)==kMaxRecordBytes);}
 // Icon-and-title tiles: a source entity; "rules" keeps the bar and states,
 // "auto" drops them; other types never keep a source line.
 assert(normalizeTileIconColors(TILE_FOLDER,"v2\nff0000\nsrc rules sensor.waste\nhas f44336 6\nbar steps 0 10 0:000000 1000:FFFFFF")=="v2\nFF0000\nsrc rules sensor.waste\nbar steps 0 10 0:000000 1000:FFFFFF\nhas F44336 6");
 assert(normalizeTileIconColors(TILE_SCENE,"v2\n\nsrc auto light.kitchen\nhas f44336 6")=="v2\n\nsrc auto light.kitchen");
 assert(normalizeTileIconColors(TILE_CAMERA,"v2\n\nsrc auto Light.Kitchen")=="");
 assert(normalizeTileIconColors(TILE_BACK,"v2\n\nsrc maybe light.kitchen")=="");
 assert(normalizeTileIconColors(TILE_SENSOR,"v2\n\nsrc rules sensor.waste\nhas f44336 6")=="v2\n\nsrc rules sensor.waste\nhas F44336 6");
 assert(tileIconSourceEntity(TILE_FOLDER,"v2\n\nsrc rules sensor.waste\nhas F44336 6")=="sensor.waste");
 assert(tileIconSourceEntity(TILE_SENSOR,"v2\n\nsrc rules sensor.waste")=="sensor.waste");assert(tileIconSourceEntity(TILE_SENSOR,"v2\n\nsrc rules self tile=20")=="");
 assert(resolve("v2\n00FF00\nsrc rules sensor.waste\nhas F44336 6","in 6 Tagen rausstellen",nullptr,rgb)&&rgb==0xF44336);
 assert(resolve("v2\n00FF00\nsrc rules sensor.waste\nhas F44336 6","",nullptr,rgb)&&rgb==0x00FF00);
 // Device path of the maintainer case: POST normalization for a Sensor, then the state update.
 const String waste=normalizeTileIconColors(TILE_SENSOR,"v2\n\nhas f44336 6");
 assert(waste=="v2\n\nhas F44336 6");
 assert(resolve(waste.c_str(),"in 6 Tagen rausstellen",nullptr,rgb)&&rgb==0xF44336);
 // Persistence for every icon color type: reboot, no-op, removal, recovery.
 const std::string full="v2\nFF8800\nbar smooth 0 100 0:3B82F6 1000:EF4444\nis 4CAF50 on";
 for(TileType type:{TILE_SENSOR,TILE_ENERGY,TILE_BINARY_SENSOR,TILE_NUMBER,TILE_SELECT,TILE_DATETIME}){
  const String expected=normalizeTileIconColors(type,full.c_str());
  assert(expected.size()>std::string("v2\nFF8800").size());
  assert(writeIconColorsSd(3,1,expected));TileGridConfig grid;grid.tiles[1].type=type;
  reboot();applyIconColorsFromSd(3,grid);assert(grid.tiles[1].icon_colors==expected);
  const int before=writes;assert(writeIconColorsSd(3,1,expected));assert(writes==before);
  assert(writeIconColorsSd(3,1,""));reboot();String gone;assert(!readIconColorsSd(3,1,gone));
 }
 assert(normalizeTileIconColors(TILE_NUMBER,full.c_str())=="v2\nFF8800\nbar smooth 0 100 0:3B82F6 1000:EF4444");
 assert(normalizeTileIconColors(TILE_SELECT,full.c_str())=="v2\nFF8800\nis 4CAF50 on");
 // Icon-and-title tiles keep only the fixed color, also from a stale file.
 assert(writeIconColorsSd(3,2,full));
 for(TileType type:{TILE_SCENE,TILE_FOLDER,TILE_BACK,TILE_CAMERA}){
  assert(normalizeTileIconColors(type,full.c_str())=="v2\nFF8800");
  TileGridConfig grid;grid.tiles[2].type=type;reboot();applyIconColorsFromSd(3,grid);assert(grid.tiles[2].icon_colors=="v2\nFF8800");
 }
 // Tiles with their own state colors keep a fixed color and their rules;
 // own rules there may use the bar and the state list.
 assert(normalizeTileIconColors(TILE_SWITCH,full.c_str())=="v2\nFF8800");
 assert(normalizeTileIconColors(TILE_SWITCH,"v2\nFF8800\nsrc rules self tile=30\nis 4CAF50 on")=="v2\nFF8800\nsrc rules self tile=30\nis 4CAF50 on");
 assert(normalizeTileIconColors(TILE_FOLDER,"v2\n\nsrc rules self\nis 4CAF50 on")=="");
 assert(normalizeTileIconColors(TILE_CLOCK,"v2\n\nsrc auto light.x tile=20")=="v2\n\nsrc auto light.x tile=20");
 // Other types never take a record, even from a stale file.
 for(TileType type:{TILE_EMPTY,TILE_SETTINGS,TILE_PIXELANIM}){
  TileGridConfig grid;grid.tiles[2].type=type;reboot();applyIconColorsFromSd(3,grid);assert(grid.tiles[2].icon_colors.empty());
  assert(normalizeTileIconColors(type,full.c_str()).empty());
 }
 // A b39 sidecar loads without a crash and is migrated; a damaged file is normalized.
 files[iconColorPathFile(3,2)]="FF8800\nge F44336 30\nge FF9800 20";TileGridConfig old;old.tiles[2].type=TILE_SENSOR;
 reboot();applyIconColorsFromSd(3,old);assert(old.tiles[2].icon_colors=="v2\nFF8800\nbar steps 10 30 0:FF8800 500:FF9800 1000:F44336");
 files[iconColorPathFile(3,2)]="v2\n#00ff00\nbogus\nbar smooth 1 0 0:000000 1000:FFFFFF\nhas 112233 x";old.tiles[2].type=TILE_SENSOR;
 reboot();applyIconColorsFromSd(3,old);assert(old.tiles[2].icon_colors=="v2\n00FF00\nhas 112233 x");
 // Recovery from .tmp and .bak like the long-title sidecar.
 const String record="v2\nFF8800\nis 4CAF50 on";
 const String path=iconColorPathFile(5,0);assert(writeIconColorsSd(5,0,record));
 files[tmpPathFor(path)]=record;files.erase(path);reboot();String restored;assert(readIconColorsSd(5,0,restored)&&restored==record);
 files[backupPathFor(path)]=record;files.erase(tmpPathFor(path));reboot();assert(readIconColorsSd(5,0,restored)&&restored==record);
 assert(writeIconColorsSd(5,0,""));reboot();assert(!readIconColorsSd(5,0,restored));
 // Failed writes keep the previous record; oversized records are refused.
 assert(writeIconColorsSd(6,3,record));fail_write=true;assert(!writeIconColorsSd(6,3,"v2\n00FF00"));fail_write=false;
 assert(readIconColorsSd(6,3,restored)&&restored==record);
 fail_rename=true;assert(!writeIconColorsSd(6,3,"v2\n00FF00"));fail_rename=false;assert(readIconColorsSd(6,3,restored)&&restored==record);
 assert(!writeIconColorsSd(6,3,String(kMaxRecordBytes+1,'x')));ready=false;assert(!writeIconColorsSd(6,3,"v2\n00FF00"));
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
  const lines = result.stdout.trim().split(/\r?\n/);
  const nativeNormalized = lines.filter(line => line.startsWith('N:'))
    .map(line => Buffer.from(line.slice(2), 'hex').toString('utf8'));
  normalizeCases.forEach(([input, , , expected], index) => {
    assert.equal(nativeNormalized[index], expected, `firmware normalize ${JSON.stringify(input)}`);
  });
  const nativeColors = lines.filter(line => line.startsWith('R:')).map(line => line.slice(2));
  assert.deepEqual(nativeColors, previewColors, 'Firmware and Web Admin preview give identical colors');
  const nativeTints = lines.filter(line => line.startsWith('T:')).map(line => line.slice(2));
  assert.deepEqual(nativeTints, previewTints, 'Firmware and Web Admin preview give identical tile tints');
  nativeChecked = true;
}

// ---------------------------------------------------------------------------
// Headless Chrome: the real editor module with the real type handlers.
const rows = [0, 1, 2, 3, 4, 5].map(i => `<div class="tile-icon-rule hidden" id="t_tile_icon_rule_${i}">
<input type="text" id="t_tile_icon_rule_${i}_value" data-icon-color="value">
<label class="icon-color-contains"><input type="checkbox" id="t_tile_icon_rule_${i}_has" data-icon-color="has"> contains</label>
<input type="color" id="t_tile_icon_rule_${i}_color" value="#22C55E" data-icon-color="rule-color">
<button type="button" class="tile-color-reset-btn" data-icon-color="remove" data-rule="${i}">x</button></div>`).join('');
const block = `<div id="t_tile_icon_color_fields" class="tile-icon-color-fields hidden" data-tab="t">
<input type="color" id="t_tile_icon_color" value="#FFFFFF" data-unset="1" data-icon-color="color">
<button type="button" id="clear" data-icon-color="clear">r</button>
<div class="icon-color-section hidden" id="t_tile_icon_source_section"><input type="hidden" id="t_tile_icon_rules_on" value="0">
<div class="icon-color-segmented"><button type="button" data-icon-color="rules-on" data-mode="0">Off</button><button type="button" data-icon-color="rules-on" data-mode="1">On</button></div>
<div class="icon-color-rules-body hidden" id="t_tile_icon_rules_body"><input type="hidden" id="t_tile_icon_source_kind" value="self">
<div class="icon-color-segmented" id="t_tile_icon_source_kinds"><button type="button" data-icon-color="source-kind" data-mode="self">Own</button><button type="button" data-icon-color="source-kind" data-mode="other">Other</button></div>
<select id="t_tile_icon_source" data-icon-color="source"><option value="">None</option></select><input type="hidden" id="t_tile_icon_source_mode" value="rules">
<div class="icon-color-segmented" id="t_tile_icon_source_modes"><button type="button" data-icon-color="source-mode" data-mode="auto">A</button><button type="button" data-icon-color="source-mode" data-mode="rules">R</button></div>
<label class="inline-checkbox"><input type="checkbox" id="t_tile_icon_rule_icon" data-icon-color="rule-target" checked> Icon</label>
<label class="inline-checkbox"><input type="checkbox" id="t_tile_icon_rule_tile" data-icon-color="rule-target"> Tile</label>
<div class="icon-color-strength hidden" id="t_tile_icon_rule_strength_row"><input type="range" id="t_tile_icon_rule_strength" min="10" max="50" step="5" value="20" data-icon-color="rule-strength"><output id="t_tile_icon_rule_strength_value">20 %</output></div>
<p class="hint hidden" id="t_tile_icon_rule_follows_icon">follows</p><input type="checkbox" id="t_tile_icon_fill" hidden></div></div>
<div class="icon-color-section hidden" id="t_tile_icon_bar_section"><input type="hidden" id="t_tile_icon_bar" value="">
<div class="icon-color-segmented"><button type="button" data-icon-color="mode" data-mode="off">Off</button><button type="button" data-icon-color="mode" data-mode="smooth">Smooth</button><button type="button" data-icon-color="mode" data-mode="steps">Steps</button></div>
<div class="icon-color-bar-editor hidden" id="t_tile_icon_bar_editor"><div class="icon-color-presets">
${['cold_warm', 'traffic', 'battery', 'humidity', 'single'].map(p => `<button type="button" class="icon-color-chip" data-icon-color="preset" data-preset="${p}">${p}</button>`).join('')}</div>
<div class="icon-color-bar" id="t_tile_icon_bar_strip" data-icon-color="bar"></div>
<div class="icon-color-handles" id="t_tile_icon_bar_handles" data-selected="-1"><button type="button" class="icon-color-stop-remove hidden" data-icon-color="stop-remove">x</button></div>
<input type="color" class="icon-color-stop-picker" id="t_tile_icon_stop_color" data-icon-color="stop-color">
<div class="icon-color-range"><label>Min<input type="text" id="t_tile_icon_bar_min" data-icon-color="min"></label><label>Max<input type="text" id="t_tile_icon_bar_max" data-icon-color="max"></label></div></div></div>
<div class="icon-color-section hidden" id="t_tile_icon_state_section">${rows}
<button type="button" class="btn btn-secondary tile-icon-rule-add" id="t_tile_icon_rule_add" data-icon-color="add">+</button></div>
<div class="icon-color-section hidden" id="t_tile_icon_binary_section">
<input type="color" id="t_tile_icon_on" value="#FFC107" data-default="#FFC107" data-unset="1" data-icon-color="binary"><button type="button" id="clear_on" data-icon-color="binary-clear" data-state="on">r</button>
<input type="color" id="t_tile_icon_off" value="#9E9E9E" data-default="#9E9E9E" data-unset="1" data-icon-color="binary"><button type="button" id="clear_off" data-icon-color="binary-clear" data-state="off">r</button></div></div>`;
const registry = Object.fromEntries(Object.entries(handlers).map(([type, [name]]) =>
  [type, {load: `load${name}Fields`, save: `save${name}Fields`, reset: `reset${name}Fields`, fields: name.toLowerCase()}]));
const page = `<!doctype html><html lang="en"><head><style>${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div class="tile-settings" style="width:400px">
<select id="t_tile_type">${['0', '1', '14', '20', '21', '22', '23', '5', '2', '4'].map(v => `<option value="${v}">${v}</option>`).join('')}</select>
<select id="t_sensor_entity"><option value=""></option><option value="sensor.temp">t</option><option value="sensor.waste">w</option></select>
<select id="t_select_entity"><option value=""></option><option value="input_select.mode">m</option></select>
${block}</div><pre id="result"></pre><script>
const nativeListen=document.addEventListener.bind(document);document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY=${JSON.stringify(Object.assign({0: {}}, registry))};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
window.fetch=async()=>({json:async()=>({success:true})});
${inlineScriptSafe(readAdminDeliverySource())}
try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const calls=[];updateTilePreview=t=>calls.push('preview');updateDraft=t=>calls.push('draft');scheduleAutoSave=t=>calls.push('autosave');
 sensorMetaCache=normalizeSensorMetaPayload({values:{'sensor.temp':'21.5','sensor.waste':'in 6 Tagen rausstellen'},
   editable_values:{'input_select.mode':JSON.stringify({version:1,state:'eco',available:true,kind:'select',options:['eco','comfort','away']})}});
 sensorMetaCache.loaded=true;
 const $=id=>document.getElementById(id);
 const setType=type=>{$('t_tile_type').value=type;};
 const load=(type,data)=>{setType(type);callTypeHandler(getTileTypeMeta(type),'load','t',data);};
 const snapshot=()=>collectTypeFieldValues('t').icon_colors;
 const hidden=id=>$(id).classList.contains('hidden');
 const click=el=>el.dispatchEvent(new MouseEvent('click',{bubbles:true}));
 const knobs=()=>[...document.querySelectorAll('#t_tile_icon_bar_handles .icon-color-stop')];
 // Numeric Sensor: load, visible bar, round trip through the real handlers.
 const cold='v2\\n\\nbar smooth 0 100 0:3B82F6 450:22C55E 700:F59E0B 1000:EF4444';
 load('1',{sensor_entity:'sensor.temp',icon_colors:cold});
 check(!hidden('t_tile_icon_color_fields')&&!hidden('t_tile_icon_bar_section')&&hidden('t_tile_icon_state_section')&&hidden('t_tile_icon_binary_section'),'Numeric Sensor shows the bar');
 check(snapshot()===cold,'Draft snapshot carries the bar: '+snapshot());
 check(knobs().length===4&&knobs()[1].style.left==='45%','Handles sit on their stops');
 check([...document.querySelectorAll('.icon-color-stop-label')].map(l=>l.textContent).join(',')==='0,45,70,100','Value labels follow min/max');
 check($('t_tile_icon_bar_strip').style.background.includes('linear-gradient'),'Bar shows the gradient');
 check(document.querySelector('[data-mode="smooth"]').classList.contains('active'),'Smooth is active');
 // Mode Steps, then presets.
 calls.length=0;click(document.querySelector('[data-mode="steps"]'));
 check(snapshot()==='v2\\n\\nbar steps 0 100 0:3B82F6 450:22C55E 700:F59E0B 1000:EF4444','Steps mode');
 check(calls.join()==='preview,draft,autosave','Changes take the live-editor path');
 click(document.querySelector('[data-preset="battery"]'));
 check(snapshot()==='v2\\n\\nbar steps 0 100 0:EF4444 250:F59E0B 600:22C55E 1000:22C55E','Battery preset keeps the mode');
 click(document.querySelector('[data-preset="traffic"]'));
 check(snapshot()==='v2\\n\\nbar steps 0 100 0:22C55E 500:EAB308 1000:EF4444','Traffic light preset');
 // Drag the middle stop to 25 % with pointer events.
 const rect=$('t_tile_icon_bar_handles').getBoundingClientRect();
 const knob=knobs()[1];const at=f=>rect.left+rect.width*f;
 knob.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,pointerId:7,button:0,clientX:at(0.5)}));
 knob.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,pointerId:7,clientX:at(0.25)}));
 check(knob.style.left==='25%'&&knobs()[1]===knob,'Dragging moves the same handle');
 knob.dispatchEvent(new PointerEvent('pointerup',{bubbles:true,pointerId:7,clientX:at(0.25)}));
 click(knob);
 check(snapshot()==='v2\\n\\nbar steps 0 100 0:22C55E 250:EAB308 1000:EF4444','Drag moves the stop: '+snapshot());
 check(!hidden('t_tile_icon_bar_handles')&&!document.querySelector('.icon-color-stop-remove').classList.contains('hidden'),'The selected stop offers remove');
 // Dragging past a neighbour re-sorts on release.
 knob.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,pointerId:8,button:0,clientX:at(0.25)}));
 knob.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,pointerId:8,clientX:at(1.2)}));
 knob.dispatchEvent(new PointerEvent('pointerup',{bubbles:true,pointerId:8,clientX:at(1.2)}));
 check(snapshot()==='v2\\n\\nbar steps 0 100 0:22C55E 1000:EF4444 1000:EAB308','Stops stay sorted: '+snapshot());
 // Double-click adds a stop with the color shown there; smooth mixes.
 click(document.querySelector('[data-mode="smooth"]'));
 $('t_tile_icon_bar_strip').dispatchEvent(new MouseEvent('dblclick',{bubbles:true,clientX:$('t_tile_icon_bar_strip').getBoundingClientRect().left+$('t_tile_icon_bar_strip').getBoundingClientRect().width*0.5}));
 check(/^v2\\n\\nbar smooth 0 100 0:22C55E 5\\d\\d:[0-9A-F]{6} 1000:EF4444 1000:EAB308$/.test(snapshot()),'Double-click adds a stop: '+snapshot());
 check(knobs()[1].classList.contains('selected'),'The new stop is selected');
 click(document.querySelector('.icon-color-stop-remove'));
 check(snapshot()==='v2\\n\\nbar smooth 0 100 0:22C55E 1000:EF4444 1000:EAB308','Remove drops the selected stop');
 click(knobs()[0]);click(document.querySelector('.icon-color-stop-remove'));click(knobs()[0]);click(document.querySelector('.icon-color-stop-remove'));
 check(knobs().length===2,'At least two stops remain');
 // Stop color picker, range inputs and Off.
 click(knobs()[0]);$('t_tile_icon_stop_color').value='#123456';$('t_tile_icon_stop_color').dispatchEvent(new Event('input',{bubbles:true}));
 check(snapshot().endsWith(' 1000:123456 1000:EAB308'),'The picker recolors the selected stop: '+snapshot());
 $('t_tile_icon_bar_min').value='-10,5';$('t_tile_icon_bar_min').dispatchEvent(new Event('input',{bubbles:true}));
 check(snapshot().startsWith('v2\\n\\nbar smooth -10.5 100 '),'Min accepts a comma');
 $('t_tile_icon_bar_max').value='-20';check(snapshot()==='','min >= max drops the bar like the firmware');
 $('t_tile_icon_bar_max').value='40';
 click(document.querySelector('[data-mode="off"]'));
 check(snapshot()===''&&hidden('t_tile_icon_bar_editor'),'Off removes the bar');
 click(document.querySelector('[data-mode="steps"]'));
 check(snapshot().startsWith('v2\\n\\nbar steps -10.5 40 1000:123456 1000:EAB308'),'Turning it on again restores the last stops: '+snapshot());
 // Fixed color stays the fallback and clears to white.
 $('t_tile_icon_color').value='#00bcd4';$('t_tile_icon_color').dispatchEvent(new Event('input',{bubbles:true}));
 check(snapshot().startsWith('v2\\n00BCD4\\nbar steps'),'Fixed color is kept');
 click($('clear'));check(snapshot().startsWith('v2\\n\\nbar')&&$('t_tile_icon_color').value==='#ffffff','Clear shows white');
 callTypeHandler(getTileTypeMeta('1'),'reset','t');check(snapshot()==='','Reset clears everything');
 // Text Sensor (maintainer case): Rules on, the state list, contains 6.
 load('1',{sensor_entity:'sensor.waste',icon_colors:''});
 check($('t_tile_icon_rules_on').value==='0'&&hidden('t_tile_icon_rules_body')&&hidden('t_tile_icon_state_section'),'Without colors the rules start off');
 click(document.querySelector('[data-icon-color="rules-on"][data-mode="1"]'));
 check(!hidden('t_tile_icon_rules_body')&&!hidden('t_tile_icon_source_kinds')&&hidden('t_tile_icon_source'),'Own entity by default');
 check(hidden('t_tile_icon_bar_section')&&!hidden('t_tile_icon_state_section'),'Text Sensor shows the state list');
 check(!document.querySelector('datalist'),'No native suggestion popup on the state field');
 click($('t_tile_icon_rule_add'));check(document.activeElement===$('t_tile_icon_rule_0_value'),'Add focuses the text');
 $('t_tile_icon_rule_0_value').value='6';$('t_tile_icon_rule_0_has').checked=true;$('t_tile_icon_rule_0_has').dispatchEvent(new Event('change',{bubbles:true}));
 $('t_tile_icon_rule_0_color').value='#f44336';$('t_tile_icon_rule_0_color').dispatchEvent(new Event('input',{bubbles:true}));
 check(snapshot()==='v2\\n\\nhas F44336 6','Contains row (own rules stay implicit): '+snapshot());
 check(resolveIconColorRecord(snapshot(),'in 6 Tagen rausstellen',null)==='#F44336','Preview colors the waste state');
 // Tint the tile, then switch the rules off: settings stay, no effect.
 $('t_tile_icon_rule_tile').checked=true;$('t_tile_icon_rule_tile').dispatchEvent(new Event('change',{bubbles:true}));
 check(!hidden('t_tile_icon_rule_strength_row')&&snapshot()==='v2\\n\\nsrc rules self tile=20\\nhas F44336 6','Tint tile: '+snapshot());
 $('t_tile_icon_rule_strength').value='35';$('t_tile_icon_rule_strength').dispatchEvent(new Event('input',{bubbles:true}));
 check($('t_tile_icon_rule_strength_value').textContent==='35 %'&&snapshot()==='v2\\n\\nsrc rules self tile=35\\nhas F44336 6','Strength: '+snapshot());
 $('t_tile_icon_rule_icon').checked=false;$('t_tile_icon_rule_icon').dispatchEvent(new Event('change',{bubbles:true}));
 check(snapshot()==='v2\\n\\nsrc rules self tile=35 noicon\\nhas F44336 6','Tile only: '+snapshot());
 // Tile color "From icon": the tile follows the icon, so "Tint tile" stays
 // visible but greyed out and not clickable (its setting is kept) and a note
 // says why; Global makes it active again. Nothing disappears.
 const tintLabel=()=>$('t_tile_icon_rule_tile').closest('label');
 $('t_tile_icon_fill').checked=true;syncIconColorFields('t');
 check(!tintLabel().classList.contains('hidden')&&tintLabel().classList.contains('is-disabled')&&$('t_tile_icon_rule_tile').disabled&&$('t_tile_icon_rule_tile').checked&&
  !hidden('t_tile_icon_rule_strength_row')&&$('t_tile_icon_rule_strength_row').classList.contains('is-disabled')&&$('t_tile_icon_rule_strength').disabled&&
  !hidden('t_tile_icon_rule_follows_icon')&&getComputedStyle(tintLabel()).opacity<0.6&&
  snapshot()==='v2\\n\\nfill 20\\nsrc rules self tile=35 noicon\\nhas F44336 6','From icon greys Tint tile out and keeps it: '+snapshot());
 $('t_tile_icon_fill').checked=false;syncIconColorFields('t');
 check(!tintLabel().classList.contains('is-disabled')&&!$('t_tile_icon_rule_tile').disabled&&!$('t_tile_icon_rule_strength').disabled&&
  !$('t_tile_icon_rule_strength_row').classList.contains('is-disabled')&&hidden('t_tile_icon_rule_follows_icon'),'Global makes Tint tile active again');
 click(document.querySelector('[data-icon-color="rules-on"][data-mode="0"]'));
 check(hidden('t_tile_icon_rules_body')&&snapshot()==='v2\\n\\nsrc rules self tile=35 noicon off\\nhas F44336 6','Off keeps the settings: '+snapshot());
 click(document.querySelector('[data-icon-color="rules-on"][data-mode="1"]'));
 $('t_tile_icon_rule_icon').checked=true;$('t_tile_icon_rule_icon').dispatchEvent(new Event('change',{bubbles:true}));
 $('t_tile_icon_rule_tile').checked=false;$('t_tile_icon_rule_tile').dispatchEvent(new Event('change',{bubbles:true}));
 for(let i=1;i<6;i++)click($('t_tile_icon_rule_add'));
 check(hidden('t_tile_icon_rule_add'),'At most six states');
 click(document.querySelector('#t_tile_icon_rule_0 [data-icon-color="remove"]'));
 check(!hidden('t_tile_icon_rule_add')&&snapshot()==='','Remove keeps the others (empty rows are not stored)');
 // Select: known options; Number: bar only; Binary: On/Off colors.
 load('22',{sensor_entity:'input_select.mode',icon_colors:'v2\\n\\nis 4CAF50 eco'});
 check($('t_tile_icon_rules_on').value==='1'&&snapshot()==='v2\\n\\nis 4CAF50 eco'&&hidden('t_tile_icon_bar_section'),'b40 records load with the rules on');
 load('21',{icon_colors:cold+'\\nis 4CAF50 eco'});
 check(snapshot()===cold&&hidden('t_tile_icon_state_section'),'Number keeps the bar only');
 load('20',{icon_colors:''});click(document.querySelector('[data-icon-color="rules-on"][data-mode="1"]'));
 check(!hidden('t_tile_icon_binary_section')&&hidden('t_tile_icon_state_section')&&hidden('t_tile_icon_bar_section'),'Binary shows On/Off');
 check($('t_tile_icon_on').value==='#ffc107'&&$('t_tile_icon_off').value==='#9e9e9e'&&snapshot()==='','Binary defaults are amber and grey, not stored');
 $('t_tile_icon_on').value='#4caf50';$('t_tile_icon_on').dispatchEvent(new Event('input',{bubbles:true}));
 check(snapshot()==='v2\\n\\nis 4CAF50 on','On color is stored as a state line');
 $('t_tile_icon_off').value='#607d8b';$('t_tile_icon_off').dispatchEvent(new Event('input',{bubbles:true}));
 check(snapshot()==='v2\\n\\nis 4CAF50 on\\nis 607D8B off','Off color too');
 load('20',{icon_colors:'v2\\n\\nis 4CAF50 on\\nis 607D8B off'});
 check($('t_tile_icon_on').value==='#4caf50'&&$('t_tile_icon_off').dataset.unset==='0','Binary colors load');
 click($('clear_on'));check(snapshot()==='v2\\n\\nis 607D8B off'&&$('t_tile_icon_on').value==='#ffc107','Reset returns On to amber');
 // Another entity: the Bridge entities once, Entity color or own rules.
 rebuildEntitySelect('t_tile_icon_source', iconColorSourceEntries({sensors:[{v:'sensor.waste',t:'Waste'}],
   switches:[{v:'light.kitchen',t:'Kitchen'}],binary_sensors:[{v:'sensor.waste'},{v:'Bad Entity'}]}));
 check([...$('t_tile_icon_source').options].map(o=>o.value).join()===',sensor.waste,light.kitchen','Source list merges valid Bridge entities once');
 load('4',{icon_colors:'v2\\nFF0000\\nsrc rules sensor.waste\\nhas F44336 6'});
 check(!hidden('t_tile_icon_rules_body')&&hidden('t_tile_icon_source_kinds')&&!hidden('t_tile_icon_source')&&$('t_tile_icon_source').value==='sensor.waste','Folder: another entity only');
 check(!hidden('t_tile_icon_state_section')&&hidden('t_tile_icon_bar_section'),'A text source shows the state list');
 check(snapshot()==='v2\\nFF0000\\nsrc rules sensor.waste\\nhas F44336 6','Folder record round trip: '+snapshot());
 calls.length=0;click(document.querySelector('[data-icon-color="source-mode"][data-mode="auto"]'));
 check(snapshot()==='v2\\nFF0000\\nsrc auto sensor.waste'&&hidden('t_tile_icon_state_section'),'Entity color drops the rules: '+snapshot());
 check(calls.join()==='preview,draft,autosave','The mode takes the live-editor path');
 $('t_tile_icon_source').value='light.kitchen';$('t_tile_icon_source').dispatchEvent(new Event('change',{bubbles:true}));
 check(snapshot()==='v2\\nFF0000\\nsrc auto light.kitchen','Changing the entity: '+snapshot());
 click(document.querySelector('[data-icon-color="rules-on"][data-mode="0"]'));
 check(snapshot()==='v2\\nFF0000\\nsrc auto light.kitchen off','Off keeps the entity without effect: '+snapshot());
 click(document.querySelector('[data-icon-color="rules-on"][data-mode="1"]'));
 $('t_tile_icon_source').value='';$('t_tile_icon_source').dispatchEvent(new Event('change',{bubbles:true}));
 check(snapshot()==='v2\\nFF0000','No entity keeps the fixed color only');
 load('2',{icon_colors:'v2\\n\\nsrc auto light.kitchen'});
 check($('t_tile_icon_source').value==='light.kitchen'&&$('t_tile_icon_source_mode').value==='auto'&&snapshot()==='v2\\n\\nsrc auto light.kitchen','Scene loads Entity color');
 // A Switch can tint its tile with its own light color.
 load('5',{icon_colors:''});click(document.querySelector('[data-icon-color="rules-on"][data-mode="1"]'));
 click(document.querySelector('[data-icon-color="source-mode"][data-mode="auto"]'));
 $('t_tile_icon_rule_tile').checked=true;$('t_tile_icon_rule_tile').dispatchEvent(new Event('change',{bubbles:true}));
 check(!hidden('t_tile_icon_source_kinds')&&snapshot()==='v2\\n\\nsrc auto self tile=20','Switch tints with its own color: '+snapshot());
 // Preview: the same colors as tile_icon_source.cpp.
 const meta2={values:{'light.kitchen':JSON.stringify({state:'on',rgb_color:[255,0,0]}),'light.off':'off',
   'sensor.waste':'in 6 Tagen rausstellen','binary_sensor.door':'on','sensor.temp':'21.5'}};
 check(iconColorSourcePreview('v2\\n\\nsrc auto light.kitchen',meta2)==='#FF0000','Entity color takes the light color');
 check(iconColorSourcePreview('v2\\n\\nsrc auto light.off',meta2)==='#B0B0B0','A light that is off is grey');
 check(iconColorSourcePreview('v2\\n00FF00\\nsrc rules sensor.waste\\nhas F44336 6',meta2)==='#F44336','Own rules evaluate the source state');
 check(iconColorSourcePreview('v2\\n00FF00\\nsrc rules sensor.other\\nhas F44336 6',meta2)==='#00FF00','An unknown source keeps the fixed color');
 check(iconColorSourcePreview('v2\\n\\nsrc auto binary_sensor.door',meta2)==='#FFC107','A Binary sensor that is on is amber');
 check(previewIconColor('4','v2\\n\\nsrc auto light.kitchen','',meta2,null,'')==='#FF0000','Folder previews use the source');
 check(previewIconColor('1','v2\\n\\nsrc rules sensor.waste\\nhas F44336 6','sensor.temp',meta2,null,'')==='#F44336','A Sensor can follow another entity');
 check(previewIconColor('1','v2\\n\\nsrc rules self off\\nhas F44336 21','sensor.temp',meta2,null,'#FFFFFF')==='#FFFFFF','Switched-off rules leave the icon');
 check(previewIconColor('5','v2\\n00BCD4','light.kitchen',meta2,null,'#FFD54F')==='#00BCD4','A fixed color overrides the Switch state color');
 const tint=iconColorTilePreviewTint('5','v2\\n\\nsrc auto self tile=25','light.kitchen',meta2);
 check(tint&&tint.color==='#FF0000'&&tint.percent===25,'Tile tint from the own light color');
 check(tileTintBackground('#2A2A2A','#FF0000',25)===tileTintBackground('#2a2a2a','#ff0000',25),'Tint is case-insensitive');
 check(!iconColorTilePreviewTint('1','v2\\n\\nsrc rules self tile=25\\nhas F44336 6','sensor.temp',meta2),'No rule match, no tint');
 setType('0');syncIconColorFields('t');check(hidden('t_tile_icon_color_fields'),'Types without rules hide the block');
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}
</script></body></html>`;
let domChecked = false;
try {
  domChecked = runDomHarness({label: 'Icon color editor', html: page, tmpPrefix: 'hometiles-icon-colors-'}) !== false;
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 600));
}
console.log(`Icon colors: record v2 model, b39 migration, smooth/steps bar, state and Binary colors, contracts and translations pass` +
  `${nativeChecked ? '; firmware == preview and sidecar persistence pass' : ''}${domChecked ? '; editor DOM (presets, drag, add, remove, rows, On/Off, drafts) passes' : ''}`);
