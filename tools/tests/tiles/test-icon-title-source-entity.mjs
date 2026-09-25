// Icon-and-title tiles (Scene, Folder, Back, Camera) can take their icon color
// from a source entity stored in their icon colors record: "src auto" uses the
// entity's own tile color (light color, on/off, climate mode, cover state),
// "src rules" evaluates the color bar and state colors on its state. The
// device subscribes to the entity, recolors visible tiles on every state,
// recolors restored folders from the cache, and polls it in the screensaver.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// Record: an optional "src" line after the fixed color.
const record = read('src/tiles/config/tile_icon_colors.h');
for (const marker of [
  'enum class SourceMode : uint8_t { None, Auto, Rules };',
  'inline bool parse_source(const char* begin, const char* end, SourceMode& mode,',
  'inline SourceMode source(const char* record, const char*& entity, size_t& entity_len) {',
  'if (source_mode == SourceMode::Rules) {\n    allow_bar = true;\n    allow_rows = true;',
  'append_text(out, n, source_mode == SourceMode::Auto ? "\\nsrc auto " : "\\nsrc rules ");',
]) assert.ok(record.includes(marker), `record: ${marker}`);
assert.match(read('src/tiles/config/tile_config.h'),
  /static inline String tileIconSourceEntity\(int type, const String& record\) \{\s*if \(!tileTypeHasFixedIconColorOnly\(type\)/);

// Subscription: the folder projection carries the source entity, and the
// route rebuild adds it for folders and the screensaver; saves reload routes.
const config = read('src/tiles/config/tile_config.cpp');
assert.match(config, /if \(!tileTypeHasFixedIconColorOnly\(out\[i\]\.type\)\) continue;\s*String record;\s*out\[i\]\.sensor_entity = readIconColorsSd\(folder_id, i, record\)\s*\? tileIconSourceEntity\(out\[i\]\.type, record\)/);
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
assert.match(mqtt, /if \(tileTypeHasFixedIconColorOnly\(slot\.type\) && slot\.entity\[0\]\) \{\s*add_route\(String\(slot\.entity\), -1, "state"\);\s*continue;/);
assert.match(mqtt, /if \(tileTypeHasFixedIconColorOnly\(tile\.type\)\) \{\s*const String source = tileIconSourceEntity\(tile\.type, tile\.icon_colors\);\s*if \(source\.length\(\)\) add_route\(source, -1, "state"\);/);
assert.match(read('src/web/server/handlers/web_admin_tiles.cpp'),
  /if \(tileTypeHasFixedIconColorOnly\(tile\.type\)\) return tileIconSourceEntity\(tile\.type, tile\.icon_colors\);/);

// Dispatch: MQTT only sets bits (no allocation, no LVGL); the loop recolors.
const tabs = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
for (const marker of [
  'static std::atomic<uint64_t> g_icon_source_pending{0};',
  'return strlen(entity_id) == length && strncasecmp(source, entity_id, length) == 0;',
  'if (tile_icon_source_matches(tile, entity_id)) {',
  'g_icon_source_pending.fetch_or(icon_source_indices);',
  'uint64_t pending = g_icon_source_pending.exchange(0);',
  'tile_icon_source::refresh_card(g_tiles_objs[idx][i], config.tiles[i]);',
  'if (include_media && grid_type == GridType::TAB0 && icon_sources) {',
]) assert.ok(tabs.includes(marker), `dispatch: ${marker}`);
assert.match(read('src/tiles/runtime/tile_update_service.h'), /process_media_update_queue\(drain_all \? 0 : 2\);\s*process_icon_source_updates\(\);/);
const saver = read('src/ui/screensaver/image_screensaver.cpp');
assert.ok(saver.includes('lv_obj_t* slot_objs[TILES_PER_GRID] = {};') && saver.includes('st->slot_objs[i] = tile_obj;') &&
  saver.includes('for (lv_obj_t*& obj : st->slot_objs) obj = nullptr;'));
assert.match(saver, /if \(tileTypeHasFixedIconColorOnly\(tile\.type\)\) \{[\s\S]*?if \(source_payload == st->slot_payloads\[i\]\) continue;[\s\S]*?tile_icon_source::refresh_card\(st->slot_objs\[i\], tile\);/);

// Colors: the same functions the entity's own tiles use.
const source = read('src/tiles/runtime/tile_icon_source.cpp');
for (const marker of [
  'if (switch_domain(domain)) return switch_payload_icon_color(payload, rgb);',
  'if (domain == "climate") return climate_payload_icon_color(payload, rgb);',
  'if (domain == "cover") return cover_payload_icon_color(payload, rgb);',
  'rgb = binary_sensor_visual_color(state);',
  'return tile_icon_colors::resolve(record.c_str(), state.c_str(), display, rgb);',
  'if (record.length() && tile_icon_colors::resolve(record.c_str(), "", nullptr, fixed)) rgb = fixed;',
  'if (tile_icon_disc::is_disc(child)) return tile_icon_disc::icon_of(child);',
]) assert.ok(source.includes(marker), `tile_icon_source: ${marker}`);
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.ok(renderer.includes('const uint32_t icon_color = switch_state_icon_color(state);'), 'The Switch tile uses the shared color');
assert.match(renderer, /static uint32_t switch_state_icon_color\(const SwitchState& state\) \{\s*if \(!state\.available \|\| \(state\.has_state && !state\.is_on\)\) return 0xB0B0B0;/);
assert.ok(read('src/types/cover/renderer.cpp').includes('rgb = cover_icon_color(state);'));
for (const [file, icon] of [['src/types/navigate/renderer.cpp', 'icon_lbl'], ['src/types/scene/renderer.cpp', 'icon_lbl'],
                            ['src/types/camera/renderer.cpp', 'icon']]) {
  assert.ok(read(file).includes(`tile_icon_source::apply_initial(${icon}, tile);`), `${file} builds with the source color`);
}

// Web Admin: source select, mode buttons and translations.
const html = read('src/web/server/render/tile_icon_colors_html.cpp');
for (const marker of ['"_tile_icon_source_section\\">\\n"', '_tile_icon_source" data-icon-color="source"><option value="">)html";',
  '_tile_icon_source_mode" value="auto">', '"source-mode", "data-mode", "auto", tr.tile_icon_color_source_auto',
  '"source-mode", "data-mode", "rules", tr.tile_icon_color_source_rules', 'tr.tile_icon_color_source_hint'])
  assert.ok(html.includes(marker), `HTML: ${marker}`);
assert.ok(read('src/web/admin/tiles/registry.js').includes("rebuildEntitySelect(tab + '_tile_icon_source', iconColorSourceEntries(data));"));
const i18n = read('src/core/i18n/i18n.cpp');
for (const text of ['"Farbe von Entität"', '"Color from entity"', '"Couleur depuis une entité"', '"Eigene Regeln"',
  '"Own rules"', '"Règles propres"']) assert.ok(i18n.includes(text), `translation ${text}`);
console.log('Icon-and-title tiles follow a source entity: record, routes, dispatch, screensaver, colors and editor pass');
