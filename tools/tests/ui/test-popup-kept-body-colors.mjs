// A popup reopened for the same entity shows its resident body in the first
// frame and applies its content after that frame. Weather, Media and the
// Sensor's range buttons took the opening tile's colors only in that content
// step, so the first frame showed the last opening's colors (user 2026-10-28:
// "die ganze Zeitleiste noch rot", everything else beige; the Weather footer
// bottom left). Cover, Climate and Light already recolor before the first
// frame. Now every kept body takes the new colors right after the shell
// (whose disc the controls follow), before the first frame.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};
const after = (body, first, second, message) => {
  const a = body.indexOf(first);
  const b = body.indexOf(second, a + 1);
  assert.ok(a >= 0 && b > a, message);
};

// Weather: the footer pill and buttons (apply_init_to_context) after the shell.
const weather = read('src/ui/popups/weather/weather_popup.cpp');
const showWeather = fn(weather, 'show_weather_popup');
after(showWeather, 'show_popup_shell(', 'apply_init_to_context(ctx, init);', 'Weather recolors its kept body after the shell');
assert.ok(fn(weather, 'apply_init_to_context').includes('apply_card_color(ctx, init.bg_color);') &&
  fn(weather, 'apply_init_to_context').includes('update_mode_buttons(ctx);'), 'with the footer pill and the mode buttons');

// Sensor: the range buttons and the editor when the body is kept.
const sensor = read('src/ui/popups/sensor/sensor_popup.cpp');
const showSensor = fn(sensor, 'show_sensor_popup');
assert.ok(showSensor.includes('const bool kept_body = reusable_sensor_body(ctx, init);'));
after(showSensor, 'show_popup_shell(', 'if (kept_body) {\n    update_range_buttons(ctx);\n    editable_control_follow_colors(ctx->control);',
  'Sensor restyles its kept range buttons after the shell');

// Media: the kept player's colored parts after the shell, as build_player
// colors them (the rebuild after the frame then draws the same).
const media = read('src/ui/popups/media/media_popup.cpp');
const showMedia = fn(media, 'show_media_popup');
after(showMedia, 'prepare_media_popup_open(init);', 'recolor_kept_player(g_media_popup_ctx, init);', 'the resident path recolors');
after(showMedia, 'show_popup_shell(', 'recolor_kept_player(g_media_popup_ctx, init);', 'after the shell');
const kept = fn(media, 'recolor_kept_player');
assert.ok(kept.includes('popup_open_pending(ctx->card)') &&
  kept.includes('if (ctx->entity_id != init.entity_id || ctx->centered != init.cover_centered) return;'),
  'only a kept body waiting for its content');
assert.ok(kept.includes('if (colors_key(colors) == colors_key(ctx->colors)) return;') && kept.includes('recolor_player(ctx);'));
assert.ok(!kept.includes('built_colors'), 'the content step still rebuilds the player (menu included)');
const slider = fn(media, 'make_slider');
const recolorSlider = fn(media, 'recolor_slider');
const fill = 'lv_color_hex(c.tinted ? c.icon : c.control_icon)';
assert.ok(slider.includes(fill) && recolorSlider.includes(fill), 'the bars fill as built');
for (const style of ['lv_obj_set_style_bg_color(slider.track, c.control, 0);', 'lv_obj_set_style_bg_opa(slider.track, c.control_opa, 0);',
  'lv_obj_set_style_bg_color(slider.cap, fill, 0);', 'lv_obj_set_style_bg_color(slider.input, fill, LV_PART_INDICATOR);',
  'lv_obj_set_style_bg_color(slider.input, lv_color_hex(c.control_icon), LV_PART_KNOB);']) {
  assert.ok(slider.includes(style) && recolorSlider.includes(style), `bar: ${style}`);
}
const player = fn(media, 'recolor_player');
for (const part of ['recolor_slider(ctx->seek_slider, c);', 'recolor_slider(ctx->volume_slider, c);',
  'lv_obj_set_style_bg_color(ctx->bars, lv_color_hex(c.icon), 0);', 'style_toggle(label, c,',
  'lv_obj_set_style_text_color(ctx->play_pause_label, lv_color_hex(c.card), 0);', 'recolor_button(ctx->menu_button,']) {
  assert.ok(player.includes(part), `player: ${part}`);
}
assert.ok(fn(media, 'round_button').includes('popup_nav_style::style_press_fill(btn, c.control, c.control_opa);') &&
  fn(media, 'recolor_button').includes('popup_nav_style::style_press_fill(btn, c.control, c.control_opa);'), 'buttons as built');

// The popups that already did it keep doing it.
for (const [file, name] of [['src/ui/popups/cover/cover_popup.cpp', 'prepare_cover_popup_open'],
  ['src/ui/popups/climate/climate_popup.cpp', 'prepare_climate_popup_open'],
  ['src/ui/popups/light/light_popup.cpp', 'prepare_light_popup_open']]) {
  const prepare = fn(read(file), name);
  after(prepare, 'apply_card_color(ctx, init.bg_color);', 'defer_popup_body(', `${name} colors before deferring`);
}
console.log('Kept popup bodies take the opening tile\'s colors before their first frame');
