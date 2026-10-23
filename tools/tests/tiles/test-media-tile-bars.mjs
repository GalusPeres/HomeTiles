// The media tile shows the popup's playing bars over its title (user
// 2026-10-23, mockup build/design-mockups/media-popup-simple/tile-bars.html):
// one drawing for both (playing_bars.h), the bars shown while a song plays
// or pauses, moving only while it plays. Every frame redraws one small area
// (estimated ~4 ms on V2, ~6 ms on S3 from the b158 measurement), so the
// tile's timer skips frames under a popup and while the display sleeps;
// lv_obj_invalidate skips hidden pages and other screens itself. The timer
// goes with the bars like the Pixel animation tile's. Placement and the Web
// Admin preview: test-media-layout-lvgl.mjs, test-media-preview-device.mjs.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).filter(f => f.name === name);
  assert.equal(found.length, 1, name);
  return found[0].source;
};

// One drawing: the popup and the tile call media_bars::draw.
const bars = readRepoFile('src/types/media/playing_bars.h');
assert.match(bars, /inline float level\(uint32_t now, int index, bool playing\)/);
assert.match(bars, /inline void draw\(lv_layer_t\* layer, const lv_area_t& area, int bar_width, int bar_gap, lv_color_t color,\s*bool playing\)/);
const popup = readRepoFile('src/ui/popups/media/media_popup.cpp');
assert.ok(!/float bar_level\(/.test(popup), 'the popup keeps no copy of the bars');
assert.match(fn(popup, 'bars_draw_cb'), /media_bars::draw\(layer, area, ml::kBarWidth, ml::kBarGap, lv_obj_get_style_bg_color\(obj, LV_PART_MAIN\), playing\);/);

// The tile's bars, their color and their timer.
const renderer = readRepoFile('src/types/media/renderer.cpp');
const create = fn(renderer, 'create_media_bars');
assert.match(create, /lv_obj_remove_flag\(bars, LV_OBJ_FLAG_CLICKABLE\);/, 'a press goes to the tile');
assert.match(create, /lv_obj_set_size\(bars, 4 \* media_tile::kBarWidth \+ 3 \* media_tile::kBarGap, media_tile::kBarsHeight\);/);
assert.match(create, /lv_obj_add_flag\(bars, LV_OBJ_FLAG_HIDDEN\);/);
assert.match(create, /lv_obj_add_event_cb\(bars, media_bars_draw_cb, LV_EVENT_DRAW_MAIN, icon_label\);/);
assert.match(create, /lv_timer_t\* timer = lv_timer_create\(media_bars_timer_cb, media_tile::kBarsPeriodMs, bars\);\s*lv_obj_add_event_cb\(bars, media_bars_delete_cb, LV_EVENT_DELETE, timer\);/);
assert.match(fn(renderer, 'media_bars_delete_cb'), /if \(timer\) lv_timer_delete\(timer\);/);
assert.match(fn(renderer, 'media_bars_draw_cb'), /icon \? lv_obj_get_style_text_color\(icon, LV_PART_MAIN\) : lv_color_white\(\)/,
  'the tile icon\'s color, as the popup takes it');
assert.match(fn(renderer, 'media_bars_draw_cb'), /lv_obj_has_state\(bars, LV_STATE_CHECKED\)\);/);
const timer = fn(renderer, 'media_bars_timer_cb');
assert.match(timer, /if \(!bars \|\| !lv_obj_has_state\(bars, LV_STATE_CHECKED\) \|\| lv_obj_has_flag\(bars, LV_OBJ_FLAG_HIDDEN\)\) return;/);
assert.match(timer, /if \(popup_shell_active\(\) \|\| powerManager\.isInSleep\(\)\) return;\s*lv_obj_invalidate\(bars\);/);
const render = fn(renderer, 'render_media_tile');
assert.match(render, /lv_obj_t\* bars = create_media_bars\(card, icon_label\);/);
assert.match(render, /target\[index\]\.bars = bars;/);

// The state shows them and marks the playing.
const update = fn(readRepoFile('src/tiles/runtime/tile_renderer.cpp'), 'update_media_tile_state');
assert.match(update, /const bool bars_shown = playing \|\| state\.equalsIgnoreCase\("paused"\);/);
assert.match(update, /text_layout_dirty = true;/);
assert.match(update, /if \(playing\) lv_obj_add_state\(widgets\.bars, LV_STATE_CHECKED\);\s*else lv_obj_remove_state\(widgets\.bars, LV_STATE_CHECKED\);\s*lv_obj_invalidate\(widgets\.bars\);/);

// The block: bars, gap, title, subtitle, centred like before.
const layout = readRepoFile('src/types/media/content_layout.cpp');
assert.match(layout, /const int bars_height = has_bars \? media_tile::kBarsHeight \+ media_tile::kBarsGap : 0;/);
assert.match(layout, /if \(has_bars\) lv_obj_align\(widgets\.bars, LV_ALIGN_TOP_LEFT, text_x, block_y\);\s*const int title_y = block_y \+ bars_height;/);
const tileLayout = readRepoFile('src/types/media/tile_layout.h');
for (const [name, value] of [['kBarsHeight', 36], ['kBarWidth', 8], ['kBarGap', 5], ['kBarsGap', 10]]) {
  assert.match(tileLayout, new RegExp(`constexpr lv_coord_t ${name} = tile_layout::scale\\(${value}\\);`), name);
}
assert.match(tileLayout, /constexpr uint32_t kBarsPeriodMs = 40;/);
// The widgets stay copyable with memcpy (test-media-position.mjs).
assert.match(readRepoFile('src/types/media/widgets.h'), /lv_obj_t\* bars = nullptr;/);
console.log('Media tile bars: the popup\'s drawing over the title, moving only while it plays and is seen');
