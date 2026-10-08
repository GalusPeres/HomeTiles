// The head bar's time (home_bar.cpp): a size larger than the head title, and
// on Home without the gear moved right until its digits are as far from the
// screen's right edge as from its top (user 2026-10-08). The Web Admin
// preview follows with the same sizes and places.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const bar = read('src/ui/tabs/tiles/home_bar.cpp');
const layout = read('src/ui/popups/popup_layout.h');
const styles = read('src/web/server/render/web_admin_styles.cpp');
const css = read('src/web/assets/admin.css');

// The title font per profile and the time's, one size up.
const branch = (source, name) => {
  const body = source.slice(source.indexOf(name));
  const small = /DEVICE_LAYOUT_1024X600\)[^\n]*\n\s*return &ui_font_(\d+);/.exec(body)[1];
  const large = /#else\s*\n\s*return &ui_font_(\d+);/.exec(body)[1];
  return {small: Number(small), large: Number(large)};
};
const title = branch(layout, 'headerTitleFont()');
const time = branch(bar, 'const lv_font_t* time_font()');
assert.deepEqual(title, {small: 16, large: 24});
assert.deepEqual(time, {small: 20, large: 32}, 'the time a size larger than the title');
assert.match(styles, /emit_scaled\("head-font", 16\);\s*emit_scaled\("head-time-font", 20\);/);
assert.match(styles, /emit_scaled\("head-font", 24\);\s*emit_scaled\("head-time-font", 32\);/);
assert.match(styles, /emit_scaled\("head-time-line", head\.time_line\);/);

// Its width and line from the time font; without the gear the digits' gap
// to the right edge equals the one to the top, the title wider by as much.
assert.match(bar, /const lv_font_t\* font = time_font\(\);/);
assert.match(bar, /const int ink_top = g\.time_line - font->base_line - digit\.box_h - digit\.ofs_y;/);
assert.match(bar, /const int top_gap = g\.center_y - g\.time_line \/ 2 \+ ink_top;/);
assert.match(bar, /g\.time_x_alone = SCREEN_WIDTH - top_gap \+ right_bearing - g\.time_w;/);
assert.match(styles, /emit_scaled\("head-time-x-alone", head\.time_x_alone\);/);
assert.match(bar, /g\.title_w_alone = g\.title_w \+ g\.time_x_alone - g\.time_x;/);
assert.match(bar, /const bool gear_hidden = home && !configManager\.getConfig\(\)\.head_gear;/);
assert.match(bar, /lv_obj_set_pos\(time_label, gear_hidden \? g\.time_x_alone : g\.time_x,/);
assert.match(bar, /const int title_w = gear_hidden \? g\.title_w_alone : g\.title_w;/);

// The preview: the same font and place (the gear's is-off follows the
// Settings tile option live).
assert.match(css, /font-size:var\(--head-time-font, var\(--head-font\)\);/);
assert.match(css, /\.head-bar-preview:has\(\.head-gear\.is-off\) \.head-time \{\s*left:var\(--head-time-x-alone, var\(--head-time-x\)\);/);
assert.match(css, /\.head-bar-preview:has\(\.head-gear\.is-off\) \.head-title \{\s*width:calc\(var\(--head-title-w\) \+ var\(--head-time-x-alone, var\(--head-time-x\)\) - var\(--head-time-x\)\);/);
assert.match(read('src/web/admin/tiles/layout-window.js'), /'--head-time-x-alone': layoutPx\(layoutHead\.timeAlone \+ shift\)/);

console.log('Head bar: the time a size larger, in the gear\'s place without the gear');
