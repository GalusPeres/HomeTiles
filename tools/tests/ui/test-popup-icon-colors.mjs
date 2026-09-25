// Popups take the tile's per-tile icon color for their header icon, like the
// tile icon: Sensor (color bar or state colors, live), Binary sensor (On/Off
// colors on the raw state or its label, else the state color), Number/Select/
// Date/Time (tile known-state rule with the displayed text), Energy (color for
// the state at opening) and Camera (fixed color). The shell copies the icon
// color, so the header disc glow and the popup border follow.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const popup = read('src/ui/popups/sensor/sensor_popup.cpp');
for (const marker of [
  'static void apply_popup_icon_color(SensorPopupContext* ctx, bool known, const char* state,',
  'tile_icon_colors::resolve(ctx->icon_colors.c_str(), state, display, rgb);',
  'const bool known = value.valid && value.has_state && value.available && value.state != "unknown";',
  'ctx->icon_colors = init.icon_colors;',
  'const bool known = available && (state == "on" || state == "off");',
  'lv_color_hex(binary_state_color(binary_state_code(state, available))));',
]) assert.ok(popup.includes(marker), `sensor popup: ${marker}`);
// Every state path applies the color: opening, live Sensor values, Binary
// states and editable value changes.
assert.match(popup, /set_label_text_if_changed\(ctx->value_label, display\.c_str\(\)\);\s*if \(!ctx->binary_mode && !ctx->editable\) \{\s*apply_popup_icon_color\(ctx, popup_icon_state_known\(value\), value\.c_str\(\), nullptr,/);
assert.equal((popup.match(/apply_editable_icon_color\(ctx, (?:value|editable_value)\);/g) || []).length, 3,
  'Editable values color the icon when opening, rebinding and updating');
assert.match(popup, /init\.state_history_mode\)\.c_str\(\)\);\s*apply_popup_icon_color\(ctx, popup_icon_state_known\(init\.value\)/);
assert.ok(read('src/ui/popups/sensor/sensor_popup.h').includes('  String icon_colors;\n};'));
// Tiles hand over their record or color.
const sensor = read('src/types/sensor/renderer.cpp');
assert.ok(sensor.includes('tileTypeIsEditableValue(tile.type),\n      tile.icon_colors\n    };') &&
  sensor.includes('init.icon_colors = data->icon_colors;'));
assert.ok(read('src/types/binary_sensor/renderer.cpp').includes('init.icon_colors = tile->icon_colors;'));
assert.ok(read('src/ui/popups/binary_sensor/binary_sensor_popup.cpp').includes('sensor_init.icon_colors = init.icon_colors;'));
const energy = read('src/types/energy/renderer.cpp');
assert.ok(energy.includes('data->icon_colors = tile.icon_colors;') &&
  energy.includes('tile_icon_colors::resolve(data->icon_colors.c_str(), state.c_str(), nullptr, rgb)'));
assert.ok(read('src/ui/popups/energy/energy_popup.cpp').includes('lv_obj_set_style_text_color(ctx->icon_label, lv_color_hex(init.icon_color), 0);'));
const camera = read('src/types/camera/renderer.cpp');
assert.ok(camera.includes('tile.sensor_entity, title, icon_name, card_color, tile.icon_colors};') &&
  camera.includes('init.icon_color = tile_icon_source::color(data->icon_colors, payload.c_str());'));
assert.ok(read('src/ui/popups/camera/camera_popup.cpp').includes('lv_obj_set_style_text_color(g_camera_popup->icon_label, lv_color_hex(init.icon_color), 0);'));
// A protected Folder's PIN popup shows the icon in the tile's current color.
const pin = read('src/ui/popups/pin/pin_popup.cpp');
assert.ok(pin.includes('lv_obj_set_style_text_color(g_ctx->icon_label, lv_color_hex(init.icon_color), 0);') &&
  pin.includes('lv_obj_set_style_text_color(parts.icon, lv_color_hex(init.icon_color), 0);'),
  'PIN popup icon color on first build and on reuse');
assert.ok(read('src/ui/ui_manager.cpp').includes('init.icon_color = icon_color;'));
const navigateSource = read('src/types/navigate/renderer.cpp');
assert.ok(navigateSource.includes('lv_obj_t* icon = tile_icon_source::card_icon(') &&
  navigateSource.includes('data->bg_color, icon_color);'), 'Folder tiles pass their current icon color');
// The shell follows the body icon color every sync, so disc and border follow.
assert.match(read('src/ui/popups/popup_shell.cpp'), /copy_label\(shell\.icon, shell\.active->icon, false\);\s*apply_header_disc_tint\(shell\.icon_disc, shell\.icon\);/);
console.log('Popup header icons follow the tile icon colors');
