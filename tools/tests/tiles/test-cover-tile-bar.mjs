// Cover tile (user 2026-10-01, Home Assistant's tile card with the cover
// position feature): half height shows the Sensor compact layout with
// "Open · 58 %"; a full tile of a Cover with a position shows the header
// (title and state left beside the disc) and the position bar with the
// Switch dimmer's logic: the shared level bar (box, drawing, touch mapping),
// Home Assistant's slider timing (command_pacer.h), a 3 s hold after the
// release, set_cover_position commands. A Cover without a position keeps the
// centered state and position.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const renderer = read('src/types/cover/renderer.cpp');
const geometry = read('src/tiles/config/tile_geometry.h');

assert.match(geometry, /inline bool compact_cover\(int type, float w, float h\) \{\s*return type == TILE_COVER && w >= 1 && h == 0\.5f;/);
for (const marker of [
  // Layout choice: compact, header with bar, or the former centered value.
  'const bool compact = tile_geometry::compact_cover(tile.type, tile.span_w, tile.span_h);',
  'const bool positionable = !reported.valid || (reported.supported_features & COVER_FEATURE_SET_POSITION);',
  'compact_sensor_layout::apply(card, widget.icon_label, widget.title_label, widget.state_label, tile);',
  // The shared level bar, like the Switch dimmer.
  'widget.bar = level_bar::create(card, tile);',
  'widget.bar_base = static_cast<int16_t>(level_bar::box(tile).base);',
  'level_bar::draw_fill(lv_event_get_layer(e), widget->bar, widget->level, widget->bar_base,',
  'lv_color_hex(kCoverActive), card);',
  'const uint8_t value = cover_position_at(level_bar::value_at(widget.bar, widget.bar_base, point));',
  // The closed part like Home Assistant (75 % open fills a quarter); fully
  // open keeps the smallest piece with the handle.
  'uint8_t cover_fill_level(uint8_t position) { return position >= 99 ? 1 : static_cast<uint8_t>(100 - position); }',
  'uint8_t cover_position_at(uint8_t level) { return level <= 1 ? 100 : static_cast<uint8_t>(100 - level); }',
  // The shared header with the tall look from 1.5 rows and the value size.
  'const tile_header::Header text = tile_header::create(card, tile, tall, level_bar::box(tile).top);',
  'tile_header::set_state(widget.state_label, line.c_str(), widget.compact ? nullptr : widget.state_font,',
  'level_bar::invalidate_change(widget.bar, widget.bar_base, old_level, fill);',
  // Commands: Home Assistant's slider timing and the final value on release.
  'mqttPublishCoverCommand(entity_id.c_str(), "set_cover_position", value);',
  'const uint32_t wait = g_pacer.wait(millis());',
  'const bool repeat = g_pacer.final_redundant(value);',
  'g_drag.moved = dx * dx + dy * dy >= kDragThreshold * kDragThreshold;',
  'if (changed && g_drag.moved) schedule_live();',
  // Hold: echoes of earlier commands do not move the released bar back.
  'constexpr uint32_t kRemoteBlockMs = 3000;',
  'if (state.available && held_value(grid_type, index, held)) {',
  // A Cover that reports no position control hides the bar.
  'const bool positionable = !state.valid || (state.supported_features & COVER_FEATURE_SET_POSITION);',
  // The state line like Home Assistant: "Open · 58 %".
  'text += " \\xC2\\xB7 ";',
]) assert.ok(renderer.includes(marker), `cover renderer: ${marker}`);
// The fill keeps the cover color open or closed, like the popup sliders.
assert.match(renderer, /constexpr uint32_t kCoverActive = 0x926BC7;/);
// Screensaver tiles draw the bar but take no drag.
assert.ok(renderer.includes('lv_obj_remove_flag(widget.bar, LV_OBJ_FLAG_CLICKABLE);'));
// A deleted card ends a drag that points at it.
assert.match(renderer, /if \(g_drag\.data == data\) \{\s*cancel_live_timer\(\);[\s\S]*?g_drag = CoverDrag\{\};\s*\}\s*delete data;/);

// Web Admin: half-size type, compact classes and the Switch preview's bar.
assert.match(read('src/web/admin/tiles/layout.js'), /\[2, 4, 5, 7, 8, 9, 17, 18, 19, 24, 25, 26\]\.includes\(Number\(type\)\)/);
assert.ok(read('src/web/server/render/web_admin_html.cpp').includes('tile_geometry::compact_cover(tile.type, span_w, span_h)'));
const admin = read('src/types/cover/admin.js');
assert.ok(admin.includes("bar.style.setProperty('--switch-accent', COVER_PREVIEW_ACTIVE);") &&
  admin.includes('drawSwitchPreviewFill(bar);') && admin.includes("const COVER_PREVIEW_ACTIVE = '#926BC7';"));
// The Lock, Alarm panel and Fan bars share the rule (.tile.device.switch-bar).
assert.match(read('src/web/assets/admin.css'), /\.tile\.cover\.switch-bar > \.tile-switch,\s*\.tile\.device\.switch-bar > \.tile-switch \{/);
console.log('Cover tile: compact half height, header with the position bar like the Switch dimmer');
