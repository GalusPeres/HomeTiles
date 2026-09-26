// The Light popup inherits the tile it was opened from: the card is the
// tile's current background (own color or rules tint) and every neutral
// surface derives from that card instead of a fixed grey. Only an opening
// applies the color; state updates keep the color of the last opening.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const slice = (source, start, end) => {
  const from = source.indexOf(start);
  const to = source.indexOf(end, from + start.length);
  assert.ok(from >= 0 && to > from, `${start} .. ${end} not found`);
  return source.slice(from, to);
};
const header = read('src/ui/popups/light/light_popup.h');
const popup = read('src/ui/popups/light/light_popup.cpp');

// The init carries the card color; 0 falls back to the default card.
assert.match(header, /struct LightPopupInit \{[^}]*\n  uint32_t bg_color = 0;\n[^}]*\};/,
  'LightPopupInit has a bg_color field');
assert.ok(popup.includes('#include "src/ui/popups/popup_surface.h"'));
assert.ok(popup.includes('  uint32_t card_bg = popup_surface::kDefaultCard;\n'),
  'The context keeps the card color of the last opening');

// First build: the body is created in the tile's color and the resident
// notches take it right away.
const show = slice(popup, 'void show_light_popup(const LightPopupInit& init) {', 'void update_light_popup(');
assert.match(show,
  /ctx->card_bg = popup_surface::card_or_default\(init\.bg_color\);\s*const auto parts = create_popup_body\(on_close_click, ctx, ctx->card_bg\);/,
  'First build passes the tile color to create_popup_body');
assert.match(show, /&ctx->temp_handle_dash,\s*&ctx->temp_value_label\);\s*apply_card_cutouts\(ctx\);/,
  'First build colors the resident notches');

// Reuse: the resident card takes the opening tile's color before the shell
// shows its first frame and before the body visibility is decided.
const prepare = slice(popup, 'static void prepare_light_popup_open(', 'void show_light_popup(');
const applyColor = prepare.indexOf('apply_card_color(ctx, init.bg_color);');
assert.ok(applyColor >= 0 && applyColor < prepare.indexOf('const bool keep_visible') &&
  applyColor < prepare.indexOf('defer_popup_body('), 'Reuse applies the color before the deferred body');
const reuse = slice(show, 'if (g_light_popup_ctx && g_light_popup_ctx->overlay', 'LightPopupContext* ctx = new');
assert.ok(reuse.indexOf('prepare_light_popup_open(init);') >= 0 &&
  reuse.indexOf('prepare_light_popup_open(init);') < reuse.indexOf('show_popup_shell('),
  'Reuse prepares the color before show_popup_shell');

// Only a changed color is applied: it recolors the card and the resident
// cut-outs, and stale derived content waits for the full apply.
const apply = slice(popup, 'static void apply_card_color(', 'static void apply_init_to_context(');
for (const marker of [
  'const uint32_t color = popup_surface::card_or_default(bg_color);',
  'color == ctx->card_bg) return;',
  'ctx->card_bg = color;',
  'lv_obj_set_style_bg_color(ctx->card, lv_color_hex(color), 0);',
  'apply_card_cutouts(ctx);',
  'ctx->body_ready = false;',
]) assert.ok(apply.includes(marker), `apply_card_color: ${marker}`);
const cutouts = slice(popup, 'static void apply_card_cutouts(', 'static void apply_card_color(');
assert.ok(cutouts.includes('lv_obj_set_style_bg_color(ctx->val_dash, cutout, 0);') &&
  cutouts.includes('lv_obj_set_style_bg_color(ctx->temp_handle_dash, cutout, 0);'),
  'Resident notches are recolored');
assert.equal((popup.match(/apply_card_color\(/g) || []).length, 2,
  'Only the opening path applies a card color');

// State updates (MQTT, own commands echoed through the tile) keep the color.
const update = slice(popup, 'void update_light_popup(', 'void preload_light_popup()');
const initApply = slice(popup, 'static void apply_init_to_context(', 'static void on_power_button_click(');
const finish = slice(popup, 'static void finish_light_popup_open(', 'static void prepare_light_popup_open(');
for (const [name, body] of [['update_light_popup', update], ['apply_init_to_context', initApply],
  ['finish_light_popup_open', finish]]) {
  assert.ok(!/bg_color|card_bg = |apply_card_color|lv_obj_set_style_bg_color\(ctx->card/.test(body),
    `${name} must not reset the card color`);
}

// Neutral surfaces derive from the current card.
for (const marker of [
  'visual_on ? popup_surface::card(ctx->card_bg) : lv_color_white();',
  'ctx->is_on ? accent_color : popup_surface::lighter(ctx->card_bg, kSwitchThumbOffStep);',
  'lv_obj_set_style_text_color(ctx->val_switch_icon, popup_surface::card(ctx->card_bg), 0);',
  'dash_dsc.bg_color = popup_surface::card(ctx->card_bg);',
  'const lv_color_t disabled_color = popup_surface::lighter(card, popup_surface::kDisabled);',
]) assert.ok(popup.includes(marker), `derived surface: ${marker}`);
assert.equal((popup.match(/ctx->supports_(?:brightness|color|temperature),\n\s*ctx->card_bg\);/g) || []).length, 3,
  'Every mode button derives its disabled icon from the card');

// The off thumb step reproduces a neutral 0x8D8D8D on the default card
// (lv_color_mix with LV_COLOR_MIX_ROUND_OFS 0).
const udiv255 = value => Number((BigInt(value) * 0x8081n) >> 23n);
const step = Number(popup.match(/constexpr lv_opa_t kSwitchThumbOffStep = (\d+);/)[1]);
assert.equal(udiv255(255 * step + 0x2A * (255 - step)), 0x8D);

// The former fixed greys and card constants are gone.
for (const gone of ['kControlButtonBg', 'kControlButtonDisabled', 'kControlBarBg',
  '0x2A2A2A', '0x6B6B6B', '0x8A8D96', '0x1F1F22']) {
  assert.ok(!popup.includes(gone), `light popup still contains ${gone}`);
}

// The opener passes the global tile color for now and never follows the tile.
const opener = slice(read('src/types/switch/renderer.cpp'),
  'LightPopupInit init = build_light_popup_init(data);', 'show_light_popup(init);');
assert.match(opener,
  /init\.bg_color = tileDefaultBgColor\(\);\s*tile_icon_source::forget_popup_source\(static_cast<lv_obj_t\*>\(lv_event_get_current_target\(e\)\)\);/,
  'Switch/Light tiles pass the global tile color to the Light popup');

console.log('Light popup inherits the tile background');
