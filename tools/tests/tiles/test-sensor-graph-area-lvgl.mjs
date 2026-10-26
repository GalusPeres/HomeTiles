// The Sensor graph (mockup build/design-mockups/sensor-graph-2, issue #131,
// user 2026-10-26): the tile draws the area below its line in the circle's
// color, the popup a dithered fade from 40 % of the icon color to nothing
// under the line with the card's color above it. The area is one picture
// computed from the line (area_graph.h) and drawn as one image: b558 built it
// from a rect and a triangle per segment, several hundred LVGL draw tasks and
// about 150 ms per popup frame on the V2 (user log 2026-10-26). Renders the
// real pictures with LVGL 9.6 under an lv_chart line: every pixel well below
// the line is the area, every pixel well above it the card; the fade sits on
// exact RGB565 steps and is dithered instead of banded; the picture is only
// computed again after a change. The wiring is checked in the sources.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');

// The area is one image, never pieces per segment.
const areaGraph = read('src/ui/shared/area_graph.h');
for (const gone of ['lv_draw_triangle', 'lv_draw_rect', 'for_each_segment']) {
  assert.ok(!areaGraph.includes(gone), `area_graph.h: no ${gone} any more (one picture, one draw task)`);
}

// Tile: the Switch header beside the disc, the graph in a rounded frame
// below it whose clip_corner cuts line and area at the bottom corners, the
// frame one radius taller upwards so the top stays open; the area one A8
// mask recolored in the circle's current color.
const tile = read('src/types/sensor/renderer.cpp');
assert.ok(tile.includes('const tile_header::Header header = tile_header::create(card, tile, false, box.y1);'),
  'graph tile: the Switch header beside the disc');
assert.ok(tile.includes('lv_obj_set_style_clip_corner(frame, true, LV_PART_MAIN);') &&
  tile.includes('lv_obj_set_size(frame, lv_area_get_width(&box), lv_area_get_height(&box) + radius);') &&
  tile.includes('tile_icon_disc::mark_surface(frame);') && tile.includes('chart = lv_chart_create(frame);'),
  'graph tile: a rounded surface frame clips the graph');
assert.ok(tile.includes('area_graph::render_mask(area->mask, static_cast<uint32_t>(width), width, height, area->tops);') &&
  tile.includes('image.recolor = lv_obj_get_style_bg_color(frame, LV_PART_MAIN);') &&
  tile.includes('if (!area->mask || key != area->key) {') &&
  tile.includes('static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), area);') &&
  tile.includes('lv_obj_add_event_cb(chart, sensor_graph_delete_cb, LV_EVENT_DELETE, area);') &&
  tile.includes('series->color = area->icon ? lv_obj_get_style_text_color(area->icon, LV_PART_MAIN)'),
  'graph tile: one recolored mask before the line, freed with the chart, the line in the icon color');
assert.ok(!tile.includes('sensor_graph_height'), 'graph tile: the graph fills the box below the header');
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.ok(renderer.includes('const size_t target = graph_w > 8 ? static_cast<size_t>(graph_w / 4) : 96;'),
  'graph tile: one point per four pixels');
assert.ok(renderer.includes('!lv_color_eq(lv_chart_get_series_color(chart, series), lv_obj_get_style_text_color(icon, LV_PART_MAIN))'),
  'graph tile: a new icon color redraws the line');

// Web Admin preview: the graph tile shows the same header beside the disc
// (the graph itself needs the Bridge's history, which the preview lacks).
const layoutJs = read('src/web/admin/tiles/layout.js');
assert.ok(layoutJs.includes("const sensorGraph = Number(type) === 1 && Number(mode) === 2 && !halfHeight;") &&
  layoutJs.includes("el.classList.toggle('sensor-graph', sensorGraph);"), 'preview: the graph tile class');
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('.tile.sensor.sensor-graph > .tile-value {') && css.includes('.tile.sensor.sensor-graph { display:block; }'),
  'preview: the Switch header rules cover the graph tile');

// Popup: no vertical lines or guide line objects, labels and graph at the
// header circle's inset, the area as one RGB565 picture, dashed lines, dots.
const popup = read('src/ui/popups/sensor/sensor_popup.cpp');
for (const gone of ['time_lines', 'y_max_line', 'y_min_line', 'calc_day7_boundary_axis', 'image.tile']) {
  assert.ok(!popup.includes(gone), `popup: no ${gone} any more`);
}
assert.ok(popup.includes('constexpr int kGraphInset = popup_layout::kHeaderIconX;') &&
  popup.includes('int chart_left = kGraphInset + axis_w;') && popup.includes('const int chart_right = kAvailW - kGraphInset;') &&
  popup.includes('lv_obj_set_style_text_align(y_max, LV_TEXT_ALIGN_LEFT, 0);'),
  'popup: labels left, graph between the header circle insets');
// The unit's room (the label's right padding) counts once: twice left a gap
// of the unit's width between the left-aligned labels and the graph.
const measure = popup.slice(popup.indexOf('static lv_coord_t measure_label_text_width'),
  popup.indexOf('static bool get_valid_local_time'));
assert.equal((measure.match(/lv_obj_get_style_pad_right\(label, LV_PART_MAIN\)/g) || []).length, 1,
  'popup: label width counts the unit room once');
assert.ok(popup.includes('area_graph::render_fade(ctx->area_pixels, stride / sizeof(uint16_t), width, height, ctx->area_tops, color, card,') &&
  popup.includes('if (ctx->area_pixels && key == ctx->area_key) return &ctx->area_image;') &&
  popup.includes('dash.dash_width = kDashLength;') &&
  popup.includes('lv_obj_add_event_cb(chart_wrap, on_chart_extremes_draw, LV_EVENT_DRAW_POST, ctx);'),
  'popup: the area picture (computed again only after a change), the dashed lines, the dots');
assert.ok(popup.includes('constexpr uint8_t kFadeTopOpa = 102;  // 40 %'), 'popup: 40 % at the top');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: Sensor graph area needs native LVGL');
  process.exit(0);
}
const out = path.join(root, 'build/tests/sensor-graph-area');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <lvgl.h>
#include <lvgl_private.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "src/ui/shared/area_graph.h"

static const uint32_t kCard = 0x1A1A1A, kFill = 0x6E2A12, kIcon = 0xFF7043;
static lv_chart_series_t* g_series = nullptr;
static bool g_popup = false;
static int g_renders = 0, g_images = 0;
static uint32_t g_key = 0;
static std::vector<int16_t> g_tops;
static std::vector<uint16_t> g_fade;
static std::vector<uint8_t> g_mask;
static lv_image_dsc_t g_image{};

// The callbacks of the tile renderer and the popup in short: the picture
// from the line, computed again only after a change, drawn as one image.
static void area_cb(lv_event_t* e) {
  lv_obj_t* chart = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t content, coords;
  lv_obj_get_content_coords(chart, &content);
  lv_obj_get_coords(chart, &coords);
  const int32_t width = lv_area_get_width(&content);
  const int32_t height = g_popup ? lv_area_get_height(&content) + 1 : coords.y2 - content.y1 + 1;
  g_tops.assign(width, 0);
  if (!area_graph::column_tops(chart, g_series, g_tops.data(), width)) return;
  const uint32_t key = area_graph::area_key(g_tops.data(), width, height, kIcon, kCard);
  if (key != g_key) {
    ++g_renders;
    g_key = key;
    lv_image_cache_drop(&g_image);
    g_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_image.header.w = width;
    g_image.header.h = height;
    if (g_popup) {
      g_fade.assign(width * height, 0);
      area_graph::render_fade(g_fade.data(), width, width, height, g_tops.data(), kIcon, kCard, 102);
      g_image.header.cf = LV_COLOR_FORMAT_RGB565;
      g_image.header.stride = width * 2;
      g_image.data = reinterpret_cast<const uint8_t*>(g_fade.data());
      g_image.data_size = g_fade.size() * 2;
    } else {
      g_mask.assign(width * height, 0);
      area_graph::render_mask(g_mask.data(), width, width, height, g_tops.data());
      g_image.header.cf = LV_COLOR_FORMAT_A8;
      g_image.header.stride = width;
      g_image.data = g_mask.data();
      g_image.data_size = g_mask.size();
    }
  }
  lv_draw_image_dsc_t image;
  lv_draw_image_dsc_init(&image);
  image.base.layer = layer;
  image.src = &g_image;
  // The tile's mask takes the circle's color; the popup's picture has its own.
  if (!g_popup) {
    image.recolor = lv_color_hex(kFill);
    image.recolor_opa = LV_OPA_COVER;
  }
  const lv_area_t at = {content.x1, content.y1, content.x1 + width - 1, content.y1 + height - 1};
  lv_draw_image(layer, &image, &at);
  ++g_images;
}

static uint32_t pixel(const lv_draw_buf_t* buf, int x, int y) {
  const uint8_t* p = buf->data + y * buf->header.stride + x * 4;
  return (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0];
}
static int channel(uint32_t rgb, int c) { return (rgb >> (16 - 8 * c)) & 0xFF; }

int main() {
  lv_init();
  // The fade on exact RGB565 steps with 40 % of the icon over the card in
  // the first row and the card in the last, dithered: every row mixes the
  // two steps around its exact color.
  {
    const int width = 64, height = 160;
    std::vector<int16_t> tops(width, 0);
    std::vector<uint16_t> fade(width * height);
    area_graph::render_fade(fade.data(), width, width, height, tops.data(), kIcon, kCard, 102);
    for (int y = 0; y < height; ++y) {
      double sum = 0;
      for (int x = 0; x < width; ++x) sum += (fade[y * width + x] >> 11) * 255.0 / 31;
      const double exact = 0x1A + (0xFF - 0x1A) * 0.4 * (1.0 - static_cast<double>(y) / (height - 1));
      assert(sum / width > exact - 9.0 && sum / width < exact + 9.0 && "the dither keeps the row's mean");
    }
    double previous = 1e9;
    for (int block = 0; block + 8 <= height; block += 8) {
      double sum = 0;
      for (int y = block; y < block + 8; ++y)
        for (int x = 0; x < width; ++x) sum += fade[y * width + x] >> 11;
      assert(sum <= previous + 0.01 && "the fade only darkens downwards");
      previous = sum;
    }
    assert(fade[(height - 1) * width] == area_graph::rgb565(kCard) && "the last row is the card");
    bool mixed = false;
    for (int y = 0; y < height && !mixed; ++y)
      for (int x = 1; x < 8; ++x) mixed = mixed || fade[y * width + x] != fade[y * width];
    assert(mixed && "dithered");
    // Above the line and in gaps the card.
    std::fill(tops.begin(), tops.end(), static_cast<int16_t>(80));
    tops[5] = -1;
    area_graph::render_fade(fade.data(), width, width, height, tops.data(), kIcon, kCard, 102);
    assert(fade[40 * width + 10] == area_graph::rgb565(kCard) && fade[100 * width + 5] == area_graph::rgb565(kCard));
    assert(fade[100 * width + 10] != area_graph::rgb565(kCard));
  }

  lv_display_t* display = lv_display_create(320, 220);
  static std::vector<uint32_t> buffer(320 * 220);
  lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size() * 4, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  for (int mode = 0; mode < 2; ++mode) {
    g_popup = mode == 1;
    g_renders = g_images = 0;
    g_key = 0;
    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kCard), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_t* chart = lv_chart_create(screen);
    lv_obj_set_pos(chart, 20, 30);
    lv_obj_set_size(chart, 280, 168);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_radius(chart, 0, 0);
    lv_obj_set_style_pad_hor(chart, 0, 0);
    lv_obj_set_style_pad_ver(chart, 4, 0);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_obj_set_style_line_width(chart, 4, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart, true, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    g_series = lv_chart_add_series(chart, lv_color_hex(kIcon), LV_CHART_AXIS_PRIMARY_Y);
    // Steep and flat stretches, a peak, a valley and a gap (288 points: more
    // points than columns, LVGL's crowded line).
    const int count = 288;
    lv_chart_set_point_count(chart, count);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    for (int i = 0; i < count; ++i) {
      int v = i < 80 ? 200 + i * 10 : i < 120 ? 1000 - (i - 80) * 22 : i < 200 ? 120 : 120 + (i - 200) * 9;
      lv_chart_set_value_by_id(chart, g_series, i, i >= 150 && i < 160 ? LV_CHART_POINT_NONE : v);
    }
    lv_obj_add_event_cb(chart, area_cb, static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), nullptr);
    lv_screen_load(screen);
    lv_refr_now(display);
    lv_draw_buf_t* shot = lv_snapshot_take(screen, LV_COLOR_FORMAT_XRGB8888);
    assert(shot);
    lv_area_t content;
    lv_obj_get_content_coords(chart, &content);
    int checked = 0, gaps = 0;
    // The line's round caps reach two pixels into a gap, where the area has
    // already ended: the columns next to a gap are left out.
    std::vector<int> gap_columns;
    for (int i = 150; i <= 160; ++i) {
      lv_point_t point;
      lv_chart_get_point_pos_by_id(chart, g_series, i == 150 ? 149 : 160, &point);
      gap_columns.push_back(lv_obj_get_x(chart) + point.x);
    }
    const int gap_from = gap_columns.front(), gap_to = gap_columns.back();
    for (int x = content.x1; x <= content.x2; ++x) {
      if (x >= gap_from - 1 && x <= gap_from + 3) continue;
      if (x >= gap_to - 3 && x <= gap_to + 1) continue;
      // The line's top and bottom edge in this column.
      int top = -1, bottom = -1;
      for (int y = content.y1 - 4; y <= content.y2 + 4; ++y) {
        if (channel(pixel(shot, x, y), 0) > 0xC0) {
          if (top < 0) top = y;
          bottom = y;
        }
      }
      if (top < 0) {
        // A gap: no line and no area.
        for (int y = content.y1; y <= content.y2; ++y) assert((pixel(shot, x, y) & 0xF8FCF8) == (kCard & 0xF8FCF8) && "a gap shows the card (on a 16-bit panel exactly)");
        ++gaps;
        continue;
      }
      for (int y = content.y1; y <= content.y2; ++y) {
        const uint32_t p = pixel(shot, x, y);
        if (y < top - 3) {
          if ((p & 0xF8FCF8) != (kCard & 0xF8FCF8))
            std::fprintf(stderr, "mode=%d x=%d y=%d top=%d p=%06x\n", mode, x, y, top, static_cast<unsigned>(p));
          assert((p & 0xF8FCF8) == (kCard & 0xF8FCF8) && "above the line: the card");
        } else if (y > bottom + 3) {
          if (!g_popup) {
            if (p != kFill) std::fprintf(stderr, "x=%d y=%d top=%d bottom=%d p=%06x\n", x, y, top, bottom, static_cast<unsigned>(p));
            assert(p == kFill && "below the line: the area");
          } else {
            assert(channel(p, 0) >= 0x18 && channel(p, 0) <= 0x7F && "the fade");
            if (y < content.y1 + (content.y2 - content.y1) * 3 / 4)
              assert((p & 0xF8FCF8) != (kCard & 0xF8FCF8) && "the fade under the line");
          }
          ++checked;
        }
      }
    }
    assert(checked > 5000 && gaps >= 2);
    // One image per frame; a redraw without a change reuses the picture.
    assert(g_renders == 1 && g_images >= 1);
    lv_obj_invalidate(chart);
    lv_refr_now(display);
    assert(g_renders == 1 && g_images >= 2 && "an unchanged line keeps its picture");
    lv_chart_set_value_by_id(chart, g_series, 10, 900);
    lv_refr_now(display);
    assert(g_renders == 2 && "a changed line computes its picture again");
    lv_draw_buf_destroy(shot);
    lv_obj_delete(screen);
  }
  lv_image_cache_drop(&g_image);
  lv_deinit();
  std::puts("ok");
  return 0;
}
`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, 'test' + (process.platform === 'win32' ? '.exe' : ''));
const built = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-I', out, source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(built.status, 0, built.stdout + built.stderr);
const run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
console.log('sensor graph: one picture per change for the tile area and the popup fade, card above the line and in gaps, dithered RGB565 fade');
