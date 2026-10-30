// Vacuum tile (29, types/vacuum, issue #50): the Bridge's detail state
// (vacuum_helpers.py) is parsed defensively and keeps missing values apart
// from zero, commands go only to the Bridge's fixed actions, every text
// comes from the central translations in every language, and the type is
// wired into every list a tile type needs (registry, policy, picker, Web
// Admin, previews, popup lifecycle, sealed command leaf, map pictures).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

// Persisted ID and policies.
assert.match(read('src/types/tile_type.h'), /TILE_POWER_FLOW = 28,\n  TILE_VACUUM = 29\n/);
assert.match(read('src/types/tile_type_policy.h'), /\|\| type == TILE_VACUUM;/);
assert.match(read('src/tiles/config/tile_geometry.h'), /type == TILE_FAN \|\| type == TILE_VACUUM;/);
assert.match(read('src/ui/navigation/view_navigation.cpp'), /case TILE_VACUUM: return true;/);

// Registry: the device field set and runtime, its own renderer.
const registry = read('src/types/types_registry.cpp');
assert.match(registry, /TILE_VACUUM,\s*"Staubsauger",\s*"device",\s*"vacuum",\s*"device",\s*nullptr,\s*"loadVacuumFields",\s*"saveVacuumFields",\s*"resetVacuumFields",[\s\S]{0,60}render_vacuum_wrapper,\s*apply_device_wrapper,\s*append_vacuum_fields_wrapper,/);
assert.match(registry, /case TILE_VACUUM: return i18n::vacuum_label\(language, i18n::VacuumLabel::Type\);/);

// Bridge list, detail route, picker.
const bridgeConfig = read('src/network/bridge/ha_bridge_config.cpp');
for (const marker of ['std::make_pair("\\"vacuums\\"", &merged.vacuums_text)', '"vacuum_meta"',
                      'listEqualsIgnoringOrder(a.vacuums_text, b.vacuums_text)']) {
  assert.ok(bridgeConfig.includes(marker), `ha_bridge_config: ${marker}`);
}
assert.match(read('src/network/bridge/ha_bridge_config.h'), /bool supportsVacuums\(\) const \{ return entity_search_ >= 6; \}/);
assert.ok(read('src/network/mqtt/mqtt_handlers.cpp').includes('{&cfg.vacuums_text, "detail"}'));
const search = read('src/network/bridge/entity_search.cpp');
assert.ok(search.includes('{"vacuums", TILE_VACUUM}'));
assert.ok(search.includes('if (list == "vacuums") return haBridgeConfig.supportsVacuums();'));
const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.ok(tiles.includes('appendList("vacuums", parseSensorList(ha.vacuums_text));'));
assert.ok(tiles.includes('&ha.fans_text, &ha.vacuums_text}'), 'the previews read the vacuum detail');
const picker = read('src/web/admin/tiles/entity-picker.js');
assert.ok(picker.includes("['calendar'], ['vacuum']"), 'the picker names the vacuum domain');
assert.ok(picker.includes("'calendars', 'vacuums']"), 'the picker searches the vacuums list');

// The detail state goes to its own cache; tiles and popup follow it.
const control = read('src/types/device/device_control.cpp');
assert.match(control, /if \(entity\.startsWith\("vacuum\."\)\) \{\s*if \(!payload \|\| !vacuum_control::accept_detail\(entity, payload, length\)\) return;/);
assert.match(control, /vacuum_tiles_refresh\(entity\);\s*vacuum_popup_refresh\(entity\);/);

// Commands: only the Bridge's fixed actions, sealed when paired like Fan.
assert.match(read('src/network/secure/command_channel_core.h'), /"fan", "vacuum", "lock"/);
const vc = read('src/types/vacuum/vacuum_control.cpp');
assert.ok(vc.includes('mqttTopics.deviceBase() + "/cmnd/vacuum"'));
assert.ok(vc.includes('doc["action"] = "set_fan_speed";') && vc.includes('doc["action"] = "clean_area";'));
const actions = new Set();
for (const file of ['src/types/vacuum/vacuum_tile.cpp', 'src/ui/popups/vacuum/vacuum_popup.cpp']) {
  for (const m of read(file).matchAll(/vacuum_control::send\([^,]+, ([^;]+)\);/g)) {
    for (const a of m[1].matchAll(/"([a-z_]+)"/g)) actions.add(a[1]);
  }
}
const allowed = new Set(['start', 'pause', 'stop', 'return_to_base', 'locate', 'clean_spot']);
for (const action of actions) assert.ok(allowed.has(action), `unknown vacuum action ${action}`);
for (const action of allowed) assert.ok(actions.has(action), `${action} is reachable from the tile or popup`);
const bridgeHelpers = path.join(root, '../HomeTiles_Bridge/custom_components/tab5_lvgl/vacuum_helpers.py');
if (fs.existsSync(bridgeHelpers)) {
  const py = fs.readFileSync(bridgeHelpers, 'utf8');
  for (const action of allowed) assert.ok(py.includes(`"${action}": ("${action}"`), `the Bridge runs ${action}`);
}

// Popup lifecycle: every other popup hides it, it hides them, preload and
// the tile color follow.
for (const type of ['calendar', 'camera', 'climate', 'cover', 'energy', 'light', 'media', 'pin', 'sensor', 'weather']) {
  assert.match(read(`src/ui/popups/${type}/${type}_popup.cpp`), /hide_vacuum_popup\(\);/, `${type} hides the vacuum popup`);
}
assert.match(read('src/ui/popups/device/device_popup.cpp'), /hide_vacuum_popup\(\);/);
const popup = read('src/ui/popups/vacuum/vacuum_popup.cpp');
for (const hide of ['pin', 'light', 'climate', 'cover', 'sensor', 'weather', 'energy', 'media', 'camera', 'calendar', 'device']) {
  assert.ok(popup.includes(`hide_${hide}_popup();`), `the vacuum popup hides the ${hide} popup`);
}
assert.ok(read('src/ui/ui_manager.cpp').includes('preload_vacuum_popup();'));
assert.ok(read('src/tiles/runtime/tile_icon_source.cpp').includes('vacuum_popup_follow_tile_color(color);'));
assert.ok(popup.includes('viewNavigationPopupShown(pop.card'));
// Controls only with their feature bit; the menu tabs too.
for (const marker of ['d.features & vacuum_detail::kReturnHome', 'd.features & vacuum_detail::kStop',
                      'd.features & vacuum_detail::kFanSpeed', 'd.features & vacuum_detail::kLocate',
                      'd.features & vacuum_detail::kCleanSpot', 'd.features & vacuum_detail::kCleanArea']) {
  assert.ok(popup.includes(marker), `vacuum popup: ${marker}`);
}

// The map: a camera refreshes every 3 s while the vacuum moves, 60 s
// otherwise; the Web Admin choice (Tile::flow_entities, its sidecar) wins
// over the Bridge's suggestion and is declared as a picture.
const map = read('src/types/vacuum/vacuum_map.cpp');
// A camera map's interval is the tile's (Web Admin vacuum_every, 3..60 s,
// Tile::image_slideshow_sec; user 2026-10-30).
assert.ok(map.includes('camera ? every : 0'));
assert.ok(map.includes('if (seconds < bridge_images::kEveryMin) return bridge_images::kEveryMin;'));
const deviceWeb = read('src/types/device/web.cpp');
assert.ok(deviceWeb.includes('_vacuum_every\\" min=\\"3\\" max=\\"60\\"') && deviceWeb.includes('tile.image_slideshow_sec = static_cast<uint16_t>(every < 3 ? 3 : every > 60 ? 60 : every);'));
// One row on every size (Dock, Clean, Stop); two rows: the map above it,
// the header beside the disc; from 1.5 rows otherwise the state centred like
// the Lock/Alarm/Fan tiles, in the panel and the preview alike.
const tileCpp = read('src/types/vacuum/vacuum_tile.cpp');
assert.ok(tileCpp.includes('const bool tall = view->layout != Layout::Map && tile.span_h > 1.0f;'));
assert.ok(tileCpp.includes('if (view->layout == Layout::Row || view->layout == Layout::Map) {'));
assert.ok(tileCpp.includes('const int edge = std::min(row_top - view->gap - content_top, tile_w - 2 * inset);'));
assert.ok(read('src/web/admin/tiles/layout.js').includes("const vacuumMap = Number(type) === 29 && Number(layout?.span_w) >= 2 && Number(layout?.span_h) >= 2;"));
assert.ok(read('src/types/device/admin.js').includes('const edge = Math.max(0, Math.min(rowTop - gap - top, tileWidth - 2 * inset));'));
// User 2026-10-30 on b624: no black square before the popup's map. The
// tile fetches the popup's picture ahead while it is on screen (like the
// Media tile holds its cover) and the popup shows it in its first frame.
assert.ok(read('src/types/vacuum/vacuum_tile.cpp').includes('vacuum_map::want(view, view->card, map, static_cast<uint16_t>(vacuum_popup_map_edge(view->map_centered)),'));
assert.ok(read('src/types/vacuum/vacuum_tile.cpp').includes('vacuum_map::unwant(view);'));
assert.ok(read('src/ui/popups/vacuum/vacuum_popup.cpp').includes('if (show) vacuum_map::show_now(pop.map);'));
assert.ok(map.includes('for (const Want& wish : g_wants) add(wish.entity, wish.edge, wish.every, wish.card);'));
const config = read('src/tiles/config/tile_config.h');
assert.match(config, /static constexpr bool tileKeepsFlowEntities\(int type\) \{\s*return type == TILE_POWER_FLOW \|\| type == TILE_VACUUM;/);
assert.ok(config.includes('tile.flow_entities = map.startsWith("image.") || map.startsWith("camera.") ? map : String();'));
const configCpp = read('src/tiles/config/tile_config.cpp');
assert.ok(!configCpp.includes('tile.type != TILE_POWER_FLOW || !readFlowEntitiesSd'), 'the sidecar is read for every type that keeps it');
assert.ok(configCpp.includes('if (!tileKeepsFlowEntities(tile.type) || !readFlowEntitiesSd(folder_id, index, record)) continue;'));
assert.ok(read('src/types/device/web.cpp').includes('appendEntityPickerField(html, tab_id, "vacuum_map", i18n::vacuum_label(lang, i18n::VacuumLabel::Map), "images");'));
assert.ok(read('src/types/device/web.cpp').includes('tile.flow_entities = type == TILE_VACUUM && server.hasArg("vacuum_map") ? server.arg("vacuum_map") : String();'));
assert.ok(read('src/web/server/handlers/web_admin_tiles.cpp').includes(',\\"vacuum_map\\":\\"'), 'the tile JSON carries the map');
assert.ok(search.includes('if (!strncmp(entity, "image.", 6) || !strncmp(entity, "camera.", 7)) return "images";'));
assert.ok(read('src/types/vacuum/vacuum_tile.cpp').includes('view->map_entity.length() ? view->map_entity.c_str() : d.map'));
assert.ok(popup.includes('pop.target.map_entity.length() ? pop.target.map_entity.c_str() : d.map'));
// The menu box's shadow as pictures like the Media popup, freed on close.
for (const marker of ['lv_snapshot_take(probe, LV_COLOR_FORMAT_A8)', 'if (!pop.menu_shadow) build_menu_shadow();',
                      'free_menu_shadow(true);\n  hide_popup_shell(pop.card);']) {
  assert.ok(popup.includes(marker), `vacuum popup: ${marker}`);
}
assert.ok(!popup.includes('lv_obj_set_style_border_width(pop.box'), 'no border instead of the shadow');
// User 2026-10-30 on b621: no ring on the disc (a circle never follows the
// disc's corners); the main button is a surface in the controls' fill with
// the level bar's radius (concentric with the card) and the text color, no
// colored fill with dark text; the vacuum's blue in every reported state,
// no grey while docked; the automatic icon is Home Assistant's (as the Web
// Admin shows it); the map square has no white theme style and tells the
// Bridge its color, so transparent maps are not put on black.
const tileSource = read('src/types/vacuum/vacuum_tile.cpp');
assert.ok(!tileSource.includes('lv_arc_create'), 'no ring on the disc');
assert.match(tileSource, /tile_icon_disc::mark_surface\(box\);\s*ui_surface_style::apply_radius\(box, climate_layout::kControlRadius, 0\);/);
assert.ok(tileSource.includes('set_label_style(view->main_text, lv_color_hex(ui_theme::text()), FONT_TITLE);'));
assert.ok(tileSource.includes('haBridgeConfig.findEntityIcon(view->entity)'));
const visual = read('src/types/vacuum/vacuum_visual.h');
assert.ok(visual.includes('constexpr uint32_t kColor = 0x29B6F6;') && !visual.includes('on_accent'));
assert.ok(!/grey\(\)[^;]*;\s*\}\s*if \(vacuum_detail::is\(d, "error"\)\)/.test(visual));
assert.ok(popup.includes('filled(pop.main, c);') && popup.includes('add_text(pop.main, popup_layout::font20(), c.text'));
assert.ok(popup.includes('value.text = add_text(value.box, popup_layout::font20(), c.text, "");'), 'values in the Media times size');
assert.ok(map.includes('lv_obj_remove_style_all(box);'), 'no white default style');
// User 2026-10-30 on b622: no tinted map (black like the map's own
// background); loaded like the Media cover: the hardware decoder's steps of
// 16, an averaged copy in the square's size, the resolved surface radius;
// kept across a rebuild; icons white like Media's, only the header colored.
assert.ok(!map.includes('backdrop_of') && !popup.includes('set_fill'), 'the map is not tinted');
assert.ok(map.includes('return static_cast<uint16_t>((edge + 15) / 16 * 16);'), 'P4 fetches in steps of 16');
assert.ok(map.includes('make_media_popup_cover_dsc(dsc, view.edge)'), 'the averaged copy of the Media cover');
assert.ok(map.includes('ui_surface_style::apply_radius(box, radius, 0);') &&
  map.includes('lv_obj_get_style_radius(box, LV_PART_MAIN)'), 'the picture is rounded with the resolved radius');
assert.ok(map.includes('park(*view);') && map.includes('if (next.length()) adopt_parked(*view);'), 'a rebuilt square keeps its picture');
assert.ok(popup.includes('constexpr int kMapRadius = popup_layout::scale480(18);'), 'the Media cover radius');
assert.ok(popup.includes('vacuum_map::set_under(pop.map, pop.card_rgb);') && map.includes('return view.under_rgb >= 0 ? static_cast<uint32_t>(view.under_rgb) : color_behind(view.box);'),
  'the popup map rounds towards its card, not the black screen behind the transparent body');
assert.ok(popup.includes('add_icon(value.box, icon, c.control_icon);') && popup.includes('if (icon) add_icon(row, icon, c.secondary);'),
  'icons like the Media popup');
assert.ok(read('src/network/mqtt/mqtt_handlers.cpp').includes('device_control::refresh_all();'), 'icon updates reach the vacuum');

// Map pictures share the tile picture topics and arrivals.
assert.ok(read('src/types/camera/picture.cpp').includes('vacuum_map::collect_topics(entities, suffixes);'));
assert.ok(read('src/network/bridge/bridge_images.cpp').includes('vacuum_map::arrived(entity);'));

// Web Admin: fields, previews, import/export.
const admin = read('src/types/device/admin.js');
for (const verb of ['load', 'save', 'reset']) assert.match(admin, new RegExp(`function ${verb}VacuumFields\\(`));
assert.ok(admin.includes("(value === '29' ? 'vacuum' : '')"));
assert.ok(admin.includes("formData.append('vacuum_layout'"));
assert.ok(read('src/web/admin/tiles/import-export.js').includes("fd.append('vacuum_layout'"));
const web = read('src/types/device/web.cpp');
assert.ok(web.includes('append_switch_choice(html, tab_id, "vacuum_layout"'));
assert.ok(web.includes('type == TILE_VACUUM && server.hasArg("vacuum_layout")'));
assert.ok(web.includes('JsonObject vacuum = doc.createNestedObject("vacuum");'));
assert.ok(read('src/web/assets/admin.js').includes('function applyVacuumPreview('), 'the bundle carries the preview');

// Translations: every language has every label and state.
const header = read('src/core/i18n/i18n.h');
const labelEnum = header.match(/enum class VacuumLabel : uint8_t \{([\s\S]*?)\n\};/)[1];
const labels = labelEnum.split('\n').map(line => line.replace(/\/\/.*$/, '').trim().replace(/,$/, '')).filter(Boolean);
assert.equal(labels.at(-1), 'Count');
assert.match(header, new RegExp(`const char\\* vacuum_labels\\[${labels.length - 1}\\];`));
assert.match(header, /const char\* vacuum_states\[8\];/);
assert.match(header, /const char\* entity_kind_labels\[24\];/);
const i18n = read('src/core/i18n/i18n.cpp');
const profiles = [...i18n.matchAll(/static const LocaleProfile kLocale(\w+) = \{([\s\S]*?\})\};\n/g)];
assert.equal(profiles.length, 8, 'eight languages');
for (const [, code, body] of profiles) {
  const groups = [...body.matchAll(/\{("[^{}]*")\}/g)]
    .map(m => [...m[1].matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(g => g[1]));
  // Right before the Power flow labels (9): the vacuum labels and states.
  const at = groups.findIndex((g, i) => g.length === labels.length - 1 && groups[i + 2]?.length === 9);
  assert.ok(at > 0, `${code}: vacuum labels before the Power flow labels`);
  const tail = [groups[at], groups[at + 1]];
  assert.equal(tail[0].length, labels.length - 1, `${code}: vacuum labels`);
  assert.equal(tail[1].length, 8, `${code}: vacuum states`);
  assert.ok(tail[0][12].includes('%u'), `${code}: CleanRooms keeps its %u`);
  assert.ok(tail[0].every(text => text.length > 0) && tail[1].every(text => text.length > 0), `${code}: no empty text`);
}

// The parser on the host with the firmware's ArduinoJson.
const jsonInclude = [process.env.ARDUINOJSON_INCLUDE, path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src')]
  .filter(Boolean).find(p => fs.existsSync(path.join(p, 'ArduinoJson.h')));
const compiler = [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version']).status === 0);
if (!jsonInclude || !compiler) {
  console.log('SKIP: vacuum detail parser needs ArduinoJson and a host C++ compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/vacuum-tile');
fs.mkdirSync(out, {recursive: true});
const source = `
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include "src/types/vacuum/vacuum_detail.h"
using namespace vacuum_detail;
static Detail d;
static bool run(const char* text) {
  DynamicJsonDocument doc(6144);
  return parse(text, text ? strlen(text) : 0, doc, d);
}
int main() {
  assert(run(R"({"state":"Cleaning","supported_features":32316,"fan_speed":"Turbo","fan_speed_list":["Quiet","Turbo",5,"","this_speed_name_is_longer_than_32_bytes"],"battery":84.4,"areas":[{"id":"kitchen","name":"K\\u00fcche"},{"id":"hall"},{"name":"x"}],"map":"image.robo_map","cleaned_area":{"value":24.0,"unit":"m\\u00b2"},"cleaning_time":{"value":18,"unit":"min"}})"));
  assert(d.valid && d.exists && d.available && is(d, "cleaning"));
  assert(d.speed_count == 2 && !strcmp(d.speeds[1], "Turbo") && !strcmp(d.fan_speed, "Turbo"));
  assert(d.has_battery && d.battery == 84);
  assert(d.area_count == 2 && !strcmp(d.areas[1].id, "hall") && !strcmp(d.areas[1].name, "hall"));
  assert(!strcmp(d.map, "image.robo_map"));
  assert(d.has_cleaned_area && d.cleaned_area == 24.0f && d.has_cleaning_time && d.cleaning_minutes == 18);
  assert(main_pauses(d) && can_start(d));
  // Missing values stay missing, null state is no entity, unavailable is not available.
  assert(run(R"({"state":null,"supported_features":0,"battery":null,"cleaned_area":null,"cleaning_time":null,"map":null,"areas":[],"fan_speed_list":[]})"));
  assert(d.valid && !d.exists && !d.available && !d.has_battery && !d.has_cleaned_area && !d.has_cleaning_time);
  assert(d.map[0] == 0 && d.area_count == 0 && d.speed_count == 0 && !main_pauses(d));
  assert(run(R"({"state":"unavailable","supported_features":8192})") && d.exists && !d.available);
  // A map must be a picture entity; numbers are clamped; a cleaning vacuum without pause shows Clean.
  assert(run(R"({"state":"cleaning","supported_features":8192,"map":"sensor.robo","battery":140})"));
  assert(d.map[0] == 0 && d.battery == 100 && !main_pauses(d));
  // Sixteen areas at most.
  std::string many = R"({"state":"docked","areas":[)";
  for (int i = 0; i < 20; ++i) many += (i ? "," : "") + std::string(R"({"id":"room_)") + std::to_string(i) + R"("})";
  many += "]}";
  assert(run(many.c_str()) && d.area_count == kMaxAreas);
  // Malformed payloads are ignored.
  assert(!run("docked") && !d.valid && !run("[]") && !run(R"({"state":5})") && !run(nullptr) && !run(""));
  return 0;
}
`;
const file = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(file, source);
const compile = spawnSync(compiler, ['-std=c++17', '-Wall', '-I', root, '-I', jsonInclude, file, '-o', binary],
                          {encoding: 'utf8'});
assert.equal(compile.status, 0, `vacuum detail parser did not compile:\n${compile.stdout}${compile.stderr}`);
const run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `vacuum detail parser contract failed:\n${run.stdout}${run.stderr}`);
console.log('Vacuum tile: detail parser, fixed commands, translations and wiring');
