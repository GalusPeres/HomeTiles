// The Sensor graph (mockups build/design-mockups/sensor-graph-2 and -3, issue
// #131, user 2026-10-26/27): the tile draws the area below its line in the
// circle's color, the popup the circle's color down to the graph's middle
// and a dithered fade to nothing below it, the card's color above the line;
// faint solid 1 px lines (8 %) at the maximum, the minimum, zero and the
// time labels. Area, line and the popup's lines are pictures computed from
// the line (area_graph.h) and drawn as images: b558 built the area from a
// rect and a triangle per segment (about 150 ms per popup frame on the V2),
// and LVGL's own 4 px line still cost about 90 ms (b559 log; on the host
// 1.1 ms against 0.09 ms for the picture). Renders the real pictures with
// LVGL 9.6: the line where lv_chart puts its points, every pixel well below
// it the area, every pixel well above it the card; the fade sits on exact
// RGB565 steps and is dithered; the pictures are computed again only after
// a change; drawing them costs a fraction of LVGL's line. The wiring is
// checked in the sources.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');

// The pictures are images, never pieces per segment or LVGL lines.
const areaGraph = read('src/ui/shared/area_graph.h');
for (const gone of ['lv_draw_triangle', 'lv_draw_rect', 'lv_draw_line', 'for_each_segment']) {
  assert.ok(!areaGraph.includes(gone), `area_graph.h: no ${gone} (pictures drawn as images)`);
}

// Tile: the Switch header beside the disc, the graph in a frame below it;
// area and line two A8 masks recolored in the circle's and the icon's
// current color, the box's round bottom corners cut into the masks. A
// clip_corner frame drew the graph three times per redraw, twice into
// ARGB8888 layers (user 2026-10-28: graph tiles slowed the UI). The masks
// are rebuilt only when the chart's raw values, range or size change
// (area_graph::data_key): chart_points asks lv_chart for every point.
const tile = read('src/types/sensor/renderer.cpp');
assert.ok(tile.includes('const tile_header::Header header = tile_header::create(card, tile, false, box.y1);'),
  'graph tile: the Switch header beside the disc');
assert.ok(!tile.includes('lv_obj_set_style_clip_corner(frame') &&
  tile.includes('lv_obj_set_size(frame, lv_area_get_width(&box), lv_area_get_height(&box));') &&
  tile.includes('tile_icon_disc::mark_surface(frame);') && tile.includes('chart = lv_chart_create(frame);') &&
  tile.includes('area_graph::round_bottom_corners(area->masks, static_cast<uint32_t>(width), width, height, radius);') &&
  tile.includes('area_graph::round_bottom_corners(area->masks + bytes, static_cast<uint32_t>(width), width, height, radius);'),
  'graph tile: a surface frame without clip_corner, the round corners in the masks');
assert.ok(tile.includes('area_graph::render_mask(area->masks, static_cast<uint32_t>(width), width, height, area->tops);') &&
  tile.includes('area_graph::line_mask(area->masks + bytes,') &&
  tile.includes('image.recolor = lv_obj_get_style_bg_color(frame, LV_PART_MAIN);') &&
  tile.includes('image.recolor = line_color;') &&
  tile.includes('const uint32_t key = area_graph::data_key(') &&
  tile.includes('if (!area->built || key != area->key) {') &&
  tile.indexOf('area_graph::chart_points(chart, series, area->points, count)') > tile.indexOf('if (!area->built || key != area->key) {') &&
  tile.includes('static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), area);') &&
  tile.includes('lv_obj_add_event_cb(chart, sensor_graph_delete_cb, LV_EVENT_DELETE, area);') &&
  tile.includes('lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS);'),
  'graph tile: two recolored masks computed only after a change, freed with the chart, no LVGL line');
assert.ok(!tile.includes('sensor_graph_height'), 'graph tile: the graph fills the box below the header');
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.ok(renderer.includes('const size_t target = graph_w > 8 ? static_cast<size_t>(graph_w / 4) : 96;'),
  'graph tile: one point per four pixels');
assert.ok(renderer.includes('!lv_color_eq(lv_chart_get_series_color(chart, series), lv_obj_get_style_text_color(icon, LV_PART_MAIN))'),
  'graph tile: a new icon color redraws the line');
// A tile built offline (at boot) asks again once the connection is up and
// every five minutes; several answers wait in a bounded queue.
assert.ok(renderer.includes('constexpr uint32_t kTileGraphRefreshMs = 5UL * 60UL * 1000UL;') &&
  renderer.includes('if (!networkManager.isMqttConnected()) {') &&
  renderer.includes('constexpr size_t kTileGraphQueueSlots = 4;') &&
  renderer.includes('if (tile.sensor_entity.length()) request_tile_graph_history(tile.sensor_entity.c_str());'),
  'graph tile: asks again after connecting and every five minutes, queue for several answers');
assert.ok(read('HomeTiles.ino').includes('    process_tile_graph_queue();\n    tile_graph_service_periodic();'),
  'the main loop runs the graph tile service');

// Web Admin preview: the graph tile shows the same header beside the disc
// (the graph itself needs the Bridge's history, which the preview lacks).
const layoutJs = read('src/web/admin/tiles/layout.js');
assert.ok(layoutJs.includes("const sensorGraph = Number(type) === 1 && Number(mode) === 2 && !halfHeight;") &&
  layoutJs.includes("el.classList.toggle('sensor-graph', sensorGraph);"), 'preview: the graph tile class');
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('.tile.sensor.sensor-graph > .tile-value {') && css.includes('.tile.sensor.sensor-graph { display:block; }'),
  'preview: the Switch header rules cover the graph tile');

// Popup: no guide line objects, labels and graph at the header circle's
// inset, area, grid lines and line as one RGB565 picture, no dots at the
// extremes any more (user 2026-10-27).
const popup = read('src/ui/popups/sensor/sensor_popup.cpp');
for (const gone of ['time_lines', 'y_max_line', 'y_min_line', 'image.tile', 'lv_draw_line_dsc_t dash',
  'on_chart_extremes_draw', 'kExtremeDot', 'draw_dashes', 'kFadeTopOpa']) {
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
assert.ok(popup.includes('area_graph::render_fade(ctx->area_pixels, stride_px, width, height, ctx->area_tops, (fade_top + fade_bottom) / 2,') &&
  popup.includes('fade_bottom, area, card, LV_OPA_COVER);') &&
  popup.includes('area_graph::draw_line(ctx->area_pixels, stride_px, width, height, ctx->area_points, points,') &&
  popup.includes('if (ctx->area_pixels && key == ctx->area_key) return &ctx->area_image;') &&
  popup.includes('lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS);'),
  'popup: one picture (computed again only after a change), no LVGL line, the area solid down to the middle');
// The area in the header circle's color (the selected range button's).
assert.ok(popup.includes('popup_nav_style::fill(card, color, fill, fill_opa);') &&
  popup.includes('const lv_color_t area = lv_color_mix(fill, card, fill_opa);'),
  "popup: the area takes the header circle's color");
// Faint solid lines at the maximum, the minimum, zero and the time labels;
// the zero line with its own label, a label's height from the others.
assert.ok(popup.includes('constexpr int kGridLineWidth = 1;') && popup.includes('constexpr lv_opa_t kGridLineOpa = 20;  // 8 %') &&
  popup.includes('for (const int32_t row : {fade_top, fade_bottom, zero}) {') &&
  popup.includes('time_x[i] = static_cast<int32_t>(ctx->time_fracs[i] * width);') &&
  popup.includes('const int32_t zero = graph_zero_row(ctx, fade_top, fade_bottom);') &&
  popup.includes('return area_graph::zero_row(ctx->chart, top, bottom, lv_font_get_line_height(popup_layout::font20()));') &&
  popup.includes('const int32_t row = graph_zero_row(ctx, 0, lv_obj_get_content_height(ctx->chart));'),
  'popup: faint 1 px lines at max/min/zero/time, the zero line and its label together');
// The time lines: under the 24H and 6H labels, between the days of 7D,
// whose labels sit in the middle of their day (as before sensor-graph-2).
assert.ok(popup.includes('ctx->time_count = static_cast<uint8_t>(calc_day7_boundary_axis(ctx->time_fracs, kTimeAxisMarkerCount));') &&
  popup.includes('const float center = static_cast<float>(segment_start - range_start) +'),
  'popup: 7D lines between the days, labels in the middle of their day');
// The time labels a little further below the graph; the graph gives up that
// room, so the labels stay where they were (b562 cut them off, user
// 2026-10-27).
assert.ok(popup.includes('constexpr int kTimeLabelGap = popup_layout::scale480(12);') &&
  popup.includes('constexpr int kChartHeight = popup_layout::contentScale(325) - kTimeLabelExtra;') &&
  popup.includes('kChartHeight + kTimeLabelExtra + 2 * kLabelOverhang + kTimeAxisHeight') &&
  popup.includes('height = std::max(1, height - kTimeLabelExtra);') &&
  (popup.match(/kTimeLabelGap\);/g) || []).length === 3, "popup: the time labels' gap comes out of the graph");

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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "src/ui/shared/area_graph.h"

static const uint32_t kCard = 0x1A1A1A, kFill = 0x6E2A12, kIcon = 0xFF7043;
// A time line in the popup's graph (picture column), over the flat stretch.
static const int32_t kTimeX = 183;
static lv_chart_series_t* g_series = nullptr;
static bool g_popup = false, g_pictures = true;
static int g_renders = 0;
static uint32_t g_key = 0;
static std::vector<area_graph::Point> g_points;
static std::vector<int16_t> g_tops;
static std::vector<uint16_t> g_picture;
static std::vector<uint8_t> g_masks;
static lv_image_dsc_t g_image{}, g_line{};
static area_graph::Rows g_rows;

static void set_image(lv_image_dsc_t& image, lv_color_format_t cf, const uint8_t* data, int32_t w, int32_t h, int32_t stride) {
  image.header.magic = LV_IMAGE_HEADER_MAGIC;
  image.header.cf = cf;
  image.header.w = w;
  image.header.h = h;
  image.header.stride = stride;
  image.data = data;
  image.data_size = stride * h;
}

// The callbacks of the popup and the tile renderer in short: the pictures
// from the line, computed again only after a change, drawn as images.
static void area_cb(lv_event_t* e) {
  if (!g_pictures) return;
  lv_obj_t* chart = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t coords, content;
  lv_obj_get_coords(chart, &coords);
  lv_obj_get_content_coords(chart, &content);
  const int32_t width = lv_area_get_width(&coords), height = lv_area_get_height(&coords);
  const int32_t count = lv_chart_get_point_count(chart);
  g_points.resize(count);
  g_tops.resize(width);
  const int32_t points = area_graph::chart_points(chart, g_series, g_points.data(), count);
  if (!points) return;
  const int32_t fade_top = content.y1 - coords.y1, fade_bottom = fade_top + lv_area_get_height(&content);
  const int32_t zero = g_popup ? area_graph::zero_row(chart, fade_top, fade_bottom, 8) : -1;
  const uint32_t key =
      area_graph::picture_key(g_points.data(), points, {uint32_t(width), uint32_t(height), g_popup ? 1u : 0u, uint32_t(zero)});
  if (key != g_key) {
    ++g_renders;
    g_key = key;
    lv_image_cache_drop(&g_image);
    lv_image_cache_drop(&g_line);
    area_graph::column_tops(g_points.data(), points, g_tops.data(), width);
    if (g_popup) {
      g_picture.assign(width * height, 0);
      area_graph::render_fade(g_picture.data(), width, width, height, g_tops.data(), (fade_top + fade_bottom) / 2, fade_bottom,
                              kFill, kCard, 255);
      for (const int32_t row : {fade_top, fade_bottom, zero}) {
        if (row >= 0) area_graph::blend_rect(g_picture.data(), width, width, height, 0, row, width - 1, row, 0xFFFFFF, 20);
      }
      area_graph::blend_rect(g_picture.data(), width, width, height, kTimeX, fade_top, kTimeX, fade_bottom, 0xFFFFFF, 20);
      area_graph::draw_line(g_picture.data(), width, width, height, g_points.data(), points, 4.0f, kIcon);
      set_image(g_image, LV_COLOR_FORMAT_RGB565, reinterpret_cast<const uint8_t*>(g_picture.data()), width, height, width * 2);
    } else {
      g_masks.assign(2 * width * height, 0);
      area_graph::render_mask(g_masks.data(), width, width, height, g_tops.data());
      area_graph::line_mask(g_masks.data() + width * height, width, width, height, g_points.data(), points, 3.0f);
      g_rows = area_graph::rows_used(g_points.data(), points, height, 3.0f);
      set_image(g_image, LV_COLOR_FORMAT_A8, g_masks.data() + g_rows.area_top * width, width, height - g_rows.area_top, width);
      set_image(g_line, LV_COLOR_FORMAT_A8, g_masks.data() + width * height + g_rows.line_top * width, width, g_rows.line_rows, width);
    }
  }
  lv_draw_image_dsc_t image;
  lv_draw_image_dsc_init(&image);
  image.base.layer = layer;
  image.src = &g_image;
  if (!g_popup) {
    image.recolor = lv_color_hex(kFill);
    image.recolor_opa = LV_OPA_COVER;
  }
  if (g_popup) {
    lv_draw_image(layer, &image, &coords);
  } else {
    const lv_area_t area_at = {coords.x1, coords.y1 + g_rows.area_top, coords.x2, coords.y2};
    lv_draw_image(layer, &image, &area_at);
    image.src = &g_line;
    image.recolor = lv_color_hex(kIcon);
    const lv_area_t line_at = {coords.x1, coords.y1 + g_rows.line_top, coords.x2, coords.y1 + g_rows.line_top + g_rows.line_rows - 1};
    lv_draw_image(layer, &image, &line_at);
  }
}

static uint32_t pixel(const lv_draw_buf_t* buf, int x, int y) {
  const uint8_t* p = buf->data + y * buf->header.stride + x * 4;
  return (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0];
}
static int channel(uint32_t rgb, int c) { return (rgb >> (16 - 8 * c)) & 0xFF; }

static int g_draws = 0;
static void count_cb(lv_event_t*) { ++g_draws; }

int main() {
  lv_init();
  // The round bottom corners in a mask: the corner pixel cut away, the
  // pixels outside the corner bands and the top corners untouched, an
  // anti-aliased edge along the curve.
  {
    const int width = 60, height = 40, radius = 16;
    std::vector<uint8_t> mask(width * height, 0xFF);
    area_graph::round_bottom_corners(mask.data(), width, width, height, radius);
    assert(mask[(height - 1) * width] == 0 && mask[(height - 1) * width + width - 1] == 0 && "corner pixels cut");
    assert(mask[0] == 0xFF && mask[width - 1] == 0xFF && "top corners untouched");
    assert(mask[(height - 1) * width + radius] == 0xFF && mask[(height - radius - 1) * width] == 0xFF &&
           "outside the corner bands untouched");
    int soft = 0;
    for (int x = 0; x < radius; ++x) {
      const uint8_t a = mask[(height - 1) * width + x], b = mask[(height - 1) * width + width - 1 - x];
      assert(a == b && "both corners alike");
      soft += a > 0 && a < 0xFF ? 1 : 0;
    }
    assert(soft >= 1 && "an anti-aliased edge");
    // The cut follows a circle: the diagonal pixel at r(1 - 1/sqrt2) from
    // the corner is about half covered.
    const int d = static_cast<int>(radius * (1.0 - 0.70710678));
    const uint8_t diag = mask[(height - 1 - d) * width + d];
    assert(diag > 0x20 && diag < 0xE0 && "the edge on the corner circle");
  }
  // The fade on exact RGB565 steps: the circle's color down to the middle
  // row, from there fading to the card in the last along a smoothstep curve
  // (no kink, no Mach band where the solid part ends), dithered: every row
  // mixes the two steps around its exact color.
  {
    const int width = 64, height = 160, middle = (height - 1) / 2;
    std::vector<int16_t> tops(width, 0);
    std::vector<uint16_t> fade(width * height);
    area_graph::render_fade(fade.data(), width, width, height, tops.data(), middle, height - 1, kFill, kCard, 255);
    for (int y = 0; y < height; ++y) {
      double sum = 0;
      for (int x = 0; x < width; ++x) sum += (fade[y * width + x] >> 11) * 255.0 / 31;
      const double t = y <= middle ? 0.0 : static_cast<double>(y - middle) / (height - 1 - middle);
      const double alpha = 1.0 - t * t * (3.0 - 2.0 * t);
      // Both ends on the RGB565 steps LVGL shows: red 0x1A -> 3, 0x6E -> 13.
      const double exact = (3 + (13 - 3) * alpha) * 255.0 / 31;
      assert(sum / width > exact - 4.0 && sum / width < exact + 4.0 && "the dither keeps the row's mean");
    }
    // The solid part is the circle's flat color (the RGB565 step LVGL shows
    // for it), no dither texture: a dithered end gave the solid part a
    // texture that vanished in a flat band where the fade passed a step
    // (b563, user 2026-10-27).
    for (int y = 0; y < middle; ++y)
      for (int x = 0; x < width; ++x) assert(fade[y * width + x] == area_graph::rgb565(kFill) && "a flat solid part");
    // No kink where the solid part ends: the first eight fade rows drop far
    // less than eight rows in the fade's middle (a straight fade drops
    // evenly).
    auto row_mean = [&](int y) {
      double sum = 0;
      for (int x = 0; x < width; ++x) sum += (fade[y * width + x] >> 11) * 255.0 / 31;
      return sum / width;
    };
    const int centre = (middle + height - 1) / 2;
    const double start_drop = row_mean(middle) - row_mean(middle + 8);
    const double centre_drop = row_mean(centre - 4) - row_mean(centre + 4);
    if (!(start_drop < centre_drop * 0.5)) std::fprintf(stderr, "fade start drop %.2f centre drop %.2f\n", start_drop, centre_drop);
    assert(start_drop < centre_drop * 0.5 && "the fade starts without a kink");
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
  }

  lv_display_t* display = lv_display_create(320, 220);
  static std::vector<uint32_t> buffer(320 * 220);
  lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size() * 4, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  // A rounded frame with clip_corner draws its chart three times per redraw
  // (bottom band, top band, middle), the two bands into ARGB8888 layers; the
  // tile's frame without it draws the chart once. The data key follows the
  // chart's values, range and size and nothing else.
  {
    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_t* frame = lv_obj_create(screen);
    lv_obj_set_pos(frame, 20, 30);
    lv_obj_set_size(frame, 280, 168);
    lv_obj_set_style_pad_all(frame, 0, 0);
    lv_obj_set_style_border_width(frame, 0, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(frame, 16, 0);
    lv_obj_t* chart = lv_chart_create(frame);
    lv_obj_set_size(chart, 280, 168);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS);
    lv_chart_series_t* series = lv_chart_add_series(chart, lv_color_hex(kIcon), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_point_count(chart, 50);
    for (int i = 0; i < 50; ++i) lv_chart_set_value_by_id(chart, series, i, 20 + (i * 7) % 60);
    lv_obj_add_event_cb(chart, count_cb, static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), nullptr);
    lv_screen_load(screen);
    lv_refr_now(display);
    lv_obj_set_style_clip_corner(frame, true, 0);
    g_draws = 0;
    lv_obj_invalidate(frame);
    lv_refr_now(display);
    assert(g_draws == 3 && "clip_corner draws the chart three times");
    lv_obj_set_style_clip_corner(frame, false, 0);
    g_draws = 0;
    lv_obj_invalidate(frame);
    lv_refr_now(display);
    assert(g_draws == 1 && "without clip_corner the chart draws once");
    const uint32_t key = area_graph::data_key(chart, series, {280, 168, 16});
    assert(key == area_graph::data_key(chart, series, {280, 168, 16}) && "an unchanged chart keeps its key");
    assert(key != area_graph::data_key(chart, series, {281, 168, 16}) && "a new size changes the key");
    lv_chart_set_value_by_id(chart, series, 7, 99);
    const uint32_t changed = area_graph::data_key(chart, series, {280, 168, 16});
    assert(changed != key && "a new value changes the key");
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 200);
    assert(area_graph::data_key(chart, series, {280, 168, 16}) != changed && "a new range changes the key");
    lv_obj_delete(screen);
  }
  for (int mode = 0; mode < 2; ++mode) {
    g_popup = mode == 1;
    g_renders = 0;
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
    // The line is in the pictures.
    lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    g_series = lv_chart_add_series(chart, lv_color_hex(kIcon), LV_CHART_AXIS_PRIMARY_Y);
    // A house load: a base with noise and spikes, a flat stretch and a gap
    // (100 points).
    const int count = 100;
    lv_chart_set_point_count(chart, count);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    unsigned seed = 7;
    for (int i = 0; i < count; ++i) {
      seed = seed * 1103515245u + 12345u;
      int v = 180 + static_cast<int>((seed >> 16) % 60);
      if ((seed >> 8) % 6 == 0) v = 700 + static_cast<int>((seed >> 4) % 280);
      if (i >= 60 && i < 70) v = 140;
      lv_chart_set_value_by_id(chart, g_series, i, i >= 52 && i < 56 ? LV_CHART_POINT_NONE : v);
    }
    lv_obj_add_event_cb(chart, area_cb, static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), nullptr);
    lv_screen_load(screen);
    lv_refr_now(display);
    lv_draw_buf_t* shot = lv_snapshot_take(screen, LV_COLOR_FORMAT_XRGB8888);
    assert(shot);
    lv_area_t coords, content;
    lv_obj_get_coords(chart, &coords);
    lv_obj_get_content_coords(chart, &content);
    const int32_t* values = lv_chart_get_series_y_array(chart, g_series);
    int checked = 0, line_hits = 0;
    for (int i = 0; i < count; ++i) {
      if (values[i] == LV_CHART_POINT_NONE || (i > 0 && values[i - 1] == LV_CHART_POINT_NONE) ||
          (i + 1 < count && values[i + 1] == LV_CHART_POINT_NONE)) continue;
      lv_point_t p;
      lv_chart_get_point_pos_by_id(chart, g_series, i, &p);
      const int x = coords.x1 + p.x, y = coords.y1 + p.y;
      if (x <= coords.x1 + 2 || x >= coords.x2 - 2) continue;
      if (g_popup && std::abs(x - (coords.x1 + kTimeX)) <= 1) continue;
      // The line sits on lv_chart's point.
      assert(channel(pixel(shot, x, y), 0) > 0xC0 && "the line passes through lv_chart's point");
      ++line_hits;
      // Well above it the card, well below it the area. "Above" is above
      // the line in this column and its neighbours: a steep neighbouring
      // segment's soft edge reaches into this column.
      int first_line = coords.y2 + 1, last_line = coords.y1 - 1;
      for (int nx = x - 3; nx <= x + 3; ++nx) {
        for (int y2 = coords.y1; y2 <= coords.y2; ++y2) {
          const int red = channel(pixel(shot, nx, y2), 0);
          if (red > 0x30 && y2 < first_line) first_line = y2;
          if (red > 0xA0) last_line = std::max(last_line, y2);
        }
      }
      const int middle = coords.y1 + (content.y1 - coords.y1 + content.y1 - coords.y1 + lv_area_get_height(&content)) / 2;
      for (int yy = content.y1 + (g_popup ? 1 : 0); yy <= coords.y2; ++yy) {
        const uint32_t q = pixel(shot, x, yy);
        if (yy < first_line - 2) {
          if ((q & 0xF8FCF8) != (kCard & 0xF8FCF8)) std::fprintf(stderr, "above mode=%d x=%d y=%d q=%06x\n", mode, x, yy, (unsigned)q);
          assert((q & 0xF8FCF8) == (kCard & 0xF8FCF8) && "above the line: the card");
        } else if (yy <= last_line + 3) {
          continue;
        } else if (!g_popup) {
          if (q != kFill) std::fprintf(stderr, "below x=%d y=%d q=%06x\n", x, yy, (unsigned)q);
          assert(q == kFill && "below the line: the area");
          ++checked;
        } else if (yy < middle) {
          bool near = true;
          for (int c = 0; c < 3; ++c) near = near && std::abs(channel(q, c) - channel(kFill, c)) <= 10;
          if (!near) std::fprintf(stderr, "solid x=%d y=%d q=%06x\n", x, yy, (unsigned)q);
          assert(near && "the circle's color down to the middle");
          ++checked;
        } else if (yy <= content.y2) {
          assert(channel(q, 0) >= 0x18 && channel(q, 0) <= 0x76 && "the fade below it");
          ++checked;
        }
      }
    }
    assert(line_hits > 40 && checked > 1000);
    // Gap columns show neither line nor area.
    for (int i = 53; i <= 54; ++i) {
      lv_point_t p;
      lv_chart_get_point_pos_by_id(chart, g_series, i, &p);
      for (int yy = content.y1 + 2; yy <= content.y2 - 3; ++yy)
        assert((pixel(shot, coords.x1 + p.x, yy) & 0xF8FCF8) == (kCard & 0xF8FCF8) && "a gap shows the card");
    }
    if (g_popup) {
      // Faint solid lines: 8 % white over the card at the maximum's row and
      // in the time line's column, where the line is far below.
      auto faint = [&](uint32_t q) { const int d = channel(q, 2) - 0x1A; return d >= 10 && d <= 28; };
      int maximum_hits = 0;
      for (int x = coords.x1 + 2; x <= coords.x2 - 2; ++x) maximum_hits += faint(pixel(shot, x, content.y1)) ? 1 : 0;
      assert(maximum_hits > (coords.x2 - coords.x1) / 2 && "a faint solid line at the maximum");
      assert(faint(pixel(shot, coords.x1 + kTimeX, content.y1 + 10)) && "a faint time line");
    }
    // The pictures are computed once; a redraw without a change reuses them.
    assert(g_renders == 1);
    lv_obj_invalidate(chart);
    lv_refr_now(display);
    assert(g_renders == 1 && "an unchanged line keeps its pictures");
    // Drawing the pictures costs a fraction of LVGL's own line (b559: 90 ms
    // per popup frame on the V2).
    auto frame_us = [&]() {
      const auto t0 = std::chrono::steady_clock::now();
      for (int n = 0; n < 20; ++n) {
        lv_obj_invalidate(chart);
        lv_refr_now(display);
      }
      return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 20;
    };
    const double pictures = frame_us();
    g_pictures = false;
    lv_obj_set_style_line_width(chart, g_popup ? 4 : 3, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart, true, LV_PART_ITEMS);
    const double lvgl_line = frame_us();
    g_pictures = true;
    lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS);
    std::fprintf(stderr, "mode %d: pictures %.0f us, LVGL line %.0f us per frame\n", mode, pictures, lvgl_line);
    assert(pictures * 2 < lvgl_line && "the pictures cost far less than LVGL's line");
    lv_chart_set_value_by_id(chart, g_series, 10, 900);
    lv_refr_now(display);
    assert(g_renders == 2 && "a changed line computes its pictures again");
    if (g_popup) {
      // A line running below zero gets a line at zero, on the row where
      // lv_chart puts the value zero (user 2026-10-27); none while the range
      // stays above zero or the row almost touches the minimum's.
      const int32_t top = content.y1 - coords.y1, bottom = top + lv_area_get_height(&content);
      assert(area_graph::zero_row(chart, top, bottom, 8) == -1 && "no zero line above zero");
      lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, -5, 1000);
      assert(area_graph::zero_row(chart, top, bottom, 8) == -1 && "no zero line on the minimum's");
      lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, -500, 1000);
      lv_chart_set_value_by_id(chart, g_series, 0, 0);
      const int32_t zero = area_graph::zero_row(chart, top, bottom, 8);
      lv_point_t at_zero;
      lv_chart_get_point_pos_by_id(chart, g_series, 0, &at_zero);
      assert(zero > top + 8 && zero < bottom - 8 && zero == at_zero.y && "the zero line on lv_chart's zero");
      lv_refr_now(display);
      assert(g_renders == 3);
      lv_draw_buf_t* crossing = lv_snapshot_take(screen, LV_COLOR_FORMAT_XRGB8888);
      assert(crossing);
      // A solid faint line: every column lighter than the row below it.
      const int y = coords.y1 + zero;
      for (const int column : {12, 20, 60}) {
        const int lift = channel(pixel(crossing, coords.x1 + column, y), 2) - channel(pixel(crossing, coords.x1 + column, y + 2), 2);
        if (lift < 8 || lift > 30) std::fprintf(stderr, "zero line column %d lift %d\n", column, lift);
        assert(lift >= 8 && lift <= 30 && "a faint solid line at zero");
      }
      lv_draw_buf_destroy(crossing);
    }
    lv_draw_buf_destroy(shot);
    lv_obj_delete(screen);
  }
  lv_image_cache_drop(&g_image);
  lv_image_cache_drop(&g_line);
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
console.log('sensor graph: tile corners in the masks (one draw pass, no clip_corner layers), masks rebuilt by the chart data key; tile and popup pictures (area, line, faint grid lines) computed once per change, line on lv_chart\'s points, card above, area below, far cheaper than LVGL\'s line');
