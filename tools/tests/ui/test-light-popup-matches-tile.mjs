// The Light popup looks like the Switch tile it was opened from (user
// 2026-10-01, mockup build/design-mockups/switch-tile/popup-vs-tile.png):
// header circle, brightness and switch track, the selected mode button and
// every press show exactly the tile's circle and bar color; the off thumb is
// one circle step above the track and its symbol has the grey of an off icon,
// on the tile and in the popup. The popup card stays neutral. A tile with the
// tile color "From icon" at any strength passes that strength, so the popup
// circle is the tile circle; Global and Custom tiles show the From icon
// circle at its default strength on both.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const iconSource = read('src/tiles/runtime/tile_icon_source.cpp');
const fromIconCard = cppFunctionDefinitions(iconSource).find(f => f.name === 'from_icon_card');
const brighten = cppFunctionDefinitions(read('src/tiles/runtime/tile_renderer_shared.h'))
  .find(f => f.name === 'brighten_rgb_color');
assert.ok(fromIconCard && brighten);

const harness = String.raw`
#include "src/ui/shared/tone_color.h"
#include "src/tiles/config/tile_tint.h"
#include "src/tiles/config/tile_icon_colors.h"

#include <cstdio>
#include <initializer_list>

static uint32_t g_global = 0x1A1A1A;
static uint32_t tileDefaultBgColor() { return g_global; }
${brighten.source}
${fromIconCard.source}

int main() {
  tone_color::g_from_icon_card = &from_icon_card;
  for (const uint32_t global : {0x1A1A1Au, 0x3A3A3Au}) {
    g_global = global;
    for (const uint8_t glow : {25, 60}) {
      for (const uint32_t icon : {0x38D4F5u, 0xFFD54Fu, 0xF44336u, 0x9C27B0u}) {
        for (const uint8_t strength : {0, 20, 40}) {
          // The tile: "From icon" at its strength draws on its own card;
          // Global (strength 0) on the From icon card at the default strength.
          const uint32_t tile_card = strength ? tile_tint::background(global, icon, strength) : global;
          const uint32_t tile_circle_card = strength ? tile_card : from_icon_card(icon, false, 0);
          const tone_color::Fill tile = tone_color::fill(tile_circle_card, icon, true, glow);
          // The Light popup: the neutral global card, the circle computed for
          // the card the tile's strength gives (popup_shell.cpp header_fill).
          const tone_color::Fill popup = tone_color::fill(from_icon_card(icon, false, strength), icon, true, glow);
          std::printf("on %06X %u %06X %u %06X %06X %06X %06X\n", static_cast<unsigned>(global),
                      static_cast<unsigned>(glow), static_cast<unsigned>(icon), static_cast<unsigned>(strength),
                      static_cast<unsigned>(tile.disc_color),
                      static_cast<unsigned>(popup.disc_color), static_cast<unsigned>(tile.control_color),
                      static_cast<unsigned>(popup.control_color));
        }
      }
      // Off: the grey off icon never tints; tile (the card falls back to the
      // global color) and popup show the neutral step and the same off thumb.
      const tone_color::Fill tile = tone_color::fill(global, tone_color::kOffIcon, false, glow);
      const tone_color::Fill popup = tone_color::fill(global, tone_color::kOffIcon, false, glow);
      std::printf("off %06X %u %06X %06X %06X %06X\n", static_cast<unsigned>(global), static_cast<unsigned>(glow),
                  static_cast<unsigned>(tile.control_color), static_cast<unsigned>(popup.control_color),
                  static_cast<unsigned>(tone_color::switch_thumb_off(tile.control_color)),
                  static_cast<unsigned>(tone_color::switch_thumb_off(popup.control_color)));
    }
  }
  return 0;
}
`;

const output = compileAndRun({label: 'Light popup matches the tile', harness});
if (output !== null) {
  for (const line of output.trim().split('\n')) {
    const parts = line.trim().split(' ');
    if (parts[0] === 'on') {
      const [, global, glow, icon, strength, tileDisc, popupDisc, tileControl, popupControl] = parts;
      const what = `global ${global}, circle ${glow} %, icon ${icon}, From icon ${strength} %`;
      assert.equal(popupDisc, tileDisc, `${what}: popup circle = tile circle`);
      assert.equal(popupControl, tileControl, `${what}: popup track and buttons = tile bar`);
    } else {
      const [, global, glow, tileControl, popupControl, tileThumb, popupThumb] = parts;
      assert.equal(popupControl, tileControl, `off on ${global} at ${glow} %: same neutral track`);
      assert.equal(popupThumb, tileThumb, `off on ${global} at ${glow} %: same off thumb`);
      assert.notEqual(tileThumb, tileControl, 'the off thumb sits a step above its track');
    }
  }
}

// One set of functions on both sides.
const tone = read('src/ui/shared/tone_color.h');
assert.ok(tone.includes('inline constexpr uint32_t kOffIcon = 0xB0B0B0;') &&
  tone.includes('inline uint32_t switch_thumb_off(uint32_t track) { return lifted(track, track, false, kThumbStep); }'));
const tileSwitch = read('src/types/switch/renderer.cpp');
assert.ok(tileSwitch.includes('constexpr uint32_t kIconOff = tone_color::kOffIcon;') &&
  tileSwitch.includes('view->thumb_off = tone_color::switch_thumb_off(base);') &&
  tileSwitch.includes('symbol_color = lv_color_hex(kIconOff);'), 'Tile switch: off thumb and grey off symbol');
const popup = read('src/ui/popups/light/light_popup.cpp');
assert.ok(popup.includes('popup_nav_style::fill(popup_surface::card(ctx->card_bg), lv_color_hex(header_icon_rgb(ctx, icon_rgb)), color, opa);'),
  'Light popup controls use the shell control fill (the header circle color)');
assert.equal((popup.match(/control_fill\(ctx, icon_rgb, track, track_opa\);\n\s*(?:const lv_color_t thumb_color =|lv_obj_set_style_bg_color\(ctx->val_slider, track, LV_PART_MAIN\);)/g) || []).length, 2,
  'Switch and brightness track take the control fill');
assert.ok(popup.includes(': lv_color_hex(tone_color::switch_thumb_off(lv_color_to_u32(track) & 0xFFFFFF));') &&
  popup.includes('ctx->is_on ? brightness_dash_color(ctx) : lv_color_hex(tone_color::kOffIcon), 0);'),
  'Light popup switch: off thumb a step above the track, grey off symbol, on symbol in the tile card color');
assert.ok(popup.includes('const lv_opa_t bg_opa = enabled && (active || pressed) ? fill_opa : static_cast<lv_opa_t>(LV_OPA_TRANSP);') &&
  popup.includes('popup_nav_style::no_press_filter(button, selector);'),
  'Selected and pressed mode buttons show the control fill without theme darkening');
assert.ok(popup.includes('popup_nav_style::style_press_fill(ctx->power_button, fill, fill_opa);'),
  'The off power button presses in the control fill');
// Regression b156 (user 2026-10-01): switching the light on or off moved the
// header circle between blue-grey and grey, but the mode buttons kept the
// old color until another mode was picked.
const slice = (from, to) => popup.slice(popup.indexOf(from), popup.indexOf(to, popup.indexOf(from)));
assert.ok(slice('static void update_header_and_power_visuals(', 'static void update_live_accent_visuals(')
  .includes('follow_mode_button_fill(ctx, icon_rgb);'), 'On/off and state updates recolor the mode buttons');
// Regression b157 (user 2026-10-01): restyling all three mode buttons in
// both states on every step made the color wheel stutter (three more dirty
// areas per step). A step recolors only the visible selected button live;
// the rest follows once the finger lifts.
assert.ok(slice('static void update_live_accent_visuals(', 'static lv_color_t brightness_dash_color(')
  .includes('follow_mode_button_fill(ctx, icon_rgb, true);'), 'The selected button follows a drag live');
const follow = slice('static void follow_mode_button_fill(', 'static void update_header_and_power_visuals(');
assert.ok(follow.includes('if (!button || (live && button != selected)) continue;') &&
  follow.includes('if (live && selector != LV_PART_MAIN) continue;'), 'Live: only the selected resting fill');
for (const handler of ['static void on_temp_track_event(', 'static void on_color_field_event(']) {
  const body = slice(handler, 'static void apply_');
  assert.equal((body.match(/follow_mode_button_fill\(ctx, get_preview_icon_rgb\(ctx\)\);/g) || []).length, 2,
    `${handler} recolors all mode buttons on release`);
  assert.ok(body.includes('start_drag_timing(ctx->card);') && body.includes('note_drag_step(step_us);'),
    `${handler} measures the drag`);
  // The invisible close press color waits for the release too (b159).
  assert.ok(body.includes('popup_shell_hold_close_fill(true);') && body.includes('popup_shell_hold_close_fill(false);'),
    `${handler} holds the close press color while dragging`);
}
assert.ok(popup.includes('Serial.printf("[LightPopup] %s drag: %lums, steps=%lu (avg %luus, max %luus), frames=%lu (avg %luus, max %luus), "'),
  'One English diagnostic line per drag');
assert.match(iconSource, /static Entry cache\[4\] = \{\};[\s\S]*tile_tint::background\(base, icon, percent\)/,
  'The From icon card is cached, not recomputed on every loop pass');
assert.ok(popup.includes('follow_mode_button_fill(ctx, get_preview_icon_rgb(ctx));'),
  'A reused popup shows the button color of the new light in its first frame');
for (const gone of ['kAccentTrackShare', 'accent_track_color', 'kSwitchThumbOffStep', 'popup_shell_disc_track',
  'kControlButtonIndicator']) {
  assert.ok(!popup.includes(gone), `Light popup still contains ${gone}`);
}
const shell = read('src/ui/popups/popup_shell.cpp');
assert.ok(shell.includes('? tone_color::g_from_icon_card(rgb, false, options.tile_tint)') &&
  !/disc_track|on_track|tile_from_icon/.test(shell), 'The header circle has no Light exception');
assert.ok(iconSource.includes('if (icon_fill_marker(obj, marker)) return marker;') &&
  iconSource.includes('tile_icon_disc::glow_of(disc), popup_shows_tile_color && from_icon > 0,\n                            from_icon);'),
  'The opening tile passes its From icon strength');
assert.match(read('src/web/assets/admin.css'),
  /\.tile-switch \.tile-switch-knob \{[^}]*background:var\(--switch-thumb-off, #555\);\s*color:#B0B0B0;/,
  'Web preview: grey off symbol');
console.log('Light popup circle, track, buttons and switch match the tile');
