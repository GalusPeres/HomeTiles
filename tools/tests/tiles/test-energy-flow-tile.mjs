// Energy flow tile (27, types/energy_flow, decided design picture 65, user
// 2026-10-24): wired into every list a tile type needs, its five node
// sensors and options travel through the Web Admin (fields, live binding,
// draft snapshot, copy/paste, import), the device redraws only from marked
// states, its dots move on one shared timer that rests without a flow and
// never runs behind a popup, while asleep or off screen, a node tap opens
// that sensor's normal popup, every label comes from the central
// translations in every language, and the real preview code draws the
// mockup's tile from made-up states.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

// Stable ID, registry and its type module.
assert.match(read('src/types/tile_type.h'), /TILE_FAN = 26,\n\s*TILE_ENERGY_FLOW = 27\n\};/);
const registry = read('src/types/types_registry.cpp');
assert.match(registry, /TILE_ENERGY_FLOW,\s*"Energiefluss",\s*"energy-flow",\s*"energy_flow",\s*"energy_flow",\s*nullptr,\s*"loadEnergyFlowFields",\s*"saveEnergyFlowFields",\s*"resetEnergyFlowFields",\s*0x2A2A2A,\s*false,\s*render_energy_flow_wrapper,\s*apply_energy_flow_wrapper,\s*append_energy_flow_fields_wrapper,\s*append_energy_flow_styles,\s*append_energy_flow_scripts/);
assert.ok(registry.includes('case TILE_ENERGY_FLOW: return i18n::energy_flow_label(language, i18n::EnergyFlowLabel::Type);'));
for (const file of ['renderer.cpp', 'renderer.h', 'web_handler.cpp', 'web_html.cpp', 'web_scripts.cpp', 'web_styles.cpp', 'admin.js',
  'flow_model.h', 'flow_geometry.h', 'flow_metrics.h']) {
  assert.ok(fs.existsSync(path.join(root, 'src/types/energy_flow', file)), file);
}
assert.ok(JSON.parse(read('src/web/admin/bundle.json')).sources.includes('src/types/energy_flow/admin.js'));

// Web Admin fields: one tab-prefixed entity picker per node in Home
// Assistant's order and the options; the card starts on, the header off.
const html = read('src/types/energy_flow/web_html.cpp');
assert.ok(html.includes('_energy_flow_fields" class="type-fields">'));
for (const field of ['flow_pv_entity', 'flow_grid_entity', 'flow_battery_entity', 'flow_battery_charge_entity', 'flow_home_entity']) {
  assert.ok(html.includes(`{"${field}", EnergyFlowLabel::`), field);
}
assert.ok(html.includes('appendEntityPickerField(html, tab_id, node.field, i18n::energy_flow_label(language, node.label), "sensors");'));
assert.match(html, /append_checkbox\(html, tab_id, "flow_header", [^;]*, false\);/);
assert.match(html, /append_checkbox\(html, tab_id, "flow_card", [^;]*, true\);/);
const handler = read('src/types/energy_flow/web_handler.cpp');
for (const marker of ['if (server.hasArg("flow_entities")) tile.flow_entities = server.arg("flow_entities");',
  ': flowEntityAt(before, role);', 'normalizeFlowEntities(tile);', '{"flow_no_card", kFlowNoCardBit}',
  'tile.key_code &= kFlowOptionBits;', 'if (!server.hasArg(option.name)) continue;']) {
  assert.ok(handler.includes(marker), `web_handler: ${marker}`);
}

// Browser editor: live binding, draft snapshot, copy/paste and import keep
// the record and the options; a new tile grows to 2x2.
assert.match(read('src/web/admin/tiles/editor.js'), /for \(const field of \['flow_pv_entity', 'flow_grid_entity', 'flow_battery_entity', 'flow_battery_charge_entity',\s*'flow_home_entity', 'flow_header', 'flow_card', 'flow_grid_invert', 'flow_battery_invert'\]\) \{\s*bindLive\(document\.getElementById\(prefix \+ '_' \+ field\), 'change', field, \(\) => \{\s*updateTilePreview\(tab\); updateDraft\(tab\); scheduleAutoSave\(tab\);/);
assert.match(read('src/web/admin/tiles/snapshots.js'), /if \(Number\(tile\.type\) === 27 && snapshot && Object\.prototype\.hasOwnProperty\.call\(snapshot, 'flow_header'\)\) \{\s*tile\.key_code = energyFlowOptionBits\(snapshot\);/);
assert.match(read('src/web/admin/tiles/import-export.js'), /\} else if \(safeType === 27\) \{\s*fd\.append\('flow_entities', tile\.flow_entities \|\| ''\);/);
assert.ok(read('src/web/admin/tiles/layout.js').includes("if (Number(type) === 27) return [Math.min(2, placeCols()), Math.min(2, placeRows())];"));
assert.ok(read('src/web/admin/tiles/live-preview.js').includes("if (previewKind === 'energy_flow' && typeof applyEnergyFlowPreview === 'function') {"));
assert.ok(read('src/web/admin/tiles/grid-preview.js').includes('applyEnergyFlowPreview(el, tile, sensorMeta);'));

// Runtime: a node state marks the tiles showing it (and feeds an open
// Sensor popup); the loop redraws each marked tile once.
const tiles = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
assert.match(tiles, /const bool changed = cache_entity_payload\(entity_id, value\);[\s\S]{0,300}if \(grid_type == GridType::TAB0 && energy_flow_entity_changed\(entity_id\)\) \{\s*const String unit = haBridgeConfig\.findSensorUnit\(entity_id\);\s*queue_sensor_popup_value\(entity_id, value, unit\.length\(\) \? unit\.c_str\(\) : nullptr\);/);
assert.match(read('src/tiles/runtime/tile_update_service.h'), /process_media_update_queue\(drain_all \? 0 : 2\);\s*\/\/[^\n]*\n\s*process_energy_flow_updates\(\);/);
const renderer = read('src/types/energy_flow/renderer.cpp');
for (const marker of [
  // One step per display frame (60 fps, user 2026-10-24): the display's
  // refresh moves the dots, whole pixels counted, not elapsed time (b533's
  // 40 ms timer stuttered); without a shown dot LVGL's refresh rests.
  'for (const lv_event_code_t code : {LV_EVENT_REFR_START, LV_EVENT_RENDER_START, LV_EVENT_FLUSH_START,',
  'lv_display_add_event_cb(g_motion_display, motion_display_cb, code, nullptr);',
  'st.step[line] = watts >= kStep2Watts ? 2 : 1;',
  'st->phase[line] = fmodf(st->phase[line] + st->step[line], length);',
  'if (waiting) lv_timer_resume(lv_display_get_refr_timer(g_motion_display));',
  // Never behind a popup, asleep or off screen; only the dot's area, and
  // only the line pieces that touch it.
  'if (popup_shell_active() || powerManager.isInSleep()) return 0;',
  'if (!st || !any_live(*st) || !shown_on_screen(st->card)) continue;',
  'lv_obj_invalidate_area(st.canvas, &area);',
  'if (last <= first || !piece_on_clip(layer, canvas, path, first, last, dsc.width)) return;',
  // What the dots cost, one log line per 10 s while they move.
  'constexpr uint32_t kMotionLogMs = 10000;',
  'Serial.printf("[EnergyFlow] Dots %lu s: frames=%lu',
  // State changes only mark; unchanged texts are not set again.
  'st->dirty = true;',
  'if (shown && text == shown) return;',
  'if (redraw) lv_obj_invalidate(st.canvas);',
  // The lines skip areas they do not touch.
  'if (!lv_area_is_on(&box, &layer->_clip_area)) continue;',
  // A node tap opens that sensor's normal popup; the battery charge its own.
  'show_sensor_popup(init);',
  'kRoleBatteryCharge * 8 + kNodeBattery',
  // The state goes with the card.
  'lv_obj_add_event_cb(card, card_delete_cb, LV_EVENT_DELETE, st);',
  'st->~FlowTileState();',
  // Only MDI icons; the battery shows the charge Home Assistant's icon gives.
  'set_icon(st.node[kNodeBattery].icon, haBridgeConfig.findEntityIcon(battery_icon_entity), "battery");',
  // Without a card the rings take the global frame line.
  'if (frame_lines) ui_surface_style::apply_global_tile_border(w.ring);',
  // The bends are true circle arcs (b532's short lines looked wavy).
  'lv_draw_arc(layer, &arc_dsc);',
]) assert.ok(renderer.includes(marker), `renderer: ${marker}`);
// The decided node size on every layout: a 56 px disc in a 6 px ring
// (b532 took the device's smaller tile radius).
const metrics = read('src/types/energy_flow/flow_metrics.h');
assert.ok(metrics.includes('return tile_icon_disc::header_diameter(icon) - 2 * tile_icon_disc::corner_shrink();'),
  'the disc of a 1x1 tile as drawn (b532 took the tile radius, b533 the mockup 56 px)');
assert.ok(metrics.includes('m.ring = m.disc + tile_icon_disc::inset() + tile_icon_disc::corner_shrink();'),
  'the ring as wide as the tile disc gap to the card edge');
// Their shapes are the tiles': the card corner and its corner disc, not
// full circles on the V2.
assert.ok(renderer.includes('ui_surface_style::apply_radius(ring, tile_layout::scale_480(22), 0);'));
assert.ok(renderer.includes('ui_surface_style::apply_radius(disc, tile_icon_disc::radius_baseline() - tile_icon_disc::corner_shrink(), 0);'));
assert.doesNotMatch(renderer, /set_style_radius\([^;]*(ring|disc)[^;]*LV_RADIUS_CIRCLE/);
assert.doesNotMatch(renderer.slice(renderer.indexOf('uint32_t step_dots('), renderer.indexOf('struct DrawProbe {')),
  /Serial\.|String |new |malloc/, 'no logs or allocations per step');
assert.doesNotMatch(renderer.slice(renderer.indexOf('void motion_display_cb('), renderer.indexOf('void wake_motion(')),
  /Serial\.|String |new |malloc/, 'no logs or allocations per frame outside the rate-limited line');

// Every language has the ten labels, none empty; the Web Admin shows them
// through energy_flow_label only.
const header = read('src/core/i18n/i18n.h');
assert.ok(header.includes('const char* energy_flow_labels[10];'));
const i18n = read('src/core/i18n/i18n.cpp');
for (const lang of ['De', 'En', 'Fr', 'Pl', 'Cs', 'Es', 'Sk']) {
  const start = i18n.indexOf(`static const LocaleProfile kLocale${lang} = {`);
  assert.ok(start >= 0, lang);
  const table = i18n.slice(start, i18n.indexOf('"};\n', start) + 3);
  // Right before the entity picker labels (LocaleProfile order).
  const labels = /\n    \{("[^"]+"(?:, "[^"]+"){9})\},\n    \{"[^"]+", "[^"]+", "[^"]*\{query\}/.exec(table);
  assert.ok(labels, `${lang}: the ten energy flow labels before the entity picker labels`);
}
assert.doesNotMatch(html, /"(Solar|Grid|Battery|Home|Header|Card)[^"]*"/, 'no display text in the HTML generator');

// The real preview code on made-up states (user 2026-10-24: 200 W from the
// grid and 500 W from the battery): the home in the battery's color, the
// two flows in their sources' colors, the idle lines grey, the values.
const admin = read('src/types/energy_flow/admin.js');
const nodeOf = () => ({classList: {add() {}, toggle() {}, contains: () => false}, style: {setProperty() {}},
  querySelectorAll: () => [], isConnected: true, offsetWidth: 202, offsetHeight: 160, closest: () => null,
  insertAdjacentHTML(where, markup) { this.html = markup; }});
const vars = {'--radius-preview-scale': '0.5', '--preview-cell-w': '97', '--preview-cell-h': '76', '--preview-gap': '8',
  '--tile-card': '#1A1A1A', '--icon-glow-pct': '25'};
const context = vm.createContext({
  Math, Number, String, Object, Array, JSON, parseFloat, parseInt,
  ENERGY_FLOW_LAYOUT: {ring: 36, disc: 30, ringCorner: 30, discCorner: 24, insetX: 24, insetY: 20, spacing: 18, lineWidth: 4, dotRadius: 5, iconPx: 48,
    valuePx: 20, valueLine: 24, valueBase: 5, valueGap: 12, valueBelow: 15, edge: 6, titleRight: 17, titleTop: 25},
  document: {documentElement: {}},
  getComputedStyle: () => ({getPropertyValue: name => vars[name] || ''}),
  panelLight: () => false, panelVar: (name, fallback) => fallback, panelRoleHex: (name, fallback) => fallback,
  toneFill: (card, rgb) => ({disc: rgb.map(v => Math.round(v * 0.41))}),
  toneHex: rgb => '#' + rgb.map(v => v.toString(16).padStart(2, '0')).join('').toUpperCase(),
  toneBlend: (a) => a, toneReadableIcon: rgb => rgb,
  formatLocalizedNumber: (value, decimals) => Number(value).toFixed(decimals),
  escapeHtml: text => String(text).replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'})[c]),
  normalizeMdiIconName: name => String(name || '').replace(/^mdi:/, ''),
});
vm.runInContext(admin + '\nthis.applyEnergyFlowPreview = applyEnergyFlowPreview;', context);
const el = nodeOf();
const meta = {values: {'sensor.pv': '0', 'sensor.grid': '200', 'sensor.bat': '0.5', 'sensor.soc': '78'},
  units: {'sensor.pv': 'W', 'sensor.grid': 'W', 'sensor.bat': 'kW', 'sensor.soc': '%'}, icons: {'sensor.soc': 'mdi:battery-70'}};
context.applyEnergyFlowPreview(el, {flow_entities: 'sensor.pv\nsensor.grid\nsensor.bat\nsensor.soc', key_code: 0, span_w: 2, span_h: 2,
  title: '<b>Energie</b>'}, meta);
const drawn = el.html;
assert.ok(drawn.includes('width="404.00" height="320.00"'), 'the V2 2x2 tile in display pixels');
assert.ok(drawn.includes('transform:scale(0.5)'), 'scaled like every preview');
assert.ok(drawn.includes('<path d="M90 160 L314 160" fill="none" stroke="#488FC2"'), 'grid -> home in the grid color');
assert.ok(/<path d="M220 80 [^"]*" fill="none" stroke="#3C3C3C"/.test(drawn), 'solar -> home idle grey');
assert.ok(drawn.includes('<path d="M220 240 A62 62 0 0 1 282 178 L320 178" fill="none" stroke="#F06292"'),
  'battery -> home in the battery color, its bend a circle arc');
assert.ok(drawn.includes('mdi-battery-70'), 'the battery shows Home Assistant\'s level icon');
assert.ok(drawn.includes('color:#F06292"></i>') && /mdi-home" style="[^"]*color:#F06292/.test(drawn), 'home takes the main source color');
for (const value of ['0 W', '200 W', '700 W', '500 W', '78 %']) assert.ok(drawn.includes('>' + value + '</div>'), value);
assert.ok(!drawn.includes('Energie'), 'no header by default');
const headed = nodeOf();
context.applyEnergyFlowPreview(headed, {flow_entities: 'sensor.pv', key_code: 1, span_w: 2, span_h: 2, title: '<b>Energie</b>',
  icon_name: ''}, meta);
assert.ok(headed.html.includes('&lt;b&gt;Energie&lt;/b&gt;'), 'the header title is escaped');
assert.ok(headed.html.includes('mdi-lightning-bolt'), 'the header icon');
assert.ok(!headed.html.includes('mdi-transmission-tower'), 'a node without its entity is left out');
console.log('Energy flow tile: registry, Web Admin fields and editor, runtime marks and timer, popups, translations and the real preview');
