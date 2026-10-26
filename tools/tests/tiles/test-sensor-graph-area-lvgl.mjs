// The Sensor graph (mockup build/design-mockups/sensor-graph-2, issue #131,
// user 2026-10-26): the tile draws the area below its line in the circle's
// color, the popup a dithered fade from 40 % of the icon color to nothing
// under the line with the card's color covering what lies above it. Renders
// area_graph.h's real pieces with LVGL 9.6 under an lv_chart line: every
// pixel well below the line is the area, every pixel well above it the card,
// no seam between the segments' pieces; the fade sits on exact RGB565 steps
// (the panels' format) and is dithered instead of banded. The wiring in the
// tile renderer and the popup is checked in their sources.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');

// Tile: the Switch header beside the disc, the graph in a rounded frame
// below it whose clip_corner cuts line and area at the bottom corners, the
// frame one radius taller upwards so the top stays open.
const tile = read('src/types/sensor/renderer.cpp');
assert.ok(tile.includes('const tile_header::Header header = tile_header::create(card, tile, false, box.y1);'),
  'graph tile: the Switch header beside the disc');
assert.ok(tile.includes('lv_obj_set_style_clip_corner(frame, true, LV_PART_MAIN);') &&
  tile.includes('lv_obj_set_size(frame, lv_area_get_width(&box), lv_area_get_height(&box) + radius);') &&
  tile.includes('tile_icon_disc::mark_surface(frame);') && tile.includes('chart = lv_chart_create(frame);'),
  'graph tile: a rounded surface frame clips the graph');
assert.ok(tile.includes('area_graph::draw_below(layer, a, b, last, coords.y2, rect, triangle);') &&
  tile.includes('static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), icon_lbl);') &&
  tile.includes('series->color = icon ? lv_obj_get_style_text_color(icon, LV_PART_MAIN)'),
  'graph tile: the area before the line, the line in the icon color');
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
// header circle's inset, the fade under the line, dashed lines, dots.
const popup = read('src/ui/popups/sensor/sensor_popup.cpp');
for (const gone of ['time_lines', 'y_max_line', 'y_min_line', 'calc_day7_boundary_axis']) {
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
assert.ok(popup.includes('area_graph::fill_fade(ctx->fade_pixels, stride, kFadeWidth, height, color, card, kFadeTopOpa);') &&
  popup.includes('image.tile = 1;') && popup.includes('area_graph::draw_above(layer, a, b, content.y1, rect, triangle);') &&
  popup.includes('dash.dash_width = kDashLength;') &&
  popup.includes('lv_obj_add_event_cb(chart_wrap, on_chart_extremes_draw, LV_EVENT_DRAW_POST, ctx);'),
  'popup: the fade strip, the cover, the dashed lines, the dots');
assert.ok(popup.includes('constexpr uint8_t kFadeTopOpa = 102;  // 40 %') && popup.includes('constexpr int kFadeWidth = 8;'),
  'popup: 40 % at the top, an 8-pixel strip');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: Sensor graph area needs native LVGL');
  process.exit(0);
}
const out = path.join(root, 'build/tests/sensor-graph-area');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <lvgl.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "src/ui/shared/area_graph.h"

static const uint32_t kCard = 0x1A1A1A, kFill = 0x6E2A12, kIcon = 0xFF7043;
static lv_chart_series_t* g_series = nullptr;
static bool g_popup = false;
static lv_image_dsc_t g_fade{};
static std::vector<uint8_t> g_fade_pixels;

static void area_cb(lv_event_t* e) {
  lv_obj_t* chart = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t content;
  lv_obj_get_content_coords(chart, &content);
  lv_area_t coords;
  lv_obj_get_coords(chart, &coords);
  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  rect.base.layer = layer;
  rect.bg_color = lv_color_hex(g_popup ? kCard : kFill);
  rect.bg_opa = LV_OPA_COVER;
  rect.border_opa = LV_OPA_TRANSP;
  lv_draw_triangle_dsc_t triangle;
  lv_draw_triangle_dsc_init(&triangle);
  triangle.base.layer = layer;
  triangle.color = rect.bg_color;
  triangle.opa = LV_OPA_COVER;
  const lv_area_t clip = layer->_clip_area;
  if (g_popup) {
    lv_draw_image_dsc_t image;
    lv_draw_image_dsc_init(&image);
    image.base.layer = layer;
    image.src = &g_fade;
    image.tile = 1;
    image.image_area = {content.x1, content.y1, content.x1 + 7, content.y2};
    lv_draw_image(layer, &image, &content);
    area_graph::for_each_segment(chart, g_series, clip, [&](const lv_point_t& a, const lv_point_t& b, bool) {
      area_graph::draw_above(layer, a, b, content.y1, rect, triangle);
    });
  } else {
    area_graph::for_each_segment(chart, g_series, clip, [&](const lv_point_t& a, const lv_point_t& b, bool last) {
      area_graph::draw_below(layer, a, b, last, coords.y2, rect, triangle);
    });
  }
}

static uint32_t pixel(const lv_draw_buf_t* buf, int x, int y) {
  const uint8_t* p = buf->data + y * buf->header.stride + x * 4;
  return (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0];
}
static int channel(uint32_t rgb, int c) { return (rgb >> (16 - 8 * c)) & 0xFF; }

int main() {
  lv_init();
  // The fade on exact RGB565 steps, top 40 % of the icon over the card,
  // bottom the card, every row dithered between neighbouring steps.
  const int width = 8, height = 160;
  const uint32_t stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_ARGB8888);
  g_fade_pixels.assign(stride * height, 0);
  area_graph::fill_fade(g_fade_pixels.data(), stride, width, height, kIcon, kCard, 102);
  const int bits[3] = {5, 6, 5};
  for (int y = 0; y < height; ++y) {
    double sum = 0;
    for (int x = 0; x < width; ++x) {
      const lv_color32_t px = reinterpret_cast<const lv_color32_t*>(g_fade_pixels.data() + y * stride)[x];
      assert(px.alpha == 0xFF);
      const int value[3] = {px.red, px.green, px.blue};
      for (int c = 0; c < 3; ++c) {
        const int step = value[c] >> (8 - bits[c]);
        const int expanded = c == 1 ? (step << 2) | (step >> 4) : (step << 3) | (step >> 2);
        assert(value[c] == expanded && "an exact RGB565 step");
      }
      sum += px.red;
    }
    const double mean = sum / width;
    const double t = static_cast<double>(y) / (height - 1);
    const double exact = 0x1A + (0xFF - 0x1A) * 0.4 * (1.0 - t);
    assert(mean > exact - 9.0 && mean < exact + 9.0 && "the dither keeps the row's mean");
  }
  // Over a whole 8x8 dither block the fade only darkens downwards.
  double previous = 1e9;
  for (int block = 0; block + 8 <= height; block += 8) {
    double sum = 0;
    for (int y = block; y < block + 8; ++y) {
      for (int x = 0; x < width; ++x) sum += reinterpret_cast<const lv_color32_t*>(g_fade_pixels.data() + y * stride)[x].red;
    }
    assert(sum / 64 <= previous + 0.01 && "the fade only darkens downwards");
    previous = sum / 64;
  }
  assert(reinterpret_cast<const lv_color32_t*>(g_fade_pixels.data() + (height - 1) * stride)[0].red == 0x18 ||
         reinterpret_cast<const lv_color32_t*>(g_fade_pixels.data() + (height - 1) * stride)[0].red == 0x1A ||
         reinterpret_cast<const lv_color32_t*>(g_fade_pixels.data() + (height - 1) * stride)[0].red == 0x21);
  // A fractional level mixes two steps in one row (no bands).
  bool mixed = false;
  for (int y = 0; y < height && !mixed; ++y) {
    const lv_color32_t* row = reinterpret_cast<const lv_color32_t*>(g_fade_pixels.data() + y * stride);
    for (int x = 1; x < width; ++x) mixed = mixed || row[x].red != row[0].red;
  }
  assert(mixed && "dithered");
  g_fade.header.magic = LV_IMAGE_HEADER_MAGIC;
  g_fade.header.cf = LV_COLOR_FORMAT_ARGB8888;
  g_fade.header.w = width;
  g_fade.header.h = height;
  g_fade.header.stride = stride;
  g_fade.data = g_fade_pixels.data();
  g_fade.data_size = g_fade_pixels.size();

  lv_display_t* display = lv_display_create(320, 220);
  static std::vector<uint32_t> buffer(320 * 220);
  lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size() * 4, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  for (int mode = 0; mode < 2; ++mode) {
    g_popup = mode == 1;
    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kCard), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_t* chart = lv_chart_create(screen);
    lv_obj_set_pos(chart, 20, 30);
    lv_obj_set_size(chart, 280, 160);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_radius(chart, 0, 0);
    lv_obj_set_style_pad_hor(chart, 0, 0);
    lv_obj_set_style_pad_ver(chart, 0, 0);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_obj_set_style_line_width(chart, 4, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart, true, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    g_series = lv_chart_add_series(chart, lv_color_hex(kIcon), LV_CHART_AXIS_PRIMARY_Y);
    // Steep and flat stretches, a peak and a valley (70 points, 4 px apart).
    const int count = 71;
    lv_chart_set_point_count(chart, count);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    for (int i = 0; i < count; ++i) {
      int v = i < 20 ? 200 + i * 40 : i < 30 ? 1000 - (i - 20) * 90 : i < 50 ? 100 : 100 + (i - 50) * 30;
      lv_chart_set_value_by_id(chart, g_series, i, v);
    }
    lv_obj_add_event_cb(chart, area_cb, static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), nullptr);
    lv_screen_load(screen);
    lv_refr_now(display);
    lv_draw_buf_t* shot = lv_snapshot_take(screen, LV_COLOR_FORMAT_XRGB8888);
    assert(shot);
    lv_area_t content;
    lv_obj_get_content_coords(chart, &content);
    int checked = 0;
    for (int x = content.x1; x <= content.x2; ++x) {
      // The line's top and bottom edge in this column.
      int top = -1, bottom = -1;
      for (int y = content.y1 - 4; y <= content.y2 + 4; ++y) {
        const uint32_t p = pixel(shot, x, y);
        if (channel(p, 0) > 0xC0) {
          if (top < 0) top = y;
          bottom = y;
        }
      }
      if (top < 0) continue;
      for (int y = content.y1; y <= content.y2; ++y) {
        const uint32_t p = pixel(shot, x, y);
        if (y < top - 3) {
          assert(p == kCard && "above the line: the card");
        } else if (y > bottom + 3) {
          if (!g_popup) {
            assert(p == kFill && "below the line: the area, no seam");
          } else {
            // The fade: between the card and 40 % of the icon, never the card
            // above the bottom quarter.
            assert(channel(p, 0) >= 0x18 && channel(p, 0) <= 0x7F);
            if (y < content.y1 + (content.y2 - content.y1) * 3 / 4) assert(p != kCard && "the fade under the line");
          }
          ++checked;
        }
      }
    }
    assert(checked > 5000);
    lv_draw_buf_destroy(shot);
    lv_obj_delete(screen);
  }
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
console.log('sensor graph: tile area and popup fade under the line, card above it, no seams, dithered RGB565 fade');
