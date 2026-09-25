// Rules for every tile type (the "src" line of tile_icon_colors.h): the
// tile's own entity or another one, entity color or own rules, coloring the
// icon (forced over the type's state color) and/or tinting the tile with a
// readable background. The device subscribes to another entity, reapplies
// the rules on each state, after folder switches and in the screensaver; the
// Web Admin edits them in the "Rules" section and previews the same colors.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// Record: "src <auto|rules> <self|entity> [tile=NN] [noicon] [off]".
const record = read('src/tiles/config/tile_icon_colors.h');
for (const marker of [
  'enum class SourceMode : uint8_t { None, Auto, Rules };',
  'inline bool parse_source_line(const char* begin, const char* end, Source& out) {',
  'inline Source source_of(const char* record) {',
  'inline bool own_state_colors_icon(const char* record) {',
  'if (layer.self && !allow_self) layer = Source{};',
  'if (!layer.self || (!allow_bar && !allow_rows)) {',
  '!(layer.mode == SourceMode::Rules && layer.self && layer.icon &&',
  'if (!fixed_fallback) return false;',
  'inline constexpr uint8_t kTintMinimum = 10;',
  'inline constexpr uint8_t kTintMaximum = 50;',
]) assert.ok(record.includes(marker), `record: ${marker}`);
const policy = read('src/types/tile_type_policy.h');
assert.match(policy, /static constexpr bool tileTypeRulesUseOwnEntity\(int type\) \{\s*return type == TILE_SENSOR \|\| type == TILE_SWITCH \|\| type == TILE_WEATHER \|\|/);
assert.match(read('src/tiles/config/tile_config.h'),
  /tileTypeIconColorsByState\(type\),\s*true, tileTypeRulesUseOwnEntity\(type\)\);/);

// Tint: mixed with the rule color, darkened until white text reads 4.5:1.
const tint = read('src/tiles/config/tile_tint.h');
assert.ok(tint.includes('for (int i = 0; i < 40 && white_contrast(out) < 4.5; ++i) out = scale(out, 95);'));
assert.ok(tint.includes('inline double white_contrast(uint32_t rgb) { return 1.05 / (luminance(rgb) + 0.05); }'));

// Subscription: every slot carries its rules' other entity.
const config = read('src/tiles/config/tile_config.cpp');
assert.ok(config.includes('if (readIconColorsSd(folder_id, i, record)) out[i].rule_entity = tileIconSourceEntity(out[i].type, record);'));
assert.ok(config.includes('e->rule_entities[i] = psramStrdupLocal(slots[i].rule_entity);') &&
  config.includes('out[i].rule_entity = e->rule_entities[i] ? e->rule_entities[i] : "";'));
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
assert.ok(mqtt.includes('if (slot.rule_entity[0]) add_route(String(slot.rule_entity), -1, "state");'));
assert.match(mqtt, /const String rule_entity = tileIconSourceEntity\(tile\.type, tile\.icon_colors\);\s*if \(rule_entity\.length\(\)\) add_route\(rule_entity, -1, "state"\);/);
assert.ok(read('src/web/server/handlers/web_admin_tiles.cpp').includes('if (rule_entity.length()) entity += "|" + rule_entity;'));

// Dispatch: bits only in MQTT, rules reapplied on the loop task.
const tabs = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
for (const marker of [
  'const tile_icon_colors::Source layer = tile_icon_colors::source_of(tile.icon_colors.c_str());',
  'if (layer.self) return tile.sensor_entity.equalsIgnoreCase(entity_id);',
  'return strlen(entity_id) == layer.entity_len && strncasecmp(layer.entity, entity_id, layer.entity_len) == 0;',
  'g_icon_source_pending.fetch_or(icon_source_indices);',
  'tile_icon_source::refresh_card(g_tiles_objs[idx][i], config.tiles[i]);',
  'void tiles_request_rule_refresh(GridType grid_type, uint8_t index) {',
]) assert.ok(tabs.includes(marker), `dispatch: ${marker}`);
assert.ok(read('src/types/value/value_control.cpp').includes('tiles_request_rule_refresh(grid, index);'), 'Editable values reapply rules');
assert.match(read('src/tiles/runtime/tile_renderer.cpp'),
  /tile_icon_source::refresh_card\(tile_obj, tile\);\s*tile_icon_disc::apply_tile_options\(tile_obj, tile\.icon_disc_mode, tile\.icon_glow\);/);
const saver = read('src/ui/screensaver/image_screensaver.cpp');
assert.ok(saver.includes('const String rule_entity = tile_icon_source::rule_entity(tile);') &&
  saver.includes('if (rule_payload != st->slot_rule_payloads[i]) {'));

// Colors, forced icon and tint.
const source = read('src/tiles/runtime/tile_icon_source.cpp');
for (const marker of [
  'if (switch_domain(domain)) return switch_payload_icon_color(payload, rgb);',
  'if (domain == "climate") return climate_payload_icon_color(payload, rgb);',
  'if (domain == "cover") return cover_payload_icon_color(payload, rgb);',
  'return tile_icon_colors::resolve(record.c_str(), state.c_str(), display, rgb, false);',
  'tile_icon_disc::force_icon_color(icon, lv_color_hex(rgb));',
  'tile_icon_disc::force_icon_color(icon, lv_color_hex(fixed));',
  'tile_icon_disc::release_icon_color(icon);',
  'set_tile_tint(card, rgb, layer.tile);',
  'const uint32_t tint = tile_tint::background(base, color, percent);',
]) assert.ok(source.includes(marker), `tile_icon_source: ${marker}`);
const disc = read('src/tiles/runtime/tile_icon_disc.h');
assert.match(disc, /if \(forced_color\(icon, forced\)\) \{\s*lv_obj_set_style_text_color\(icon, color, kIconRequested\);\s*color = forced;/,
  'Type state colors wait behind a forced rule color');
assert.ok(read('src/tiles/runtime/tile_icon_color_rules.h').includes('const bool own_rules = tile_icon_colors::own_state_colors_icon(record);'));

// Web Admin: Rules section for every type with an icon or surface.
const html = read('src/web/server/render/tile_icon_colors_html.cpp');
for (const marker of ['appendHtmlEscaped(html, tr.tile_rules);', '_tile_icon_rules_on" value="0">',
  '"rules-on", "data-mode", "1", tr.icon_disc_on', '"source-kind", "data-mode", "self", tr.tile_rules_own_entity',
  '"source-kind", "data-mode", "other", tr.tile_rules_other_entity', '_tile_icon_rule_icon" data-icon-color="rule-target" checked> ',
  '_tile_icon_rule_tile" data-icon-color="rule-target"> ', 'html += String(tile_icon_colors::kTintMinimum);',
  'data-icon-color="strength-reset"'])
  assert.ok(html.includes(marker), `HTML: ${marker}`);
for (const [file, name] of [['src/types/switch/admin.js', 'Switch'], ['src/types/cover/admin.js', 'Cover'],
  ['src/types/media/admin.js', 'Media'], ['src/types/weather/admin.js', 'Weather'], ['src/types/clock/admin.js', 'Clock'],
  ['src/types/text/admin.js', 'Text'], ['src/types/climate/admin-editor.js', 'Climate']]) {
  assert.ok(read(file).includes(`function load${name}Fields(tab, data) {\n    loadIconColorFields(tab, data);`), `${name} loads its rules`);
}
assert.ok(read('src/web/admin/tiles/icon-colors.js').includes("} else if (role === 'strength-reset') {") &&
  read('src/web/admin/tiles/icon-colors.js').includes("if (strength) strength.value = '20';"), 'Strength reset returns to 20 %');
const i18n = read('src/core/i18n/i18n.cpp');
for (const text of ['"Regeln"', '"Rules"', '"Règles"', '"Eigene Entität"', '"Other entity"', '"Kachel tönen"', '"Tint tile"', '"Intensité"'])
  assert.ok(i18n.includes(text), `translation ${text}`);
console.log('Rules for every tile: record, tint, routes, dispatch, forced icon, screensaver and editor pass');
