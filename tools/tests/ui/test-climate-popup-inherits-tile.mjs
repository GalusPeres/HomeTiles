// The Climate popup inherits the tile it was opened from: the card is the
// tile's current background (own color or rules tint), and every neutral
// surface (track, markers, pills, toggle, menu, cut-out text) is derived from
// that card instead of a fixed grey. The resident popup restyles on every
// opening before its first frame, while state updates keep the color of the
// last opening.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const popup = read('src/ui/popups/climate/climate_popup.cpp');
const header = read('src/ui/popups/climate/climate_popup.h');
const renderer = read('src/types/climate/renderer.cpp');
const functions = source => new Map(cppFunctionDefinitions(source).map(f => [f.name, f.body]));
const popupFunctions = functions(popup);
const body = name => {
  assert.ok(popupFunctions.has(name), `climate popup: ${name} exists`);
  return popupFunctions.get(name);
};

// Opener: the global tile color for now; the popup does not follow the tile.
assert.ok(renderer.includes('#include "src/tiles/runtime/tile_icon_source.h"'));
const initFor = functions(renderer).get('popup_init_for');
assert.ok(initFor && initFor.includes('init.bg_color = tileDefaultBgColor();'),
  'popup_init_for passes the global tile color');
assert.match(renderer,
  /ClimatePopupInit init = popup_init_for\(data\);\s*if \(!init\.entity_id\.length\(\)\) return;\s*(?:\/\/[^\n]*\n\s*)*tile_icon_source::forget_popup_source\(\);\s*finish_press_before_popup\(event\);\s*show_climate_popup\(init\);/,
  'The opener keeps the global color and never follows the tile');

// Init field and context color.
assert.ok(header.includes('  uint32_t bg_color = 0;\n};'), 'ClimatePopupInit carries bg_color (0 = default card)');
assert.ok(popup.includes('#include "src/ui/popups/popup_surface.h"'));
assert.ok(popup.includes('  uint32_t bg_color = popup_surface::kDefaultCard;'),
  'The context keeps the color of the last opening');

// First build: the card and every derived surface use the opening color.
const show = body('show_climate_popup');
assert.match(show,
  /ctx->bg_color = popup_surface::card_or_default\(init\.bg_color\);\s*const auto parts = create_popup_body\(on_close, ctx, ctx->bg_color\);/,
  'First build creates the card in the tile color');
for (const marker of [
  'create_visual_arc(\n      ctx->body, popup_surface::lighter(ctx->bg_color, popup_surface::kTrack));',
  'popup_surface::lighter(ctx->bg_color, popup_surface::kMarker), 0);',
  'ctx->target_toggle,\n      popup_surface::lighter(ctx->bg_color, popup_surface::kPill), 0);',
  'control.dropdown,\n        popup_surface::lighter(ctx->bg_color, popup_surface::kPill), 0);',
  'popup_surface::lighter(ctx->bg_color, kCaptionStep), 0);',
]) assert.ok(show.includes(marker), `first build: ${marker}`);
assert.equal((show.match(/popup_surface::lighter\(ctx->bg_color, popup_surface::kPressed\)/g) || []).length, 2,
  'Toggle and control pills press to the derived step');

// Reuse path: the resident card restyles before the first frame, only on a
// changed color, including the surfaces styled once at build time.
const prepare = body('prepare_climate_popup_open');
assert.ok(prepare.indexOf('apply_card_color(ctx, init.bg_color);') >= 0 &&
  prepare.indexOf('apply_card_color(ctx, init.bg_color);') < prepare.indexOf('defer_popup_body('),
  'Every opening applies the color before the deferred content');
const applyColor = body('apply_card_color');
for (const marker of [
  'color = popup_surface::card_or_default(color);',
  'if (color == ctx->bg_color) return;',
  'ctx->bg_color = color;',
  'lv_obj_set_style_bg_color(ctx->card, popup_surface::card(color), 0);',
  'popup_surface::lighter(color, popup_surface::kTrack)',
  'popup_surface::lighter(color, popup_surface::kPill), 0);',
  'button, popup_surface::lighter(color, popup_surface::kPressed),\n        LV_STATE_PRESSED);',
  'close_control_menu(ctx);',
  'refresh_ring(ctx);',
  'refresh_target_toggle(ctx);',
  'refresh_controls(ctx);',
]) assert.ok(applyColor.includes(marker), `apply_card_color: ${marker}`);
assert.equal((popup.match(/apply_card_color\(/g) || []).length, 3,
  'Only the opening path and the live tile follow apply a card color');
assert.ok(popup.includes('apply_card_color(ctx, color, false);'), 'The live follow keeps an open menu');

// Update paths (MQTT state, mini +/- taps) never reset the card color.
for (const name of ['update_climate_popup', 'apply_init', 'remote_apply_timer_cb', 'defer_remote_apply']) {
  assert.doesNotMatch(body(name), /bg_color|apply_card_color|create_popup_body/,
    `${name} keeps the color of the last opening`);
}

// State refreshes and the per-opening menu read the context color.
assert.match(body('refresh_current_marker'),
  /popup_surface::lighter\(\s*ctx->bg_color,\s*current_on_colored_ring \? popup_surface::kTrack\s*: popup_surface::kMarker\)/);
assert.equal((body('refresh_ring').match(/off \? popup_surface::lighter\(ctx->bg_color, popup_surface::kTrack\)/g) || []).length, 2,
  'Off range markers use the derived track');
assert.ok(body('refresh_target_toggle').includes('popup_surface::lighter(ctx->bg_color, popup_surface::kPill);'));
const control = body('refresh_control');
for (const marker of [
  ': popup_surface::lighter(ctx->bg_color, popup_surface::kPill);',
  'const lv_color_t cut_out = popup_surface::card(ctx->bg_color);',
  ': popup_surface::lighter(ctx->bg_color, popup_surface::kPressed),',
  ': popup_surface::lighter(ctx->bg_color, kCaptionStep),',
]) assert.ok(control.includes(marker), `refresh_control: ${marker}`);
const menu = body('open_control_menu');
for (const marker of [
  'popup_surface::lighter(ctx->bg_color, popup_surface::kPill);',
  'popup_surface::lighter(ctx->bg_color, popup_surface::kPressed);',
  'const lv_color_t cut_out = popup_surface::card(ctx->bg_color);',
  'popup_surface::lighter(ctx->bg_color, kMenuSeparatorStep), 0);',
  'lv_obj_set_style_bg_opa(separator_line, LV_OPA_COVER, 0);',
  'label, selected ? cut_out : lv_color_white(), 0);',
]) assert.ok(menu.includes(marker), `open_control_menu: ${marker}`);

// The former fixed greys and the hardcoded card color are gone.
assert.doesNotMatch(popup, /\bk(?:CardBg|TrackColor|PillBg|PillPressedBg)\b/, 'No fixed grey constants');
assert.doesNotMatch(popup, /lv_color_hex\(0x(?:2A2A2A|444444|363636|5A5A5A|A8A8A8|777777|D0D0D0)\)/i,
  'No fixed neutral grey');
assert.doesNotMatch(popup, /\b0x(?:2A2A2A|444444|363636|5A5A5A|A8A8A8)\b/i, 'No former grey literal');

// The local steps reproduce the former greys on the default card
// (lv_color_mix with LV_COLOR_MIX_ROUND_OFS 0).
const step = name => Number(popup.match(new RegExp(`constexpr lv_opa_t ${name} = (\\d+);`))[1]);
const mix = (fg, bg, m) => Number((BigInt(fg * m + bg * (255 - m)) * 0x8081n) >> 23n);
assert.equal(mix(0xFF, 0x2A, step('kMenuSeparatorStep')), 0x5D, 'Separator keeps 0x5D5D5D');
assert.equal(mix(0xFF, 0x2A, step('kCaptionStep')), 0xD0, 'Pill caption keeps 0xD0D0D0');

console.log('Climate popup inherits the tile background');
