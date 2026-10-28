// A page turn to a full Home built up visibly (user 2026-10-28: "wir müssen
// halt messen"): every large frame logs what each tile cost to draw, with
// the normal draw band. Checks the wiring: every rendered tile card is
// watched, only frames covering two fifths of the screen are measured, the
// line is rate-limited and English, and nothing is allocated per frame.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const probe = read('src/core/diagnostics/tile_frame_probe.cpp');
const renderer = read('src/tiles/runtime/tile_renderer.cpp');

// After the card's kept corners are attached (rounded_corners.h), so the probe
// times them with the rest.
assert.match(renderer, /desc->render\(parent, col, row, tile, index, grid_type, scene_cb\);[\s\S]{0,900}?rounded_corners::attach_tree\(tile_obj\);[\s\S]{0,900}?tile_frame_probe::watch\(tile_obj, index, static_cast<uint8_t>\(tile\.type\)\);/,
  'render_tile watches every tile card it renders');
assert.ok(probe.includes('constexpr uint32_t kFrameSharePercent = 40;') &&
  probe.includes('g_measuring = screen && pixels * 100U >= screen * kFrameSharePercent;'),
  'only frames covering two fifths of the screen are measured');
assert.ok(probe.includes('if (!g_measuring) return;'), 'small frames cost one check');
assert.ok(probe.includes('constexpr uint32_t kLogIntervalMs = 1000;') &&
  probe.includes('if (g_last_log_ms && now - g_last_log_ms < kLogIntervalMs) return;'), 'at most one line per second');
assert.ok(probe.includes('"[FrameProbe] frame %.1f ms px=%lu flushes=%lu flush=%.1f ms tiles=%.1f ms rest=%.1f ms |"'),
  'one English line with the frame, its flushes and the tiles');
// The parts inside the tiles (user 2026-10-28: which part costs the time):
// every element's own drawing summed by kind, a second line per frame.
assert.ok(probe.includes('watch_part(card, kCard);') && probe.includes('watch_children(card);'),
  'the card and all its elements are watched');
assert.ok(probe.includes('lv_obj_add_event_cb(obj, part_cb, LV_EVENT_DRAW_MAIN_END, data);'),
  'an element counts its own drawing, its children apart');
assert.ok(probe.includes('"[FrameProbe] parts"') && probe.includes('" %s=%.1f ms/%lu"'));
assert.ok(probe.includes('memset(g_part_us, 0, sizeof(g_part_us));'), 'the parts start over per frame');
// What came from slow memory (user 2026-10-28: "if too much comes from PSRAM
// it gets slow"): on the P4 the chip's cache counters start with a measured
// frame, are read before anything is logged and give a third line with the
// draw band's place and the L1/L2 misses and PSRAM/flash reads.
assert.match(probe, /#if defined\(CONFIG_IDF_TARGET_ESP32P4\)\s*g_frame_areas = g_display->inv_p;\s*cache_counters_start\(\);\s*#endif\s*return;/,
  'the counters start with a measured frame, its areas are kept');
// The band's rows come from its size: LVGL reshapes buf_act's header to each
// band (b586 showed the last band, 800 = 28 x 28 + 16, as "16 lines").
assert.ok(probe.includes('band->data_size / row_bytes') && !probe.includes('band->header.h'),
  'the band rows are the buffer size over a full screen row');
assert.ok(probe.indexOf('cache_counters_read();') < probe.indexOf('log_frame(frame_us);') &&
  probe.indexOf('log_frame(frame_us);') < probe.indexOf('log_cache();'), 'read before the lines are logged');
assert.ok(probe.includes('REG_WRITE(CACHE_L1_CACHE_ACS_CNT_CTRL_REG, kL1Counters | kL1Clear);') &&
  probe.includes('REG_WRITE(CACHE_L2_CACHE_ACS_CNT_CTRL_REG, kL2Counters | kL2Clear);'), 'L1 and L2 counters cleared');
assert.ok(probe.includes('CACHE_L2_DBUS1_ACS_NXTLVL_RD_CNT_REG') && probe.includes('CACHE_L2_DBUS1_ACS_NXTLVL_WR_CNT_REG'),
  'reads and writes of PSRAM/flash are counted');
assert.ok(probe.includes('"[FrameProbe] cache band=%s %lu lines, %lu areas |') && probe.includes('esp_ptr_external_ram(band->data)'),
  'the cache line names where the draw band lives');
// What inside a tile costs (user 2026-10-28): the texts by font (line height,
// compressed or not) and the slowest tile card's style parts, styles and
// event handlers, logged with the frame and started over per frame.
assert.ok(probe.includes('lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));') &&
  probe.includes('if (part == kLabel) note_font(obj, us);'), 'every label piece counts for its font');
assert.ok(probe.includes('"[FrameProbe] fonts"') && probe.includes('" h%ld%c=%.1f ms/%lu"'));
assert.ok(probe.includes('dsc->bitmap_format != LV_FONT_FMT_TXT_PLAIN ? \'z\' : \'p\''), 'compressed fonts are marked');
// The icon fonts unpack through the glyph cache (b591): marked 'c', and the
// fonts line tells how many pieces came from the cache in this frame.
assert.ok(probe.includes('reinterpret_cast<const void*>(&glyph_cache_bitmap)) {\n    return \'c\';'), 'cached fonts are marked');
assert.ok(probe.includes('" | glyph cache hits=%lu unpacked=%lu clears=%lu kept=%lu (%lu KB)%s"') &&
  probe.includes('g_glyphs_start = glyph_cache_stats();'), 'the cache counts start with the frame');
// What resolving the slowest card's styles costs per piece, timed after the
// frame, not inside it.
assert.ok(probe.includes('lv_obj_init_draw_rect_dsc(card, LV_PART_MAIN, &rect);') && probe.includes('rect_dsc=%.1f us'),
  'the card line has the style cost per piece');
assert.ok(probe.indexOf('lv_obj_init_draw_rect_dsc(card') > probe.indexOf('void log_frame('),
  'timed while logging, after the frame');
// The renderer's pixel work by tile part and task kind against the time
// around it (b592): the one software draw unit's dispatch is timed with the
// cycle counter, only in measured frames, and the line splits the frame into
// pixels, widgets and flush.
assert.ok(probe.includes('g_sw_dispatch = unit->dispatch_cb;\n  unit->dispatch_cb = timed_dispatch;') &&
  probe.includes('if (g_sw_dispatch || !unit || unit->next || !unit->dispatch_cb) {'),
  'only the single software draw unit is wrapped, once');
assert.ok(probe.includes('if (!g_measuring || g_lite) return g_sw_dispatch(unit, layer);'), 'unmeasured frames pass straight through');
// b595: only frames that will be logged are measured, every other one lite
// (frame time only, no element or task timing: what the probe itself costs),
// and a full frame lists its most expensive single elements.
assert.ok(probe.includes('if (g_last_log_ms && start_ms - g_last_log_ms < kLogIntervalMs) g_measuring = false;') &&
  probe.includes('g_lite = !g_lite;'), 'logged frames only, lite and full in turn');
assert.ok(probe.includes('if (!g_measuring || g_lite) return;') && probe.includes('| lite (no element timing)'),
  'a lite frame times nothing inside');
assert.ok(probe.includes('"[FrameProbe] top elements"') && probe.includes('note_element(obj, static_cast<uint8_t>(part), us, g_task_cycles_total - g_element_start_cycles);'),
  'the top elements with the pixel work inside them');
assert.ok(probe.includes('memset(g_elements, 0, sizeof(g_elements));'), 'the elements start over per frame');
assert.ok(probe.indexOf('task_kind(task)') < probe.indexOf('const uint32_t start = esp_cpu_get_cycle_count();'),
  'the task is classified before the timed draw');
assert.ok(probe.includes('" | pixels=%.1f ms widgets=%.1f ms flush=%.1f ms"') && probe.includes('memset(g_tasks, 0, sizeof(g_tasks));'),
  'the split starts over per frame');
// The fills and lines drawn from kept corners in the frame (b593, rounded_corners.h).
assert.ok(probe.includes('" | corners fills=%lu borders=%lu sets=%lu (%lu KB) built=%lu declined=%lu"') &&
  probe.includes('g_corners_start = rounded_corners::stats();'), 'the corner counts start with the frame');
assert.ok(probe.includes('"[FrameProbe] card t%d radius=%ld bg_opa=%u grad=%u outline=%ld/%u border=%ld/%u post=%u shadow=%ld image=%u"'));
assert.ok(probe.includes('memset(g_fonts, 0, sizeof(g_fonts));'), 'the fonts start over per frame');
// The styles per drawn piece by kind (user 2026-10-28: the clock's see-through
// card cost 0.35 ms per piece with 17 styles).
assert.ok(probe.includes('g_part_styles[part] += obj->style_cnt;') && probe.includes('"[FrameProbe] styles per piece"'));
assert.ok(probe.includes('memset(g_part_styles, 0, sizeof(g_part_styles));'), 'the styles start over per frame');
for (const code of ['LV_EVENT_DRAW_MAIN_BEGIN', 'LV_EVENT_DRAW_POST_END', 'LV_EVENT_REFR_START', 'LV_EVENT_FLUSH_START',
  'LV_EVENT_FLUSH_FINISH', 'LV_EVENT_REFR_READY']) {
  assert.ok(probe.includes(code), code);
}
const code = probe.replace(/\/\/[^\n]*/g, '');
assert.doesNotMatch(code, /\bnew\b|malloc|String\b|std::vector/, 'no allocation per frame');
console.log('Tile frame probe: large frames log their per-tile draw time');
