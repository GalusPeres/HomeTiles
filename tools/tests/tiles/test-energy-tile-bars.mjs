// The Energy tile's bars (user 2026-10-29, mockup build/design-mockups/
// energy-tile-bars): a choice per tile (Anzeige-Modus Wert / Graph, stored in
// sensor_display_mode 2), on whole-height tiles outside the screensaver.
// Today's hours below the Switch header like the Sensor graph tile, in the
// circle's tone with the value's edge in the icon color, the running hour
// all icon color, a zero line where the range puts it. The bars keep the
// box's outer inset on every side: the first and last hour never reach the
// rounded corners and stand as far from the tile's edge at the sides as at
// the bottom.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const read = rel => fs.readFileSync(path.join(root, rel), 'utf8').replace(/\r\n/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

// ---- The tile ---------------------------------------------------------------
const renderer = read('src/types/energy/renderer.cpp');
const render = fn(renderer, 'render_energy_tile');
assert.ok(render.includes('const bool bars_shown = tile.sensor_display_mode == kEnergyBarsMode && tile.span_h >= 1 && !compact &&') &&
  render.includes('grid_type != GridType::SCREENSAVER;'), 'whole-height tiles outside the screensaver');
assert.ok(render.includes('head.title = title_text;') &&
  render.includes('tile_header::create(card, head, false, tile_graph_box::box(tile).y1);'),
  'the Switch header beside the disc, with the source name when the tile has none');
assert.ok(render.indexOf('if (bars_frame) tile_icon_source::refresh_controls(card);') >
  render.indexOf('tile_icon_disc::add_round(card, icon_lbl);'), 'the circle color after the disc exists');
// The bars' lower corners keep the header disc's gap to the rounded tile
// corner along its 45-degree diagonal (user 2026-10-29): the disc's edge
// inset plus its radius times (1 - 1/sqrt 2) from both card edges.
const margin = fn(renderer, 'bars_corner_margin');
assert.ok(margin.includes('const float radius = (tile_icon_disc::header_diameter(icon_width) - 2 * shrink) / 2.0f;') &&
  margin.includes('return tile_icon_disc::inset() + shrink + static_cast<int32_t>(lroundf(radius * (1.0f - 0.70710678f)));'));
const create = fn(renderer, 'create_energy_bars');
assert.ok(create.includes('bars->room = std::max<int32_t>(0, bars_corner_margin() - climate_layout::kOuterInset);'), 'inside the frame');
assert.ok(create.includes('tile_icon_disc::mark_surface(frame);') && create.includes('lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, LV_PART_MAIN);') &&
  create.includes('const lv_area_t box = tile_graph_box::box(tile);'), 'the Sensor graph\'s box and resting surface');
assert.ok(read('src/types/sensor/renderer.cpp').includes('static lv_area_t sensor_graph_box(const Tile& tile) { return tile_graph_box::box(tile); }'),
  'one box for both');
// Live data: a day answer reaches every tile with bars of its source.
const data = read('src/types/energy/energy_data.cpp');
assert.match(data, /if \(strcmp\(period, "day"\) == 0\) \{\s*queue_energy_tile_update_for_entry\(entry\);\s*energy_tile_bars_update\(entry\);/);
assert.ok(fn(renderer, 'energy_tile_bars_update').includes('if (copy_day_values(*live.bars, entry)) lv_obj_invalidate(live.frame);'),
  'only changed hours redraw');
assert.ok(fn(renderer, 'energy_bars_delete_cb').includes('if (live.frame == frame) live = LiveBars{};'), 'a deleted tile leaves the table');

// ---- The editor -------------------------------------------------------------
const handler = read('src/types/energy/web_handler.cpp');
assert.ok(handler.includes('server.hasArg("sensor_display_mode") && server.arg("sensor_display_mode").toInt() == 2 ? 2 : 0;'));
const html = read('src/types/energy/web_html.cpp');
assert.ok(html.indexOf('"energy_display_mode"') > html.indexOf('if (tab_id != "screensaver") {'), 'no choice on the screensaver');
// The editor like the Sensor's (user 2026-10-29): segmented choices in its order.
assert.ok(html.includes('const SwitchChoice displays[] = {{"0", tr.sensor_layout_value}, {"2", tr.sensor_display_graph}};') &&
  html.includes('append_switch_choice(html, tab_id, "energy_display_mode", tr.switch_display, displays, 2);'), 'the Sensor tile\'s choice and words');
assert.ok(html.includes('append_switch_choice(html, tab_id, "energy_value_font", tr.sensor_value_size, sizes, 6, size_attrs);') &&
  html.includes('append_switch_choice(html, tab_id, "energy_popup_open_mode", tr.popup_open, presses, 2);'), 'value size and press as choices');
assert.ok(html.indexOf('"energy_value_font"') < html.indexOf('_energy_unit') &&
  html.indexOf('"energy_display_mode"') < html.indexOf('_energy_value_y_offset') &&
  html.indexOf('_energy_value_y_offset') < html.indexOf('"energy_popup_open_mode"'), 'the Sensor editor\'s order');
assert.ok(read('src/types/switch/admin.js').includes("'energy_value_font', 'energy_display_mode', 'energy_popup_open_mode'"),
  'the choices follow their selects');
// No value offset row: the Sensor editor shows it with the old gauge only
// (user 2026-10-28, again 2026-10-29 for Energy); the field keeps its value.
assert.ok(html.includes('<label class="hidden" for=")html";') && html.includes('<input class="hidden" type="number" id=")html";') &&
  html.indexOf('<input class="hidden" type="number" id=")html";') < html.indexOf('_energy_value_y_offset" min="-100"'),
  'the value offset stays hidden');
const admin = read('src/types/energy/admin.js');
assert.ok(admin.includes("if (displayEl) displayEl.value = Number(data.sensor_display_mode) === 2 ? '2' : '0';") &&
  admin.includes("formData.append('sensor_display_mode', document.getElementById(prefix + '_energy_display_mode')?.value || '0');") &&
  admin.includes("if (displayEl) displayEl.value = '0';"), 'load, save and reset');
assert.ok(read('src/web/admin/tiles/editor.js').includes(
  "bindLive(energyDisplayModeSelect, 'change', 'energyDisplayMode', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });"),
  'a change updates the preview, the draft and saves');
assert.ok(read('src/web/admin/tiles/import-export.js').includes(
  "fd.append('sensor_display_mode', !screensaverEnergy && Number(tile.sensor_display_mode) === 2 ? '2' : '0');"), 'import and paste');
assert.ok(read('src/web/admin/tiles/layout.js').includes(
  'const sensorGraph = (Number(type) === 1 || Number(type) === 14) && Number(mode) === 2 && !halfHeight;'), 'the previews\' header');
const css = read('src/web/assets/admin.css');
assert.equal((css.match(/\.tile\.energy\.sensor-graph/g) || []).length, (css.match(/\.tile\.sensor\.sensor-graph/g) || []).length,
  'every Sensor graph header rule covers the Energy tile');

// ---- Real LVGL: the bars' geometry and colors ---------------------------------
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the bars\' drawing needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}
const out = path.join(root, 'build/tests/energy-tile-bars');
fs.mkdirSync(out, {recursive: true});
const energyData = read('src/types/energy/energy_data.h');
const structs = renderer.slice(renderer.indexOf('constexpr uint8_t kEnergyBarsMode'), renderer.indexOf('// Copies a day answer'));
const cpp = String.raw`
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <lvgl.h>
#include <lvgl_private.h>
class String:public std::string{public:using std::string::string;String()=default;String(const std::string&s):std::string(s){}
 bool equalsIgnoreCase(const String&s)const{return *this==s;}};
${energyData.match(/static constexpr uint8_t ENERGY_VALUES_MAX = \d+;/)[0]}
${energyData.match(/struct EnergyEntryData \{[\s\S]*?\n\};/)[0]}
namespace climate_layout { constexpr int kOuterInset = 6; }
namespace tile_layout { constexpr int scale(int v) { return v; } }
namespace ui_theme { struct SurfaceScope {}; inline uint32_t text() { return 0xFFFFFF; } }
namespace tile_surface { inline ui_theme::SurfaceScope scope(lv_obj_t*) { return {}; } }
namespace ui_surface_style { inline uint8_t icon_glow_percent() { return 25; } }
// A V2-like header disc: 54 px box, 4 px inset, drawn 2 px smaller per side.
#define FONT_MDI_ICONS (static_cast<const lv_font_t*>(nullptr))
namespace tile_icon_disc { inline bool see_through(lv_obj_t*) { return false; } constexpr uint32_t kMdiReferenceGlyph = 0xF0001;
 inline int inset() { return 4; } inline int corner_shrink() { return 2; } inline int header_diameter(int) { return 54; } }
namespace tone_color { inline uint8_t control_opa(uint8_t, bool) { return LV_OPA_COVER; } }
struct TestSerial{template<class... T> void printf(const char*,T...){}} Serial;
namespace {
${structs}
${fn(renderer, 'copy_day_values')}
${fn(renderer, 'energy_bars_draw_cb')}
}
constexpr int W = 198, H = 150;
std::vector<uint32_t> pixels(W * H);
uint32_t at(int x, int y) { return pixels[y * W + x] & 0xFFFFFF; }
const uint32_t kCard = 0x493011, kCircle = 0x693800, kIcon = 0xFF9800;
// The frame where the tile's graph box sits: 6 px inset, 70 px from the top.
lv_obj_t* frame = nullptr;
lv_area_t box;
void draw(const std::vector<float>& hours) {
  EnergyEntryData entry;
  entry.period = "day";
  entry.value_count = static_cast<uint8_t>(hours.size());
  for (size_t i = 0; i < hours.size(); ++i) { entry.values[i] = hours[i]; entry.value_valid[i] = true; }
  auto* bars = static_cast<EnergyTileBars*>(lv_obj_get_user_data(frame));
  copy_day_values(*bars, entry);
  lv_obj_invalidate(frame);
  lv_refr_now(nullptr);
}
// The painted extent of the bars inside the frame.
struct Extent { int left = W, right = -1, top = H, bottom = -1; };
Extent bars_extent() {
  Extent e;
  for (int y = box.y1; y <= box.y2; ++y)
    for (int x = box.x1; x <= box.x2; ++x) {
      const uint32_t c = at(x, y);
      if (c != kCircle && c != kIcon) continue;
      e.left = std::min(e.left, x); e.right = std::max(e.right, x);
      e.top = std::min(e.top, y); e.bottom = std::max(e.bottom, y);
    }
  return e;
}
int main() {
  lv_init();
  auto* display = lv_display_create(W, H);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 4, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(kCard), 0);
  lv_obj_t* icon = lv_label_create(screen);
  lv_obj_set_style_text_color(icon, lv_color_hex(kIcon), 0);
  lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
  frame = lv_obj_create(screen);
  lv_obj_remove_style_all(frame);
  lv_obj_set_style_bg_color(frame, lv_color_hex(kCircle), 0);  // refresh_controls' circle color
  lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(frame, 6, 70);
  lv_obj_set_size(frame, W - 12, H - 76);
  auto* bars = new EnergyTileBars();
  bars->icon = icon;
  bars->room = 6;  // bars_corner_margin() - kOuterInset on a panel
  lv_obj_set_user_data(frame, bars);
  lv_obj_add_event_cb(frame, energy_bars_draw_cb, LV_EVENT_DRAW_MAIN, bars);
  lv_obj_update_layout(screen);
  lv_obj_get_coords(frame, &box);
  const int room = climate_layout::kOuterInset;
  // The corner margin: the disc's diagonal point (edge 6 px, radius 25 px)
  // lies 6 + 25 * (1 - 1/sqrt 2) = 13.3 px from both edges.
  assert(bars_corner_margin() == 13 && "the disc's distance to the corner, along the diagonal");

  // Consumption over the whole day, the highest in the last hour: the first
  // and the last bar stand the outer inset from the frame's sides, the
  // bottom ones the same from its bottom, so the tile's edges are equally
  // far at the sides and at the bottom (box inset + room).
  std::vector<float> use = {0.3f,0.25f,0.2f,0.2f,0.2f,0.25f,0.4f,0.6f,0.5f,0.4f,0.35f,0.5f,0.6f,0.4f,0.35f,0.4f,0.5f,0.7f,0.9f,1.0f,0.8f,0.7f,0.9f,1.6f};
  draw(use);
  Extent e = bars_extent();
  assert(e.left == box.x1 + room && e.right == box.x2 - room && "the first and last hour flush at the side room");
  assert(e.bottom == box.y2 - room && e.top == box.y1 + room && "the tallest bar at the top room, all stand on the bottom room");
  assert(e.left == (H - 1) - e.bottom && "equal distance to the tile's edge at the side and at the bottom");
  // The running hour all icon color; the others the circle's tone with the
  // icon-colored edge on top.
  const int last_x = e.right - 1;
  assert(at(last_x, e.bottom) == kIcon && at(last_x, e.top) == kIcon && "the running hour all icon color");
  const int first_x = e.left + 1;
  int first_top = box.y2; for (int y = box.y1; y <= box.y2; ++y) if (at(first_x, y) != kCard) { first_top = y; break; }
  assert(at(first_x, first_top) == kIcon && at(first_x, e.bottom) == kCircle && "the edge on top, the body in the circle's tone");

  // The deepest feed-in right after midnight: a zero line where the range
  // puts it, the bar below it with its edge at the bottom, still inside the
  // bottom room (clear of the rounded corner).
  std::vector<float> feed = {-1.4f,-0.3f,0.2f,0.25f,0.3f,0.35f,0.4f,0.3f,0.1f,-0.2f,-0.5f,-0.6f,-0.4f,0.2f,0.3f,0.35f,0.4f,0.5f,0.6f,0.55f,0.5f,0.45f,0.4f,0.3f};
  draw(feed);
  e = bars_extent();
  assert(e.left == box.x1 + room && e.bottom == box.y2 - room && "the deepest bar at midnight ends in the bottom room");
  int deep_bottom = -1, deep_top = H;
  for (int y = box.y1; y <= box.y2; ++y) if (at(first_x, y) != kCard) { deep_top = std::min(deep_top, y); deep_bottom = y; }
  assert(deep_bottom == box.y2 - room && at(first_x, deep_bottom) == kIcon && at(first_x, deep_top) == kCircle &&
         "a feed-in bar hangs from the zero line with its edge at the bottom");
  // A measured 0 shows its edge on the zero line, so the night does not look
  // like missing data (user 2026-10-29); hours without data show nothing.
  const int edge = 2;  // tile_layout::scale(2) here
  draw({0.f, 0.f, 0.f, 0.5f, 1.0f});
  auto column = [&](int x) { std::vector<int> ys; for (int y = box.y1; y <= box.y2; ++y) if (at(x, y) != kCard) ys.push_back(y); return ys; };
  const std::vector<int> zero_hour = column(box.x1 + room + 1);
  assert(zero_hour.size() == size_t(edge) && zero_hour.back() == box.y2 - room && at(box.x1 + room + 1, zero_hour.back()) == kIcon &&
         "a zero hour: its edge on the zero line");
  assert(column(box.x2 - room - 1).empty() && "an hour without data: nothing");
  // A whole day of zeros: every hour its edge on the bottom room.
  draw(std::vector<float>(24, 0.f));
  e = bars_extent();
  assert(e.left == box.x1 + room && e.right == box.x2 - room && e.top == box.y2 - room - edge + 1 && e.bottom == box.y2 - room &&
         "zeros all day: a row of edges");
  // Nothing at all before the first answer.
  draw({});
  e = bars_extent();
  assert(e.right < 0 && "no bars without data");
  std::printf("Energy tile bars: equal edges, safe corners, edges at the value's end\n");
  return 0;
}
`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, 'test' + (process.platform === 'win32' ? '.exe' : ''));
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8', timeout: 60000});
assert.equal(result.status, 0, result.stdout + result.stderr);
process.stdout.write(result.stdout);
console.log('Energy tile bars: choice, editor, preview and real drawing passed');
