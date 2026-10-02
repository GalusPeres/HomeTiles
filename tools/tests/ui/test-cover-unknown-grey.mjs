// Cover state color (user 2026-10-02): an unknown Cover (a template after a
// Home Assistant restart) showed a grey icon but purple sliders, chips and
// buttons in the popup. Home Assistant colors the whole Cover by its state
// (stateColorCss): the cover color for every known state, closed included,
// the inactive grey for unknown and unavailable. Popup, tile bar and Web
// Admin preview follow the same rule.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const popup = read('src/ui/popups/cover/cover_popup.cpp');
const renderer = read('src/types/cover/renderer.cpp');
const admin = read('src/types/cover/admin.js');
const body = (source, start) => {
  const at = source.indexOf(start);
  assert.ok(at >= 0, `missing ${start}`);
  return source.slice(at, source.indexOf('\n}\n', at));
};

// Popup: one state color for icon, slider fill, tracks and buttons.
assert.match(body(popup, 'uint32_t cover_accent('),
  /return cover_icon_is_active\(state\) \? kHaCoverActive : kHaCoverInactive;/);
const apply = body(popup, 'void apply_accent(');
assert.ok(apply.includes('if (!ctx || ctx->accent == accent) return;'), 'only a change restyles');
assert.ok(apply.includes('{ctx->position_slider.track, ctx->tilt_slider.track}') &&
  apply.includes('lv_obj_set_style_bg_color(track, lv_color_hex(accent), 0);'), 'tracks follow the state');
assert.ok(apply.includes('style_mode_buttons(ctx);') && apply.includes('style_action_buttons(ctx);'));
const refresh = body(popup, 'void refresh_popup(');
assert.ok(refresh.includes('apply_accent(ctx, cover_accent(ctx->state));') &&
  refresh.includes('lv_color_hex(ctx->accent)'), 'refresh applies the state color first');
assert.ok(body(popup, 'static void prepare_cover_popup_open(')
  .includes('apply_accent(ctx, cover_accent(init.state));'), 'no purple first frame for an unknown Cover');
assert.ok(popup.includes('fill.bg_color = lv_color_hex(ctx->accent);'), 'position fill');
assert.ok(body(popup, 'void cover_fill(').includes('lv_color_hex(accent), color, opa);'), 'chips and buttons');
assert.ok(body(popup, 'void style_action_buttons(').includes('lv_color_hex(ctx->accent), raised,'));
for (const call of popup.matchAll(/update_preset_group\(([\s\S]*?)\);/g)) {
  if (call[0].startsWith('update_preset_group(lv_obj_t')) continue;
  assert.ok(call[1].trim().endsWith('ctx->accent'), `preset group without the state color: ${call[0]}`);
}

// Tile: the bar fill takes the icon's state color.
assert.match(renderer, /constexpr uint32_t kCoverInactive = 0x9E9E9E;/);
assert.match(body(renderer, 'uint32_t cover_icon_color('),
  /return cover_icon_active\(state\) \? kCoverActive : kCoverInactive;/);
assert.ok(renderer.includes('lv_color_hex(widget->fill_color), card);'));
const view = body(renderer, 'void show_view(GridType grid_type, uint8_t index) {');
assert.ok(view.includes('const uint32_t fill_color = cover_icon_color(state);') &&
  view.includes('widget.fill_color != fill_color') && view.includes('widget.fill_color = fill_color;'));

// Web Admin preview: the same color for the bar.
assert.ok(admin.includes("bar.style.setProperty('--switch-accent', coverPreviewColor(state));"));
assert.ok(!admin.includes('COVER_PREVIEW_ACTIVE'));

console.log('Cover: unknown and unavailable grey in popup, tile bar and preview');
