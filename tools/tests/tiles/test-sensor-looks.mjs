// The Sensor tile's looks below the header, decided in the gauge lab
// (build/design-mockups/gauge-media/lab.html, user 2026-10-28;
// build/analysis/2026-10-28-analyse.md): levels like the Switch bar, stripes
// standing and lying, arcs and rings plain and of stripes. Display modes are
// appended (3-14) so stored tiles keep their meaning; the Web Admin offers
// them in every language; the panel draws them from A8 masks computed again
// only on a change; the preview computes the same arc fit as the panel.
// Renders the real geometry (sensor_look.h) on the host: every shape stays
// inside the box and its corner curves with the lab's margins, the stripes
// reach both edges, a level is exactly as long as the value, the arc's
// opening is chosen between 40 and 300 degrees, and the preview's port
// (admin-look.js) fits the same arcs.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');

// Stable ids: 0-2 as before, the looks appended, the clamp follows them.
const look = read('src/types/sensor/sensor_look.h');
const ids = {kNone: 0, kGauge: 1, kGraph: 2, kLevelSide: 3, kLevelUp: 4, kStripes: 5, kStripesUp: 6, kArc: 7,
  kArcValue: 8, kTickArc: 9, kTickArcValue: 10, kRing: 11, kTickRing: 12, kRingHead: 13, kTickRingHead: 14};
for (const [name, id] of Object.entries(ids)) {
  assert.match(look, new RegExp(`\\b${name} = ${id},`), `display mode ${name} = ${id}`);
}
assert.ok(look.includes('inline constexpr uint8_t kLastMode = kTickRingHead;'), 'the last look');
const config = read('src/tiles/config/tile_config.cpp');
assert.equal((config.match(/<= sensor_look::kLastMode\) \? in\./g) || []).length, 3, 'pack and unpack keep every look');
assert.ok(read('src/types/sensor/web_handler.cpp').includes('if (mode >= 0 && mode <= sensor_look::kLastMode) tile.sensor_display_mode'),
  'the request handler takes every look');

// The menu like the Switch's (mockup build/design-mockups/sensor-menu, user
// 2026-10-28): segmented choices instead of one long list, their words in
// every language, the choices mapped onto the stored display mode.
const i18nH = read('src/core/i18n/i18n.h');
const fields = ['sensor_layout_value', 'sensor_layout_bar', 'sensor_layout_ring', 'sensor_form', 'sensor_form_level',
  'sensor_form_line', 'sensor_form_stripes', 'sensor_ring_open', 'sensor_ring_closed', 'sensor_value_top',
  'sensor_value_inside', 'sensor_gauge_range'];
for (const field of fields) assert.ok(i18nH.includes(`  const char* ${field};`), field);
assert.ok(!i18nH.includes('sensor_display_level_side'), 'the long list is gone');
const i18n = read('src/core/i18n/i18n.cpp');
const labels = {
  de: ['"Graph",', '"Wert",', '"Min / Max",'], en: ['"Graph",', '"Value",', '"Min / Max",'],
  fr: ['"Graphique",', '"Valeur",', '"Min / Max",'], pl: ['"Wykres",', '"Wartość",', '"Min / Maks",'],
  cs: ['"Graf",', '"Hodnota",', '"Min / Max",'], es: ['"Gráfico",', '"Valor",', '"Mín / Máx",'],
  sk: ['"Graf",', '"Hodnota",', '"Min / Max",'],
};
for (const [language, [graph, first, last]] of Object.entries(labels)) {
  const table = i18n.slice(i18n.indexOf(`kStrings${language[0].toUpperCase()}${language[1]} = {`));
  const lines = table.split(/\r?\n/).map(line => line.trim());
  const at = lines.indexOf(graph);
  assert.ok(at > 0 && lines[at + 1] === first && lines[at + 12] === last, `${language}: the twelve menu words right after Graph`);
}
const html = read('src/types/sensor/web_html.cpp');
for (const field of ['sensor_layout', 'sensor_form', 'sensor_orientation', 'sensor_ring', 'sensor_place', 'sensor_value_font',
  'sensor_popup_open_mode']) {
  assert.ok(html.includes(`append_switch_choice(html, tab_id, "${field}",`), `${field}: a segmented choice`);
}
assert.ok(html.includes('_sensor_display_mode">)html";') && html.includes('for (int mode = 0; mode <= sensor_look::kLastMode; ++mode)') &&
  html.includes('<div class="sensor-range">') && html.includes('_sensor_gauge_shape_fields'),
  'the stored mode in a hidden select, Min and Max in one row, the gauge shape in its own block');
const editor = read('src/types/sensor/admin-editor.js');
assert.ok(editor.includes("row('sensor_orientation', bar);") && editor.includes("row('sensor_ring', ring);") &&
  editor.includes("if (gaugeOption) gaugeOption.hidden = layout.dataset?.gauge !== '1' && !gauge;") &&
  editor.includes("if (shapeWrap) shapeWrap.classList.toggle('hidden', !gauge);"),
  'only the rows the layout uses; the old gauge only on a tile that has it');
assert.ok(read('src/types/switch/admin.js').includes("'sensor_layout', 'sensor_form',") &&
  read('src/web/admin/tiles/editor.js').includes("bindLive(document.getElementById(prefix + '_' + field), 'change', 'sensorChoice_' + field, () => applySensorLayoutChoices(tab));") &&
  read('src/web/assets/admin.css').includes('.type-fields.show > .switch-choices.hidden { display:none; }'),
  'the choices are synced, bound and hidden like the Switch rows');
// Every display mode survives the choices and back.
const ctx = {Number, Object, String, Array};
vm.createContext(ctx);
vm.runInContext(editor.slice(editor.indexOf('  function sensorModeFromChoices'), editor.indexOf('  // Shows a stored mode in the choices')) +
  '\nthis.toMode = sensorModeFromChoices; this.toChoices = sensorChoicesFromMode;', ctx);
for (let mode = 0; mode <= 14; ++mode) assert.equal(ctx.toMode(ctx.toChoices(mode)), mode, `mode ${mode} round trip`);
assert.equal(ctx.toMode({layout: 3, form: 0, orientation: 0, ring: 1, place: 0}), 13, 'a closed ring with the value in the head');
assert.ok(read('src/web/admin/bundle.json').includes('"src/types/sensor/admin-look.js"'), 'the preview script is bundled');
const editorJs = read('src/web/admin/tiles/editor.js');
assert.ok(editorJs.includes("bindLive(gaugeMinInput, 'input', 'sensorGaugeMin', () => { updateTilePreview(tab);") &&
  editorJs.includes("bindLive(gaugeMaxInput, 'input', 'sensorGaugeMax', () => { updateTilePreview(tab);"),
  'a new range redraws the preview');

// Panel: the look over the graph's box, the header beside the disc or the
// title alone, the value updated through the widgets; masks rebuilt only on
// a change, drawn recolored, freed with the object.
const renderer = read('src/types/sensor/renderer.cpp');
assert.ok(renderer.includes('look = sensor_look_view::create(card, tile, icon_lbl, box, gauge_min, gauge_max);') &&
  renderer.includes('title_label = create_title_head(card, tile);') &&
  renderer.includes('tile.span_h > 1 ? tile_layout::content_font_24() : tile_layout::content_font_20()') &&
  renderer.includes('target[index].look = look;'), 'renderer: the look, its head and its value');
const tileRenderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.ok(tileRenderer.includes('sensor_look_view::set_value(look, numeric);') &&
  tileRenderer.includes('if (target[grid_index].look) sensor_look_view::sync_color(target[grid_index].look);') &&
  tileRenderer.includes('target[i].look = nullptr;'), 'state updates reach the look, resets clear it');
assert.ok(read('src/types/energy/renderer.cpp').includes('target[index].look = nullptr;'), 'the Energy tile clears it too');
const view = read('src/types/sensor/look_view.cpp');
assert.ok(view.includes('if (!view->built || key != view->key) {') && view.includes('lv_draw_image(layer, &image, &at);') &&
  view.includes('lv_image_cache_drop(&view.groups[i].image);') && view.includes('lv_obj_add_event_cb(frame, delete_cb, LV_EVENT_DELETE, view);') &&
  !view.includes('clip_corner') && !view.includes('lv_draw_line') && !view.includes('lv_draw_arc'),
  'look view: masks rebuilt by key, drawn as images, freed with the object, no layers or LVGL shapes');
assert.ok(view.includes('auto* found = static_cast<Found*>(lv_malloc(sizeof(Found) * kMaxGroups));'),
  'look view: the plan in the LVGL heap, not on the draw stack');
assert.ok(read('src/types/sensor/widgets.h').includes('  lv_obj_t* look = nullptr;'), 'the widgets keep the look');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: Sensor looks need a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/sensor-looks');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include "src/types/sensor/sensor_look.h"
using namespace sensor_look;

// The V2's boxes (tile 194 x 152 cells, gap 16, inset 6, content top 78,
// radius 25): 1x1, 1.5x1, 2x1, 1x1.5, 1.5x1.5, 2x2.
struct Size { float w, h; };
static const Size kSizes[] = {{182, 62}, {279, 62}, {376, 62}, {182, 146}, {279, 146}, {376, 230}};

static std::vector<float> raster(const Spec& spec, int want_layer, int want_lit) {
  const int w = static_cast<int>(spec.box.w), h = static_cast<int>(spec.box.h);
  std::vector<float> cov(w * h, 0.0f);
  emit_look(spec, [&](const Element& e) {
    if (want_layer >= 0 && e.layer != want_layer) return;
    if (want_lit >= 0 && e.lit != (want_lit == 1)) return;
    for (int y = e.y0; y <= e.y1; ++y)
      for (int x = e.x0; x <= e.x1; ++x)
        cov[y * w + x] = std::max(cov[y * w + x], coverage(e, spec.box, x + 0.5f, y + 0.5f));
  });
  return cov;
}

// Every covered pixel lies inside the box's rounded shape (with m margin).
static void inside_box(const Spec& spec, const std::vector<float>& cov, float m) {
  const int w = static_cast<int>(spec.box.w), h = static_cast<int>(spec.box.h);
  const float r = spec.box.radius;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      if (cov[y * w + x] < 0.02f) continue;
      // Lying stripes: the box is straight on top.
      const float top = spec.mode == kStripesUp ? 0.0f : r;
      const float d = round_rect_distance(x + 0.5f, y + 0.5f, 0, 0, spec.box.w, spec.box.h, top, top, r, r);
      if (d > -m + 0.75f) std::fprintf(stderr, "mode %d out of box at %d,%d d=%.2f\n", spec.mode, x, y, d);
      assert(d <= -m + 0.75f && "inside the box shape");
    }
}

int main() {
  Metrics metrics;
  // Stripes: 6 px wide, at least 5 apart, the first and last on the box's
  // edges, round ends inside the box and its corner curves, lit up to the
  // value; lying stripes stack from the bottom in a box flat on top.
  for (const Size size : kSizes) {
    for (const bool up : {false, true}) {
      Spec spec;
      spec.mode = up ? kStripesUp : kStripes;
      spec.box = make_box(size.w, size.h, 25);
      spec.fraction = 0.62f;
      const auto all = raster(spec, -1, -1);
      inside_box(spec, all, 0);
      const int w = static_cast<int>(size.w), h = static_cast<int>(size.h);
      const float U = up ? size.h : size.w;
      const int n = static_cast<int>(std::floor((U + 5) / 11));
      int lit = 0, total = 0;
      emit_look(spec, [&](const Element& e) { ++total; lit += e.lit ? 1 : 0; });
      assert(total == n && lit == static_cast<int>(std::lround(n * 0.62f)));
      // Both edges reached: the middle row (or column) is covered at both ends.
      if (!up) assert(all[(h / 2) * w + 1] > 0.9f && all[(h / 2) * w + w - 2] > 0.9f);
      else assert(all[(h - 2) * w + w / 2] > 0.9f && all[1 * w + w / 2] > 0.9f);
      // Lying stripes: the top is straight (the top stripe covers its row
      // close to both sides), the bottom corners round.
      if (up) assert(all[2 * w + 2] > 0.5f && all[(h - 1) * w] < 0.02f);
      if (!up) assert(all[0] < 0.02f && all[(h - 1) * w + w - 1] < 0.02f);
    }
  }
  // A level: the track is the box, the fill exactly as long as the value
  // with its end rounded, inside the box; none at 0.
  for (const Size size : kSizes) {
    for (const bool up : {false, true}) {
      Spec spec;
      spec.mode = up ? kLevelUp : kLevelSide;
      spec.box = make_box(size.w, size.h, 25);
      spec.metrics.end = 17;
      spec.fraction = 0;
      int fills = 0;
      emit_look(spec, [&](const Element& e) { fills += e.lit ? 1 : 0; });
      assert(fills == 0 && "an empty level has no fill");
      spec.fraction = 0.5f;
      const auto fill = raster(spec, 1, 1);
      inside_box(spec, fill, 0);
      const int w = static_cast<int>(size.w), h = static_cast<int>(size.h);
      float length = 0;
      if (!up) for (int x = 0; x < w; ++x) length += fill[(h / 2) * w + x];
      else for (int y = 0; y < h; ++y) length += fill[y * w + w / 2];
      const float want = (up ? size.h : size.w) * 0.5f;
      if (std::fabs(length - want) > 1.0f) std::fprintf(stderr, "level length %.2f want %.2f\n", length, want);
      assert(std::fabs(length - want) <= 1.0f && "the fill is as long as the value");
    }
  }
  // Arcs: the centre line sw/2 inside the box and its corners, the opening
  // between 40 and 300 degrees, flat on one-row tiles, large on taller ones;
  // the value's top at the middle of the inside.
  std::printf("[");
  bool first = true;
  for (const Size size : kSizes) {
    for (const float sw : {12.0f, 16.0f}) {
      const Box box = make_box(size.w, size.h, 25);
      const Arc arc = auto_arc(box, sw);
      assert(arc.ok);
      const float degrees = 2 * arc.half * 180 / kPi;
      assert(degrees >= 39.9f && degrees <= 300.1f);
      for (int k = 0; k <= 200; ++k) {
        const float a = -arc.half + 2 * arc.half * k / 200;
        float top = 0, bottom = 0;
        const float x = arc.cx + arc.r * std::sin(a), y = arc.cy - arc.r * std::cos(a);
        assert(column(box, sw * 0.5f - 0.01f, x, top, bottom) && y >= top - 0.05f && y <= bottom + 0.05f);
      }
      const float value_top = arc_value_top(arc);
      assert(value_top > arc.cy - arc.r + sw * 0.5f - 0.01f);
      if (size.h < 100) assert(degrees <= 200 && "a one-row tile takes a flat arc");
      std::printf("%s{\"w\":%g,\"h\":%g,\"sw\":%g,\"r\":%.4f,\"cy\":%.4f,\"half\":%.5f}", first ? "" : ",", size.w,
                  size.h, sw, arc.r, arc.cy, arc.half);
      first = false;
      // The drawn arc and its stripes stay inside the box.
      for (const uint8_t mode : {kArc, kTickArcValue}) {
        Spec spec;
        spec.mode = mode;
        spec.box = box;
        spec.metrics = metrics;
        spec.arc = look_arc(mode, box, metrics);
        spec.fraction = 0.7f;
        inside_box(spec, raster(spec, -1, -1), 0);
      }
    }
  }
  std::printf("]\n");
  // Rings: as large as the box's shorter side, centred; a rule's places come
  // in pieces with round ends at the start and the value.
  {
    const Box box = make_box(182, 146, 25);
    const Arc ring_arc = ring(box, 12);
    assert(std::fabs(2 * ring_arc.r + 12 - 146) < 0.01f && std::fabs(ring_arc.cx - 91) < 0.01f);
    Spec spec;
    spec.mode = kRing;
    spec.box = box;
    spec.arc = ring_arc;
    spec.fraction = 0.4f;
    spec.places = true;
    int pieces = 0, discs = 0;
    emit_look(spec, [&](const Element& e) {
      pieces += e.shape == Shape::ArcSlice ? 1 : 0;
      discs += e.shape == Shape::Disc ? 1 : 0;
    });
    assert(pieces == kRulePieces && discs == 2 && "closed ring: pieces, a start and a value end");
    assert(std::fabs(value_top(kRing, ring_arc, 24) - (ring_arc.cy - 14.4f)) < 0.01f && "the value centred in a ring");
    // A ring of stripes: evenly around, 11 px apart, none doubled at the top.
    spec.mode = kTickRing;
    spec.arc = ring(box, 16);
    spec.places = false;
    int ticks = 0;
    emit_look(spec, [&](const Element&) { ++ticks; });
    assert(ticks == static_cast<int>(std::floor(2 * kPi * spec.arc.r / 11)));
  }
  return 0;
}
`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, 'test' + (process.platform === 'win32' ? '.exe' : ''));
const built = spawnSync(host.cxx, ['-std=c++17', '-O1', '-I', root, source, '-o', binary], {encoding: 'utf8'});
assert.equal(built.status, 0, built.stdout + built.stderr);
const run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
const fits = JSON.parse(run.stdout.trim());

// The preview's port fits the same arcs (admin-look.js runs in the browser).
const context = {Math, Number, String, Array, document: undefined};
vm.createContext(context);
vm.runInContext(read('src/types/sensor/admin-look.js') +
  '\nthis.fit = sensorLookAutoArc; this.valueTop = sensorLookValueTop; this.inside = sensorLookValueInside; this.isLook = isSensorLookMode;', context);
for (const fit of fits) {
  const box = {w: fit.w, h: fit.h, r: Math.min(25, fit.w / 2, fit.h / 2)};
  const arc = context.fit(box, fit.sw);
  assert.ok(Math.abs(arc.r - fit.r) < 0.05 && Math.abs(arc.cy - fit.cy) < 0.05 && Math.abs(arc.half - fit.half) < 1e-4,
    `preview arc ${fit.w}x${fit.h} sw ${fit.sw}: ${JSON.stringify(arc)} against ${JSON.stringify(fit)}`);
}
assert.ok([8, 10, 11, 12].every(m => context.inside(m)) && ![3, 4, 5, 6, 7, 9].some(m => context.inside(m)),
  'preview: the value inside for B, BS/SM, G and GS');
assert.ok(context.isLook(3) && context.isLook(14) && !context.isLook(2) && !context.isLook(15), 'preview: the look modes');
console.log(`sensor looks: modes 3-14 stored, chosen like the Switch in 7 languages; stripes, levels, arcs and rings inside the box; ${fits.length} arc fits equal in the preview`);
