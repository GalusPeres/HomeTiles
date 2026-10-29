// Energy flow tile (28, types/energy_flow, decided design picture 65, user
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
assert.match(read('src/types/tile_type.h'), /TILE_ENERGY_FLOW = 28\n\};/);
const registry = read('src/types/types_registry.cpp');
assert.match(registry, /TILE_ENERGY_FLOW,\s*"Leistungsfluss",\s*"energy-flow",\s*"energy_flow",\s*"energy_flow",\s*nullptr,\s*"loadEnergyFlowFields",\s*"saveEnergyFlowFields",\s*"resetEnergyFlowFields",\s*0x2A2A2A,\s*false,\s*render_energy_flow_wrapper,\s*apply_energy_flow_wrapper,\s*append_energy_flow_fields_wrapper,\s*append_energy_flow_styles,\s*append_energy_flow_scripts/);
assert.ok(registry.includes('case TILE_ENERGY_FLOW: return i18n::energy_flow_label(language, i18n::EnergyFlowLabel::Type);'));
for (const file of ['renderer.cpp', 'renderer.h', 'web_handler.cpp', 'web_html.cpp', 'web_scripts.cpp', 'web_styles.cpp', 'admin.js',
  'flow_model.h', 'flow_geometry.h', 'flow_metrics.h']) {
  assert.ok(fs.existsSync(path.join(root, 'src/types/energy_flow', file)), file);
}
assert.ok(JSON.parse(read('src/web/admin/bundle.json')).sources.includes('src/types/energy_flow/admin.js'));

// Web Admin fields: one tab-prefixed entity picker per node in Home
// Assistant's order and the options; the card starts on, the header off.
// User 2026-10-26: the sensors at the top of the settings, "Invert sign"
// right under the grid and the battery, "Show title and icon" under Title
// and Icon, the card in Color; Auto names the type, not the solar sensor.
const html = read('src/types/energy_flow/web_html.cpp');
assert.ok(html.includes('_energy_flow_fields" class="type-fields" data-auto-title=")html";'));
assert.ok(html.includes('appendHtmlEscaped(html, i18n::energy_flow_label(language, EnergyFlowLabel::Type));'));
assert.ok(html.includes('data-auto-icon="lightning-bolt">'));
const placeAt = place => html.indexOf(`open_place(html, "${place}");`);
assert.ok(placeAt('head') > 0 && placeAt('head') < html.indexOf('"flow_header"') && html.indexOf('"flow_header"') < placeAt('top') &&
  placeAt('top') < html.indexOf('for (const auto& node : nodes)') && html.indexOf('for (const auto& node : nodes)') < placeAt('color') &&
  placeAt('color') < html.indexOf('"flow_card"'), 'header under the title, sensors on top, the card in Color');
assert.ok(html.includes('{"flow_grid_entity", EnergyFlowLabel::GridPower, "flow_grid_invert"}') &&
  html.includes('{"flow_battery_entity", EnergyFlowLabel::BatteryPower, "flow_battery_invert"}'), 'invert under its sensor');
assert.ok(html.includes('if (node.invert) {\n      append_checkbox(html, tab_id, node.invert, i18n::energy_flow_label(language, EnergyFlowLabel::InvertSign), false);'));
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
assert.match(read('src/web/admin/tiles/editor.js'), /for \(const field of \['flow_pv_entity', 'flow_grid_entity', 'flow_battery_entity', 'flow_battery_charge_entity',\s*'flow_home_entity', 'flow_header', 'flow_card', 'flow_grid_invert', 'flow_battery_invert'\]\) \{\s*bindLive\(document\.getElementById\(prefix \+ '_' \+ field\), 'change', field, \(\) => \{\s*if \(field === 'flow_header'\) energyFlowHeaderTitle\(prefix\);\s*updateTilePreview\(tab\); updateDraft\(tab\); scheduleAutoSave\(tab\);/);
assert.match(read('src/web/admin/tiles/snapshots.js'), /if \(Number\(tile\.type\) === 28 && snapshot && Object\.prototype\.hasOwnProperty\.call\(snapshot, 'flow_header'\)\) \{\s*tile\.key_code = energyFlowOptionBits\(snapshot\);/);
assert.match(read('src/web/admin/tiles/import-export.js'), /\} else if \(safeType === 28\) \{\s*fd\.append\('flow_entities', tile\.flow_entities \|\| ''\);/);
assert.ok(read('src/web/admin/tiles/layout.js').includes("if (Number(type) === 28) return [Math.min(2, placeCols()), Math.min(2, placeRows())];"));
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
  'st.step[line] = dot_speed(flows.watts[line], strongest);',
  'st->phase[line] = fmodf(st->phase[line] + st->step[line], length);',
  'if (moved || waiting) lv_timer_resume(lv_display_get_refr_timer(g_motion_display));',
  // Never behind a popup, asleep or off screen, and no frames asked for
  // there; only the dot's area, and only the part of a line's picture
  // inside it.
  'if (popup_shell_active()) {\n    still = kStillPopup;\n    return 0;\n  }',
  'if (powerManager.isInSleep()) {\n    still = kStillSleep;\n    return 0;\n  }',
  'if (image_screensaver_covers_ui()) {\n    still = kStillScreensaver;\n    return 0;\n  }',
  'if (!st || !any_live(*st)) continue;\n    if (lv_obj_t* hider = st->card ? hidden_by(st->card) : nullptr) {',
  'lv_obj_invalidate_area(st.canvas, &area);',
  // What the dots cost, one log line per 10 s while they move.
  'constexpr uint32_t kMotionLogMs = 10000;',
  'Serial.printf("[EnergyFlow] Dots %lu s: frames=%lu',
  // State changes only mark; unchanged texts are not set again.
  'st->dirty = true;',
  'if (shown && text == shown) return;',
  // A line that switches redraws only itself (b539: the whole card, 42-49 ms).
  'if (changed && st.line_shown[line]) invalidate_line(st, line);',
  // The lines skip areas they do not touch.
  'if (!lv_area_is_on(&box, &layer->_clip_area)) continue;',
  // A node tap opens that sensor's normal popup; the battery charge its own.
  'show_sensor_popup(init);',
  'make_value_tappable(*st, st->charge, kRoleBatteryCharge, kNodeBattery);',
  // The state goes with the card.
  'lv_obj_add_event_cb(card, card_delete_cb, LV_EVENT_DELETE, st);',
  'st->~FlowTileState();',
  // Only MDI icons; the battery shows the charge Home Assistant's icon gives.
  'charging ? charging_icon(has_charge, charge) : haBridgeConfig.findEntityIcon(battery_icon_entity),',
  // Without a card the rings take the global frame line.
  'if (frame_lines) ui_surface_style::apply_global_tile_border(w.ring);',
  // Each line is one smoothed stroke with a true circle bend (b532's short
  // lines looked wavy, b533-b567's LVGL pieces kinked where they met).
  'render_stroke(pixels, width, height, box.x1, box.y1, st.layout.line[line], static_cast<float>(st.line_width));',
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

// Every language has the nine labels, none empty; the Web Admin shows them
// through energy_flow_label only. The tile shows power, not energy (user
// 2026-10-26): "Leistungsfluss".
const header = read('src/core/i18n/i18n.h');
assert.ok(header.includes('const char* energy_flow_labels[9];'));
assert.match(header, /ShowCard,\s*InvertSign,\s*Count,/);
const i18n = read('src/core/i18n/i18n.cpp');
for (const lang of ['De', 'En', 'Fr', 'Pl', 'Cs', 'Es', 'Sk']) {
  const start = i18n.indexOf(`static const LocaleProfile kLocale${lang} = {`);
  assert.ok(start >= 0, lang);
  const table = i18n.slice(start, i18n.indexOf('"};\n', start) + 3);
  // Right before the entity picker labels (LocaleProfile order).
  const labels = /\n    \{("[^"]+"(?:, "[^"]+"){8})\},\n    \{"[^"]+", "[^"]+", "[^"]*\{query\}/.exec(table);
  assert.ok(labels, `${lang}: the nine energy flow labels before the entity picker labels`);
  if (lang === 'De') assert.ok(labels[1].startsWith('"Leistungsfluss"') && labels[1].includes('"Titel und Icon anzeigen"') &&
    labels[1].endsWith('"Vorzeichen umkehren"'), labels[1]);
  if (lang === 'En') assert.ok(labels[1].startsWith('"Power flow"'), labels[1]);
}
assert.doesNotMatch(i18n, /"(Energiefluss|Energy flow)"/, 'no energy in the name');
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
// Like Home Assistant's card (user 2026-10-26): the discharging battery
// cyan, its flow and the home it mostly feeds too.
assert.ok(drawn.includes('<path d="M220 240 A62 62 0 0 1 282 178 L320 178" fill="none" stroke="#4DB6AC"'),
  'battery -> home in the battery color, its bend a circle arc');
assert.ok(drawn.includes('mdi-battery-70'), 'the battery shows Home Assistant\'s level icon');
assert.ok(drawn.includes('color:#4DB6AC"></i>') && /mdi-home-lightning-bolt" style="[^"]*color:#4DB6AC/.test(drawn), 'home takes the main source color');
assert.ok(/mdi-transmission-tower-export" style="[^"]*color:#488FC2/.test(drawn), 'the supplying grid blue');
// A battery at rest is cyan too, magenta only while it charges (user
// 2026-10-26).
const resting = nodeOf();
context.applyEnergyFlowPreview(resting, {flow_entities: 'sensor.pv\nsensor.grid\nsensor.bat\nsensor.soc', key_code: 0, span_w: 2, span_h: 2},
  {values: {'sensor.pv': '0', 'sensor.grid': '200', 'sensor.bat': '0', 'sensor.soc': '78'}, units: {}, icons: {'sensor.soc': 'mdi:battery-70'}});
assert.ok(/mdi-battery-70" style="[^"]*color:#4DB6AC/.test(resting.html), 'resting battery cyan');
// Every value with its unit a step smaller after it (user 2026-10-26).
for (const [value, unit] of [['0', 'W'], ['200', 'W'], ['700', 'W'], ['500', 'W'], ['78', '%']]) {
  assert.ok(drawn.includes('>' + value + '<span class="tile-unit">' + unit + '</span></div>'), value + ' ' + unit);
}
assert.ok(!drawn.includes('Energie'), 'no header by default');
const headed = nodeOf();
context.applyEnergyFlowPreview(headed, {flow_entities: 'sensor.pv', key_code: 1, span_w: 2, span_h: 2, title: '<b>Energie</b>',
  icon_name: ''}, meta);
assert.ok(headed.html.includes('&lt;b&gt;Energie&lt;/b&gt;'), 'the header title is escaped');
assert.ok(headed.html.includes('mdi-lightning-bolt'), 'the header icon');
assert.ok(!headed.html.includes('mdi-transmission-tower'), 'a node without its entity is left out');
// A new tile without any sensor shows no node, not a lone home (user
// 2026-10-26); the home joins with the first sensor.
const empty = nodeOf();
context.applyEnergyFlowPreview(empty, {flow_entities: '', key_code: 0, span_w: 2, span_h: 2}, meta);
assert.ok(!empty.html.includes('<rect') && !empty.html.includes('mdi-') && !empty.html.includes('--'), 'empty tile: no node');
assert.ok(/mdi-home-lightning-bolt/.test(headed.html), 'the home with the first sensor');
assert.match(renderer, /const bool has_home = has_pv \|\| has_grid \|\| has_battery \|\| st->entities\[kRoleHome\]\.length\(\) > 0;\s*const bool node_shown\[kNodeCount\] = \{has_pv, has_grid, has_home, has_battery\};/,
             'the panel: no node without a sensor either');
// Every value shows its amount, the battery's too (user 2026-10-26: its
// icon shows charging; b541-b545 kept Home Assistant's minus), also with its
// direction inverted.
// While it charges its node shows Home Assistant's charging icon for its
// level; the grid's icon shows import and export; solar is solar-power.
// The grid's arrow follows the energy (user 2026-10-24): out of the mast
// while importing, into it while feeding in.
assert.ok(drawn.includes('mdi-solar-power"') && drawn.includes('mdi-transmission-tower-export"'),
          'solar-power, the grid importing: the arrow leaves the mast');
for (const key_code of [0, 8]) {
  const signed = nodeOf();
  context.applyEnergyFlowPreview(signed, {flow_entities: 'sensor.pv\nsensor.grid\nsensor.bat\nsensor.soc', key_code,
    span_w: 2, span_h: 2}, {values: {'sensor.pv': '0', 'sensor.grid': '-200', 'sensor.bat': '-0.3', 'sensor.soc': '76'},
    units: {'sensor.bat': 'kW'}, icons: {'sensor.soc': 'mdi:battery-70'}});
  const shows = amount => signed.html.includes('>' + amount + '<span class="tile-unit">W</span></div>');
  assert.ok(shows('300') && !shows('-300'), 'the battery shows its amount');
  assert.ok(shows('200') && !shows('-200'), 'the grid shows its amount');
  assert.ok(/mdi-transmission-tower-import" style="[^"]*transform:scaleX\(-1\)"/.test(signed.html),
            'the grid feeding in: import mirrored, the arrow from the lines into the mast');
  // Inverted, the same value is a discharge: Home Assistant's own icon.
  assert.ok(signed.html.includes(key_code ? 'mdi-battery-70"' : 'mdi-battery-charging-80"'), 'charging icon ' + key_code);
  // The grid taking the feed-in purple; the battery magenta while it
  // charges, cyan while it discharges.
  assert.ok(/mdi-transmission-tower-import" style="[^"]*color:#8353D1/.test(signed.html), 'feeding grid purple');
  assert.ok(new RegExp('mdi-battery-[a-z0-9-]+" style="[^"]*color:' + (key_code ? '#4DB6AC' : '#F06292')).test(signed.html),
            'battery color ' + key_code);
}
assert.match(renderer, /String charging_icon\(bool has_level, float level\) \{\s*if \(!has_level\) return "battery-charging";\s*if \(level < 10\.0f\) return "battery-charging-outline";\s*const long tens = LV_MIN\(10L, lroundf\(level \/ 10\.0f\)\) \* 10L;/,
             'the panel picks the charging icon like the preview');
assert.match(renderer, /const bool charging = in\.has_battery && -in\.battery >= kIdleWatts;/);
assert.ok(renderer.includes('show_grid_icon(st, in);'));
assert.match(renderer, /const bool mirrored = feeding && grid_feed_picture\(st\);[\s\S]*?supplying \? "transmission-tower-export" : feeding \? "transmission-tower-import" : "transmission-tower"/,
             'the panel: export while importing, import mirrored while feeding in');
assert.match(renderer, /const String glyph = getMdiChar\("transmission-tower-import"\);[\s\S]*?row\[a\] = row\[b\];/,
             'the feed-in picture is the import icon with its rows turned around');
assert.match(renderer, /if \(st->grid_feed\) lv_image_set_src\(st->grid_feed, nullptr\);\s*lv_image_cache_drop\(st->grid_feed_buf\);\s*lv_draw_buf_destroy\(st->grid_feed_buf\);/);
// The charging icons are built at runtime: they are built in, like the grid's.
const coreIcons = read('tools/icons/core-icons.txt').split('\n');
for (const name of ['battery-charging-outline', 'battery-charging-10', 'battery-charging-100', 'transmission-tower-export',
  'transmission-tower-import', 'solar-power']) assert.ok(coreIcons.includes(name), 'core icon ' + name);
assert.ok(renderer.includes('show_power(st.node[kNodeBattery].value, st.node[kNodeBattery].unit, in.has_battery, in.battery);') &&
  !renderer.includes('signed_power_text'), 'the panel shows the battery amount');
assert.match(renderer, /const float magnitude = fabsf\(watts\);/, 'show_power drops the sign');
// The values' units the shared way (value_unit.h), the charge's too.
for (const call of ['show_power(st.node[kNodePv].value, st.node[kNodePv].unit, in.has_pv, in.pv);',
  'value_unit::show(label, unit, i18n::format_number(language, magnitude / 1000.0f, 1), "kW");',
  'value_unit::show(st.charge, st.charge_unit,']) assert.ok(renderer.includes(call), call);
// User 2026-10-26 (b545): the nodes 6 px further out, the values 4 px
// closer to them; the preview takes the same sizes (web_scripts.cpp).
assert.ok(metrics.includes('m.inset_x = tile_layout::scale(18);') && metrics.includes('m.inset_y = tile_layout::scale(14);'));
assert.ok(metrics.includes('inline int value_gap() { return tile_layout::scale(8); }'));
// Every value opens its sensor like its node, the charge its own.
assert.match(renderer, /st->node\[node\]\.value = create_value\(\*st, font\);\s*make_value_tappable\(\*st, st->node\[node\]\.value, kNodeRole\[node\], node\);/);
assert.ok(renderer.includes('make_value_tappable(*st, st->charge, kRoleBatteryCharge, kNodeBattery);'));
// With the header on the flow moves down under it (user 2026-10-26), on the
// panel and in the preview alike (test-energy-flow-model.mjs: the numbers).
assert.ok(renderer.includes('st->layout = layout(width, height, metrics, energyFlowOption(tile, kFlowHeaderBit) ? header_top(metrics) : 0.0f);'));
assert.ok(admin.includes('const l = energyFlowLayout(width, height, L, (bits & 1) ? 2 * L.ring - L.insetY : 0);'));
assert.match(read('src/types/energy_flow/flow_geometry.h'), /inline float header_top\(const Metrics& m\) \{ return 2\.0f \* m\.ring - m\.inset_y; \}/);
// A node's popup shows whole numbers (user 2026-10-26: Home Assistant sends
// "9291.81" W, its card shows 9.292 W); kW and MW keep their digits.
assert.match(renderer, /String unit = init\.unit;\s*unit\.trim\(\);\s*unit\.toLowerCase\(\);\s*if \(unit != "kw" && unit != "mw" && unit != "gw"\) init\.decimals = 0;/);
// The panel colors grid and battery by direction like the preview, the
// mirrored feed-in picture included.
assert.match(renderer, /case kNodeBattery: return in\.has_battery && -in\.battery >= kIdleWatts \? kBatteryColor : kBatteryOutColor;\s*default: return in\.has_grid && -in\.grid >= kIdleWatts \? kGridReturnColor : kGridColor;/);
assert.ok(renderer.includes('color_node(st, st.node[kNodeGrid], node_color(kNodeGrid, in));') &&
  renderer.includes('color_node(st, st.node[kNodeBattery], node_color(kNodeBattery, in));'));
assert.ok(renderer.includes('lv_obj_set_style_image_recolor(st.grid_feed, lv_color_hex(tone_color::readable_icon(rgb)), 0);') &&
  renderer.includes('lv_obj_set_style_image_recolor_opa(st.grid_feed, LV_OPA_COVER, 0);'), 'the feed-in picture recolored');
assert.match(renderer, /void make_value_tappable\(FlowTileState& st, lv_obj_t\* label, uint8_t role, uint8_t node\) \{\s*if \(!label \|\| !st\.entities\[role\]\.length\(\)\) return;\s*lv_obj_add_flag\(label, LV_OBJ_FLAG_CLICKABLE\);/,
             'a value without its own sensor stays plain text');
console.log('Energy flow tile: registry, Web Admin fields and editor, runtime marks and timer, popups, translations and the real preview');
