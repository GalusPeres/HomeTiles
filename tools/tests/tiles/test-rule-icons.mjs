// An icon per rule (mockup build/design-mockups/rule-icons, user 2026-10-28):
// a stop's or state line's color may carry an MDI icon name
// ("RRGGBB,<icon>", tile_icon_colors.h) that replaces the tile's own icon
// while the stop or line applies. Covers the record (firmware header against
// the Web Admin module, identical records and icons), the label logic that
// brings the tile's own icon back (tile_rule_icon.h on shimmed LVGL), the
// device wiring (own rules, rule layers, renderers that change their icon,
// popup header), the Web Admin rows, picker and previews, and the
// translations.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const code = source => source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');

// ---------------------------------------------------------------------------
// Record model in the Web Admin module.
const js = vm.createContext({document: {addEventListener() {}, getElementById: () => null},
  TextEncoder, TextDecoder, Number, Math, String, Array});
vm.runInContext(read('src/web/admin/tiles/icon-colors.js'), js);

const normalizeCases = [
  // Icons on stops and state lines survive, colors become uppercase.
  ['v2\n\nbar steps 0 100 0:e53935,battery-alert 200:fb8c00,battery-30 800:43a047', true, true,
    'v2\n\nbar steps 0 100 0:E53935,battery-alert 200:FB8C00,battery-30 800:43A047'],
  ['v2\n\nis+ 22c55e,washing-machine läuft\nhas e53935,washing-machine-alert fehler\nis 9e9e9e aus', false, true,
    'v2\n\nis+ 22C55E,washing-machine läuft\nhas E53935,washing-machine-alert fehler\nis 9E9E9E aus'],
  // An invalid icon drops its stop or line like an invalid color.
  ['v2\n\nis 22C55E,Bad_Icon x\nis 22C55E, y\nis 22C55E,ok z', false, true, 'v2\n\nis 22C55E,ok z'],
  ['v2\n\nbar smooth 0 1 0:000000,a 500:111111,B 1000:222222,' + 'x'.repeat(49), true, true, ''],
  ['v2\n\nbar smooth 0 1 0:000000,a 1000:222222,' + 'x'.repeat(48), true, true,
    'v2\n\nbar smooth 0 1 0:000000,a 1000:222222,' + 'x'.repeat(48)],
  // Records without icons stay exactly as before.
  ['v2\nFF8800\nbar smooth 0 100 0:3B82F6 1000:EF4444\nis 00FF00 on', true, true,
    'v2\nFF8800\nbar smooth 0 100 0:3B82F6 1000:EF4444\nis 00FF00 on'],
];
for (const [input, bar, rows, expected] of normalizeCases) {
  assert.equal(js.normalizeIconColorRecord(input, bar, rows), expected, `JS normalize ${JSON.stringify(input)}`);
}
// Own-entity rules of the types that do not evaluate their own state
// (Switch, Cover, Climate, Media, ...: no bar, no rows of their own) keep
// their "src rules self" line for the rule layer; records before b575 that
// lost it get it back. The Sensor family keeps it implicit (issue #102).
// [input, allowBar, allowRows, allowSelf, expected]
const selfCases = [
  ['v2\n\nsrc rules self\nis FFC107,door-open on\nis 9E9E9E,door off', false, false, true,
    'v2\n\nsrc rules self\nis FFC107,door-open on\nis 9E9E9E,door off'],
  ['v2\n\nis FFC107,door-open on\nbar steps 0 10 0:000000 1000:FFFFFF', false, false, true,
    'v2\n\nsrc rules self\nbar steps 0 10 0:000000 1000:FFFFFF\nis FFC107,door-open on'],
  ['v2\nFF0000', false, false, true, 'v2\nFF0000'],
  ['v2\n\nsrc rules self\nhas F44336,trash-can 6', false, true, true, 'v2\n\nhas F44336,trash-can 6'],
  ['v2\n\nis FFC107 on', true, true, true, 'v2\n\nis FFC107 on'],
  ['v2\n\nis FFC107 on', false, false, false, ''],
];
for (const [input, bar, rows, self, expected] of selfCases) {
  assert.equal(js.normalizeIconColorRecord(input, bar, rows, true, self), expected, `JS own rules ${JSON.stringify(input)}`);
}

// Evaluation: [record, state, display] -> color and icon.
const battery = 'v2\n\nbar steps 0 100 0:E53935,battery-alert 200:FB8C00,battery-30 500:FDD835 800:43A047,battery';
const smooth = 'v2\n\nbar smooth 0 100 0:000000,thermometer-low 500:808080 1000:FFFFFF,thermometer-high';
const washer = 'v2\n\nis+ 22C55E,washing-machine läuft\nhas E53935,washing-machine-alert fehler\nis 9E9E9E aus';
const resolveCases = [
  [battery, '-4', null], [battery, '10', null], [battery, '20', null], [battery, '45', null], [battery, '60', null],
  [battery, '95', null], [battery, '130', null], [battery, 'unknown', null],
  [smooth, '0', null], [smooth, '49', null], [smooth, '75', null], [smooth, '100', null],
  [washer, 'läuft', null], [washer, 'Fehler E21', null], [washer, 'aus', null], [washer, 'pausiert', null],
  ['v2\n\nis FFC107,window-open on\nis 9E9E9E,window-closed off', 'on', 'Offen'],
  ['v2\nABCDEF\nis FFC107,window-open on', 'off', null],
];
const previews = resolveCases.map(([record, state, display]) => {
  const out = {};
  const color = js.resolveIconColorRecord(record, state, display, true, out) || '-';
  return color + ' ' + (out.icon || '-');
});
assert.deepEqual(previews.slice(0, 8), ['#E53935 battery-alert', '#E53935 battery-alert', '#FB8C00 battery-30',
  '#FB8C00 battery-30', '#FDD835 -', '#43A047 battery', '#43A047 battery', '- -'],
  'Steps: the icon of the last stop at or below the value; a stop without an icon shows the tile\'s own');
assert.deepEqual(previews.slice(8, 12), ['#000000 thermometer-low', '#7E7E7E thermometer-low', '#C0C0C0 -',
  '#FFFFFF thermometer-high'], 'Smooth: colors blend, the icon switches at its stop');
assert.deepEqual(previews.slice(12), ['#22C55E washing-machine', '#E53935 washing-machine-alert', '#9E9E9E -', '- -',
  '#FFC107 window-open', '#ABCDEF -'], 'State lines: the matching line\'s icon; the fixed color has none');

// ---------------------------------------------------------------------------
// The firmware gives the same records and icons.
const escape = text => JSON.stringify(text);
const native = compileAndRun({
  label: 'Rule icon records',
  harness: `#include <stdio.h>
#include "src/tiles/config/tile_icon_colors.h"
using namespace tile_icon_colors;
int main() {
  static const struct { const char* in; bool bar; bool rows; } normalize_cases[] = {
${normalizeCases.map(([input, bar, rows]) => `    {${escape(input)}, ${bar}, ${rows}},`).join('\n')}
  };
  char out[kMaxRecordBytes + 1];
  for (const auto& c : normalize_cases) {
    normalize(c.in, out, sizeof(out), c.bar, c.rows);
    printf("N %s\\x1f\\n", out);
  }
  static const struct { const char* in; bool bar; bool rows; bool self; } self_cases[] = {
${selfCases.map(([input, bar, rows, self]) => `    {${escape(input)}, ${bar}, ${rows}, ${self}},`).join('\n')}
  };
  for (const auto& c : self_cases) {
    normalize(c.in, out, sizeof(out), c.bar, c.rows, true, c.self, false);
    printf("S %s\\x1f\\n", out);
  }
  // A Switch's own layer subscribes to nothing new (its own entity).
  {
    const char* entity = nullptr;
    size_t length = 0;
    printf("SUB %d\\n", source("v2\\n\\nsrc rules self\\nis FFC107,door-open on", entity, length) == SourceMode::None ? 1 : 0);
  }
  static const struct { const char* record; const char* state; const char* display; } resolve_cases[] = {
${resolveCases.map(([record, state, display]) => `    {${escape(record)}, ${escape(state)}, ${display === null ? 'nullptr' : escape(display)}},`).join('\n')}
  };
  for (const auto& c : resolve_cases) {
    uint32_t rgb = 0;
    Icon icon;
    const bool hit = resolve(c.record, c.state, c.display, rgb, true, nullptr, &icon);
    if (hit) printf("R #%06X ", static_cast<unsigned>(rgb)); else printf("R - ");
    if (icon.length) printf("%.*s\\n", static_cast<int>(icon.length), icon.name); else printf("-\\n");
  }
  // The longest record fits: six stops and six lines with the longest icons.
  char big[4096];
  size_t n = 0;
  n += snprintf(big + n, sizeof(big) - n, "v2\\nFFFFFF\\nfill 20\\nsrc rules %s tile=50 noicon off\\nbar smooth -12345678901 12345678901",
                "sensor.${'a'.repeat(120)}");
  for (int i = 0; i < 6; ++i) n += snprintf(big + n, sizeof(big) - n, " %d:ABCDEF,${'x'.repeat(48)}", i * 200);
  for (int i = 0; i < 6; ++i) n += snprintf(big + n, sizeof(big) - n, "\\nhas+ ABCDEF,${'y'.repeat(48)} ${'z'.repeat(32)}");
  const size_t length = normalize(big, out, sizeof(out), true, true, true, false, false);
  printf("BIG %zu %d\\n", length, length > 0 && length <= kMaxRecordBytes ? 1 : 0);
  // Own rules only: a layer on another entity or switched off gives none.
  printf("OWN %s ", state_icon("v2\\n\\nis 111111,check on", true, "on", nullptr).length ? "icon" : "none");
  printf("%s ", state_icon("v2\\n\\nsrc rules sensor.other\\nis 111111,check on", true, "on", nullptr).length ? "icon" : "none");
  printf("%s ", state_icon("v2\\n\\nsrc rules self off\\nis 111111,check on", true, "on", nullptr).length ? "icon" : "none");
  printf("%s\\n", state_icon("v2\\n\\nis 111111,check on", false, "on", nullptr).length ? "icon" : "none");
  return 0;
}
`,
});
if (native !== null) {
  const lines = native.split('\n');
  const records = native.split('\x1f\n').filter(part => part.includes('N ')).map(part => part.slice(part.indexOf('N ') + 2));
  assert.deepEqual(records.slice(0, normalizeCases.length), normalizeCases.map(([, , , expected]) => expected),
    'Firmware and Web Admin normalize icons alike');
  const selfRecords = native.split('\x1f\n').filter(part => part.includes('S ')).map(part => part.slice(part.indexOf('S ') + 2));
  assert.deepEqual(selfRecords.slice(0, selfCases.length), selfCases.map(([, , , , expected]) => expected),
    'Firmware and Web Admin keep own-entity rules of Switch and co. alike');
  assert.ok(native.includes('SUB 1'), 'Own-entity layers subscribe to no other entity');
  assert.deepEqual(lines.filter(line => line.startsWith('R ')).map(line => line.slice(2)), previews,
    'Firmware and Web Admin resolve the same colors and icons');
  assert.ok(lines.includes('BIG ' + lines.find(line => line.startsWith('BIG ')).split(' ')[1] + ' 1'),
    'The longest record with icons fits kMaxRecordBytes');
  assert.ok(lines.includes('OWN icon none none none'), 'Own-state icons follow own_state_colors_icon and known states');
}

// ---------------------------------------------------------------------------
// The label logic on shimmed LVGL: the rule's icon replaces the tile's own,
// which comes back when no rule with an icon applies, also after a renderer
// changed it meanwhile.
const shimLvgl = `#pragma once
#include <stdint.h>
#include <string>
#include <map>
typedef uint32_t lv_style_selector_t;
typedef int lv_style_prop_t;
typedef int lv_style_res_t;
struct lv_color_t { uint32_t rgb; };
union lv_style_value_t { int32_t num; lv_color_t color; lv_style_value_t() : num(0) {} };
struct lv_obj_t { std::string text; std::map<long long, lv_style_value_t> props; int sets = 0; };
#define LV_PART_MAIN 0u
#define LV_STATE_USER_2 0x2000u
#define LV_STATE_USER_3 0x4000u
#define LV_STATE_USER_4 0x8000u
#define LV_STYLE_BG_COLOR 28
#define LV_STYLE_BORDER_COLOR 40
#define LV_STYLE_RES_NOT_FOUND 0
#define LV_STYLE_RES_FOUND 1
inline lv_color_t lv_color_hex(uint32_t rgb) { return lv_color_t{rgb & 0xFFFFFF}; }
inline uint32_t lv_color_to_u32(lv_color_t c) { return 0xFF000000u | c.rgb; }
inline long long lv_key(lv_style_prop_t prop, lv_style_selector_t sel) { return (static_cast<long long>(sel) << 16) | prop; }
inline lv_style_res_t lv_obj_get_local_style_prop(lv_obj_t* o, lv_style_prop_t p, lv_style_value_t* v, lv_style_selector_t s) {
  auto it = o->props.find(lv_key(p, s));
  if (it == o->props.end()) return LV_STYLE_RES_NOT_FOUND;
  *v = it->second;
  return LV_STYLE_RES_FOUND;
}
inline void lv_obj_set_local_style_prop(lv_obj_t* o, lv_style_prop_t p, lv_style_value_t v, lv_style_selector_t s) { o->props[lv_key(p, s)] = v; }
inline bool lv_obj_remove_local_style_prop(lv_obj_t* o, lv_style_prop_t p, lv_style_selector_t s) { return o->props.erase(lv_key(p, s)) > 0; }
inline const char* lv_label_get_text(lv_obj_t* o) { return o->text.c_str(); }
inline void lv_label_set_text(lv_obj_t* o, const char* t) { o->text = t; ++o->sets; }
`;
const shimArduino = `#pragma once
#include <string>
class String {
 public:
  String(const char* text = "") : value_(text) {}
  const char* c_str() const { return value_.c_str(); }
  bool operator==(const char* other) const { return value_ == other; }
 private:
  std::string value_;
};
`;
const shimMdi = `#pragma once
#include <Arduino.h>
inline uint32_t getMdiCodepoint(const String& name) {
  if (name == "check") return 0xF012C;
  if (name == "battery") return 0xF0079;
  return 0xF02D6;
}
`;
const glyph = native === null ? null : compileAndRun({
  label: 'Rule icon labels',
  files: {'lvgl.h': shimLvgl, 'Arduino.h': shimArduino, 'src/tiles/icons/mdi_icons.h': shimMdi},
  harness: `#include <stdio.h>
#include "src/tiles/runtime/tile_rule_icon.h"
static void say(const char* step, lv_obj_t& label) {
  uint32_t rule = 0;
  const bool shown = tile_rule_icon::shown(&label, rule);
  printf("%s %X %s %d\\n", step, static_cast<unsigned>(tile_rule_icon::codepoint_of(label.text.c_str())),
         shown ? "rule" : "own", label.sets);
}
int main() {
  char own[5], other[5], third[5];
  tile_rule_icon::encode(0xF02DC, own);
  tile_rule_icon::encode(0xF0335, other);
  tile_rule_icon::encode(0xF0444, third);
  lv_obj_t label;
  label.text = own;
  tile_icon_colors::Icon check{"check", 5};
  tile_rule_icon::show(&label, check); say("show", label);
  tile_rule_icon::show(&label, check); say("again", label);
  tile_rule_icon::set_glyph(&label, other); say("renderer", label);
  tile_rule_icon::show(&label, tile_icon_colors::Icon{}); say("end", label);
  tile_rule_icon::show(&label, tile_icon_colors::Icon{}); say("idle", label);
  tile_rule_icon::set_glyph(&label, other); say("same", label);
  tile_rule_icon::show_codepoint(&label, 0xF0079); say("code", label);
  label.text = third; say("past", label);
  tile_rule_icon::show_codepoint(&label, 0xF0079); say("back", label);
  tile_rule_icon::show_codepoint(&label, 0); say("own", label);
  lv_obj_t hidden;
  tile_rule_icon::show(&hidden, check); say("hidden", hidden);
  tile_rule_icon::show(&label, check);
  tile_rule_icon::forget(&label); say("forget", label);
  return 0;
}
`,
});
if (glyph !== null) {
  assert.equal(glyph.trim(), [
    'show F012C rule 1', 'again F012C rule 1', 'renderer F012C rule 1', 'end F0335 own 2', 'idle F0335 own 2',
    'same F0335 own 2', 'code F0079 rule 3', 'past F0444 rule 3', 'back F0079 rule 4', 'own F0444 own 5',
    'hidden 0 own 0', 'forget F012C own 6'].join('\n'),
    'A rule icon shows once, a renderer icon waits behind it, the own icon comes back; empty labels stay');
}

// ---------------------------------------------------------------------------
// Device wiring.
const header = read('src/tiles/config/tile_icon_colors.h');
assert.ok(header.includes('inline constexpr size_t kMaxIconBytes = 48;') &&
  header.includes('if (icon) *icon = bar_icon(bar, number);') && header.includes('if (icon) *icon = rule.icon;'),
  'resolve() hands out the matching stop\'s or line\'s icon');
const rules = code(read('src/tiles/runtime/tile_icon_color_rules.h'));
assert.match(rules, /inline void apply\(lv_obj_t\* icon, const char\* record, bool known,\s*const char\* state, const char\* display, lv_color_t fallback\) \{\s*if \(!icon\) return;\s*apply_icon\(icon, record, known, state, display\);/,
  'Every own-state color update also sets the rule icon');
assert.match(rules, /if \(!icon \|\| !record \|\| !tile_icon_colors::own_state_colors_icon\(record\)\) return;\s*tile_rule_icon::show\(icon, tile_icon_colors::state_icon\(record, known, state, display\)\);/,
  'Own rules only; a layer leaves the icon to refresh_card');
const source = code(read('src/tiles/runtime/tile_icon_source.cpp'));
assert.match(source, /rule_color\(tile, rgb, &row_fill, &rule_glyph\)/);
assert.match(source, /if \(layer\.mode != tile_icon_colors::SourceMode::None && tile\.type != TILE_WEATHER\) \{\s*tile_rule_icon::show\(icon, rule_icon \? rule_glyph : tile_icon_colors::Icon\{\}\);/,
  'A rule layer shows its matching icon (weather keeps its layered glyphs)');
const setGlyph = [
  ['src/ui/tabs/tiles/tab_tiles_unified.cpp', 'tile_rule_icon::set_glyph(icon_lbl, iconChar.c_str());'],
  ['src/types/binary_sensor/renderer.cpp', 'tile_rule_icon::set_glyph(widgets.icon_label, getMdiChar(icon).c_str());'],
  ['src/tiles/runtime/tile_renderer.cpp', 'tile_rule_icon::set_glyph(widget.icon_label, icon.c_str());'],
  ['src/tiles/runtime/tile_renderer.cpp', 'if (icon.length()) tile_rule_icon::set_glyph(widgets.icon_label, icon.c_str());'],
  ['src/types/cover/renderer.cpp', 'tile_rule_icon::set_glyph(widget.icon_label, getMdiChar(fallback_icon(state)).c_str());'],
  ['src/types/device/device_tile.cpp', 'tile_rule_icon::set_glyph(view->icon, getMdiChar(v.icon).c_str());'],
];
for (const [file, call] of setGlyph) assert.ok(read(file).includes(call), `${file}: its runtime icon keeps a rule icon`);
assert.ok(code(read('src/types/binary_sensor/renderer.cpp')).includes(
  'tile_icon_color_rules::apply_icon(widgets.icon_label, tile.icon_colors.c_str(), rule_state_known(state),'),
  'The first Binary render shows the matching rule icon');
const popup = code(read('src/ui/popups/sensor/sensor_popup.cpp'));
assert.match(popup, /if \(ctx->forced_icon\) \{\s*tile_rule_icon::show_codepoint\(ctx->icon_label, ctx->forced_icon_glyph\);\s*\} else if \(tile_icon_colors::own_state_colors_icon\(ctx->icon_colors\.c_str\(\)\)\) \{\s*tile_rule_icon::show\(ctx->icon_label, tile_icon_colors::state_icon\(/,
  'The popup header shows the tile\'s rule icon');
assert.ok(popup.includes('tile_rule_icon::forget(ctx->icon_label);') &&
  popup.includes('tile_rule_icon::set_glyph(ctx->icon_label, icon.c_str());'), 'The reused header forgets the last rule icon');
for (const file of ['src/types/sensor/renderer.cpp', 'src/types/binary_sensor/renderer.cpp']) {
  assert.ok(code(read(file)).includes('tile_rule_icon::shown(tile_icon, init.forced_icon_glyph);'), `${file}: passes the layer's icon`);
}
assert.ok(read('src/ui/popups/binary_sensor/binary_sensor_popup.cpp').includes(
  'sensor_init.forced_icon_glyph = init.forced_icon_glyph;'));

// ---------------------------------------------------------------------------
// Web Admin: rows, picker, previews and translations.
const html = read('src/web/server/render/tile_icon_colors_html.cpp');
const row = html.slice(html.indexOf('_tile_icon_rule_" + String(row);'), html.indexOf('_tile_icon_rule_add'));
const order = ['data-icon-color="value"', '_color" value="#22C55E" data-icon-color="rule-color">',
  'append_rule_icon(html, id, "rule-icon", tr.tile_group_icon);', 'data-icon-color="remove"', 'tile-icon-rule-options',
  '_has" data-icon-color="has">', '_fill" data-icon-color="row-fill">'];
order.reduce((at, marker) => {
  const next = row.indexOf(marker);
  assert.ok(next > at, `state line order: ${marker}`);
  return next;
}, -1);
assert.ok(html.includes('append_rule_icon(html, tab_id + "_tile_icon_" + row.state, "binary-icon", tr.tile_group_icon);') &&
  html.includes('_tile_icon_stop_icon" value="" data-icon-color="stop-icon">') &&
  html.includes('class="tile-rule-icon none" id=")html";') && html.includes('_icon_button" data-icon-input=")html";'),
  'On/Off and the stops have their icon too');
const head = read('src/web/admin/tiles/tile-head.js');
assert.ok(head.includes("const ruleButton = target?.closest?.('button[data-icon-input]');") &&
  head.includes("t(open.rule ? 'iconPickerRuleHint' : 'iconPickerAutoHint')") &&
  head.includes('const none = open.rule ||'), 'The tile head\'s picker sets a rule\'s icon, Automatic keeps the tile\'s own');
const colors = read('src/web/admin/tiles/icon-colors.js');
assert.ok(colors.includes("button.dataset.iconInput = tab + '_tile_icon_stop_icon';") &&
  colors.includes("stop.icon = iconColorValidIcon(target.value) ? target.value : '';") &&
  colors.includes("iconColorIconSuffix(row.icon) + ' ' + value);"), 'The editor stores the icons');
for (const file of ['src/web/admin/tiles/grid-preview.js', 'src/web/admin/tiles/live-preview.js']) {
  assert.ok(read(file).includes("if (ruleFill.icon && previewKind !== 'weather' && !lockIsIcon) iconName = ruleFill.icon;"),
    `${file}: the preview shows the rule icon`);
}
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('grid-template-columns:minmax(0,1fr) 44px 38px 38px;') && css.includes('.tile-icon-rule-options { grid-column:1;') &&
  css.includes('.tile-color-row.tile-color-row-icon { grid-template-columns:minmax(72px,1fr) 38px 38px; }'),
  'State line: state, color, icon, remove; options below the state');
assert.ok(read('src/web/assets/admin.js').includes("const ruleButton = target?.closest?.('button[data-icon-input]');"),
  'admin.js carries the picker');
const i18n = read('src/core/i18n/i18n.cpp');
assert.ok(read('src/core/i18n/i18n.h').includes('const char* icon_picker_labels[6];'));
for (const hint of ['"Das Icon der Kachel bleibt"', '"The tile\'s own icon stays"', '"L’icône de la tuile reste"',
  '"Zostaje ikona kafelka"', '"Zůstane ikona dlaždice"', '"Se mantiene el icono del mosaico"', '"Zostane ikona dlaždice"']) {
  assert.ok(i18n.includes(hint), `rule picker hint ${hint}`);
}
assert.ok(read('src/web/server/render/web_admin_scripts.cpp').includes('"iconPickerRuleHint"'));
console.log('Rule icons OK');
