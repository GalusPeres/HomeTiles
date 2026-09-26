// The Cover popup inherits the tile it was opened from: its card is the tile's
// current background (own color or rules tint), applied on the first build and
// on every later opening of the resident body before its first frame. MQTT
// state updates keep the color of the last opening. Former fixed greys (card,
// tilt gaps, position notch, handle dash, disabled mode icons) are derived
// from the card color; the HA Cover accent stays fixed.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// Returns the body of the first function whose declaration starts with
// `signature`, up to its matching closing brace.
function body(source, signature, label) {
  const start = source.indexOf(signature);
  assert.ok(start >= 0, `${label}: ${signature}`);
  const open = source.indexOf('{', start + signature.length);
  let depth = 0;
  for (let i = open; i < source.length; ++i) {
    if (source[i] === '{') ++depth;
    else if (source[i] === '}' && --depth === 0) return source.slice(open, i + 1);
  }
  assert.fail(`${label}: unbalanced ${signature}`);
}

const header = read('src/ui/popups/cover/cover_popup.h');
const popup = read('src/ui/popups/cover/cover_popup.cpp');
const renderer = read('src/types/cover/renderer.cpp');
const surface = read('src/ui/popups/popup_surface.h');

// Init field: 0 means the default card.
assert.match(body(header, 'struct CoverPopupInit', 'Cover init'), /\n  uint32_t bg_color = 0;\n/);

// Opener: the tile card hands over its background, including a rules tint.
assert.ok(renderer.includes('#include "src/tiles/runtime/tile_icon_source.h"'));
const opener = renderer.slice(renderer.indexOf('CoverEventData* data = new CoverEventData{'));
assert.match(opener,
  /init\.bg_color = tileDefaultBgColor\(\);\s*tile_icon_source::forget_popup_source\(static_cast<lv_obj_t\*>\(lv_event_get_current_target\(event\)\)\);\s*finish_press_before_popup\(event\);\s*show_cover_popup\(init\);/,
  'Cover tile passes its current background to the popup');
// The shared builder also feeds MQTT updates, so it carries no color.
assert.doesNotMatch(body(renderer, 'CoverPopupInit popup_init(', 'Cover renderer'), /bg_color/,
  'State updates must not hand a card color to the open popup');

// Context keeps the color of the last opening.
assert.ok(popup.includes('#include "src/ui/popups/popup_surface.h"'));
assert.match(body(popup, 'struct CoverPopupContext', 'Cover context'),
  /uint32_t card_color = popup_surface::kDefaultCard;/);

// First build uses the opener's color for the card and the resident surfaces.
const show = body(popup, 'void show_cover_popup(const CoverPopupInit& init)', 'show');
assert.match(show,
  /ctx->card_color = popup_surface::card_or_default\(init\.bg_color\);\s*const auto parts = create_popup_body\(on_close, ctx, ctx->card_color\);/);
assert.ok(show.includes('create_slider_view(\n      ctx->position_panel, CoverChannel::Tilt, true, ctx->card_color);'));

// Reuse path: every later opening applies the color before the first frame.
const reuse = show.slice(0, show.indexOf('CoverPopupContext* ctx = new CoverPopupContext();'));
assert.ok(reuse.indexOf('prepare_cover_popup_open(init);') >= 0 &&
  reuse.indexOf('prepare_cover_popup_open(init);') < reuse.indexOf('show_popup_shell('),
  'The resident popup is recolored before the shell shows it');
const prepare = body(popup, 'static void prepare_cover_popup_open(const CoverPopupInit& init)', 'prepare');
assert.ok(prepare.indexOf('apply_card_color(ctx, init.bg_color);') >= 0 &&
  prepare.indexOf('apply_card_color(ctx, init.bg_color);') < prepare.indexOf('defer_popup_body('),
  'The card color is applied before the deferred content');

// Only a changed color touches the resident objects, and every surface that
// was created once follows it.
const apply = body(popup, 'void apply_card_color(CoverPopupContext* ctx, uint32_t bg_color)', 'apply');
assert.match(apply, /const uint32_t color = popup_surface::card_or_default\(bg_color\);\s*if \(color == ctx->card_color\) return;\s*ctx->card_color = color;/);
for (const marker of [
  'lv_obj_set_style_bg_color(ctx->card, popup_surface::card(color), 0);',
  'if (gap == ctx->tilt_slider.handle) continue;',
  'lv_obj_set_style_bg_color(gap, popup_surface::card(color), 0);',
  'popup_surface::lighter(color, popup_surface::kDisabled), 0);',
  'style_mode_buttons(ctx);',
  'lv_obj_invalidate(ctx->position_slider.track);',
]) assert.ok(apply.includes(marker), `apply_card_color: ${marker}`);

// Update paths (MQTT, deferred remote state) keep the last opening's color.
for (const [signature, label] of [
  ['void apply_init(CoverPopupContext* ctx, const CoverPopupInit& init)', 'apply_init'],
  ['void update_cover_popup(const CoverPopupInit& init)', 'update_cover_popup'],
  ['void remote_apply_timer_cb(lv_timer_t* timer)', 'remote_apply_timer_cb'],
  ['void refresh_popup(CoverPopupContext* ctx)', 'refresh_popup'],
]) {
  assert.doesNotMatch(body(popup, signature, label), /bg_color|apply_card_color|card_color =|create_popup_body/,
    `${label} must not reset the card color`);
}

// Derived surfaces: position notch at draw time, tilt gaps, handle dash and
// disabled mode icons.
assert.ok(body(popup, 'void on_position_slider_draw(lv_event_t* event)', 'draw')
  .includes('dash.bg_color = popup_surface::card(ctx->card_color);'));
assert.ok(body(popup, 'void create_tilt_gap_mask(lv_obj_t* track, uint32_t card_color)', 'gaps')
  .includes('lv_obj_set_style_bg_color(gap, popup_surface::card(card_color), 0);'));
assert.ok(popup.includes('view.handle_dash = dash;') &&
  popup.includes('dash, popup_surface::lighter(card_color, popup_surface::kDisabled), 0);'));
assert.ok(body(popup, 'void style_mode_button(', 'mode button')
  .includes(': popup_surface::lighter(card_color, popup_surface::kDisabled),'));
assert.match(body(popup, 'void style_mode_buttons(CoverPopupContext* ctx)', 'mode buttons'),
  /position_available, ctx->card_color\);[\s\S]*controls_available, ctx->card_color\);/);

// The former fixed greys are gone; the HA Cover accents stay.
for (const gone of ['kPanelBg', '0x2A2A2A', '0x6B6B6B', 'constexpr uint32_t kDisabled',
  'lv_color_hex(kDisabled)']) {
  assert.ok(!popup.includes(gone), `Cover popup still uses ${gone}`);
}
assert.ok(popup.includes('constexpr uint32_t kHaCoverActive = 0x926BC7;') &&
  popup.includes('constexpr uint32_t kHaCoverInactive = 0x9E9E9E;'));

// The chosen step reproduces the former grey on the default card
// (lv_color_mix with LV_COLOR_MIX_ROUND_OFS 0).
const constant = name => Number(surface.match(new RegExp(`${name} = (0x[0-9A-Fa-f]+|\\d+);`))[1]);
const mix = (step, channel) => Math.floor((255 * step + channel * (255 - step)) * 0x8081 / 2 ** 23);
const card = constant('kDefaultCard') & 0xFF;
assert.equal(card, 0x2A);
assert.equal(mix(constant('kDisabled'), card), 0x6B, 'kDisabled step keeps 0x6B6B6B on 0x2A2A2A');

console.log('Cover popup inherits the tile background');
