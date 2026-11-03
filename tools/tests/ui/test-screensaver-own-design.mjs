// The screensaver's own design (Dark or Light, user 2026-10-11) is a screen
// of its own: its tiles and the popups they open follow the pages' rules in
// that design (user 2026-11-02: "im Screensaver stimmt nix zusammen"). Before,
// the design reached only the tiles' text roles (a Light or Dark surface):
// "Ausfuellen" never filled there (tile_surface_rule_of left the screensaver
// out), and real icon colors, frame lines, white lights and the popup's
// colors followed the panel's theme, so a light screensaver over a dark
// panel opened dark popups. ui_theme::g_screen carries the design, the
// screensaver marks its tile grid with it (tile_screen.h), every tile scope
// and popup scope opens it. The Web Admin preview does the same.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n/g, '\n');
const fn = (source, name) => cppFunctionDefinitions(source).find(f => f.name === name)?.source ?? '';

// ---- The screen's design in ui_theme ----------------------------------------
const output = compileAndRun({
  label: 'screensaver own design',
  harness: String.raw`
#include <cstdio>
#include "src/ui/shared/ui_theme.h"
int main() {
  using namespace ui_theme;
  set(0);  // the panel dark
  int ok = 1;
  ok &= !theme_light() && !panel_light() && roles().card == kDark.card;
  {
    const SurfaceScope screen(-1, nullptr, 1);  // a light screensaver
    ok &= theme_light() && !panel_light() && light() && roles().card == kLight.card;
    {
      // A tile on the theme keeps the screen (tile_surface::scope passes it).
      const SurfaceScope tile(kThemeSurface, &ok);
      ok &= theme_light() && roles().text == kLight.text;
    }
    {
      // A filled tile: white words, the light design's rules.
      const SurfaceScope filled(kDarkTintedSurface, &ok);
      ok &= theme_light() && roles().text == kDarkTinted.text;
    }
    {
      // A page tile inside: the panel's theme.
      const SurfaceScope page(kThemeSurface, nullptr, -1);
      ok &= !theme_light() && roles().card == kDark.card;
    }
    ok &= theme_light();
  }
  ok &= g_screen == -1 && !theme_light();
  std::printf("screen %d\n", ok);
  return 0;
}
`,
});
if (output !== null) assert.match(output, /screen 1/, 'the screen design scopes and restores');

const theme = read('src/ui/shared/ui_theme.h');
assert.match(theme, /inline bool theme_light\(\) \{ return g_screen >= 0 \? g_screen == 1 : panel_light\(\); \}/);
assert.match(theme, /if \(screen != kKeepScreen\) g_screen = screen;/);
assert.match(theme, /g_screen = saved_screen_;/);

// ---- Tiles: the grid's mark, every tile scope -------------------------------
const screenHeader = read('src/tiles/runtime/tile_screen.h');
assert.match(screenHeader, /inline int8_t of\(lv_obj_t\* obj\) \{\n  for \(int depth = 0; obj && depth < 8;/);
const disc = read('src/tiles/runtime/tile_icon_disc.h');
assert.match(disc, /return ui_theme::SurfaceScope\(theme >= 0 \? theme : ui_theme::kThemeSurface, card,\n\s*tile_screen::of\(card \? card : obj\)\);/,
  'every tile scope opens its screen');

const saver = read('src/ui/screensaver/image_screensaver.cpp');
const rebuild = fn(saver, 'rebuild_slot_grid');
assert.match(rebuild, /const uint8_t design = screensaverConfig\.get\(\)\.theme;\n  tile_screen::set\(st->slot_grid, design <= 1 \? static_cast<int8_t>\(design\) : -1\);/,
  'the screensaver marks its grid with its design (Automatic: the panel\'s)');
assert.ok(rebuild.indexOf('tile_screen::set(st->slot_grid') < rebuild.indexOf('build_slot_tile(st, i, tile);'),
  'before its tiles are built');

const renderer = read('src/tiles/runtime/tile_renderer.cpp');
const render = fn(renderer, 'render_tile');
assert.match(render, /const ui_theme::SurfaceScope on_screen\(-1, nullptr,\n\s*grid_type == GridType::SCREENSAVER \? tile_screen::of\(parent\) : -1\);\n[\s\S]*?const uint8_t surface = tile_surface_of\(tile, grid_type\);/,
  'a tile renders on its screen\'s design, its surface computed there');
const ruleOf = renderer.slice(renderer.indexOf('static uint8_t tile_surface_rule_of(const Tile& tile, GridType grid_type) {'));
assert.match(ruleOf, /^static uint8_t tile_surface_rule_of\(const Tile& tile, GridType grid_type\) \{\n  \(void\)grid_type;\n  if \(tile\.type == TILE_EMPTY \|\| !ui_theme::theme_light\(\) \|\|/,
  '"Ausfuellen" fills on the screensaver too');
assert.doesNotMatch(renderer, /screensaverConfig\.get\(\)\.theme/, 'no Light or Dark surface of its own any more');

// The theme's rules read on the card's screen.
const source = read('src/tiles/runtime/tile_icon_source.cpp');
for (const name of ['with_white', 'follow_open_popup', 'popup_background', 'change_surface']) {
  assert.match(fn(source, name), /const ui_theme::SurfaceScope screen = tile_screen::scope\((card|obj)\);/, name);
}
assert.ok(fn(source, 'change_surface').indexOf('tile_screen::scope(card)') <
  fn(source, 'change_surface').indexOf('ui_theme::roles_of(tile_surface::theme_of(before))'), 'from roles on the card\'s screen');
const style = read('src/ui/shared/ui_surface_style.cpp');
assert.match(fn(style, 'apply_style'), /const ui_theme::SurfaceScope screen = tile_screen::scope\(obj\);/, 'the frame line too');

// ---- Popups: the opener's screen -------------------------------------------
const popupSurface = read('src/ui/popups/popup_surface.h');
assert.match(popupSurface, /inline ui_theme::SurfaceScope scope\(\) \{ return ui_theme::SurfaceScope\(g_open_surface, nullptr, g_open_screen\); \}/);
assert.match(popupSurface, /return ui_theme::SurfaceScope\(ui_theme::kThemeSurface, nullptr, g_open_screen\);/, 'the neutral popups too');
assert.match(fn(source, 'set_open_popup_surface'), /popup_surface::g_open_screen = tile_screen::of\(obj\);/);
assert.match(fn(source, 'forget_popup_source'), /popup_surface::g_open_screen = tile_screen::of\(obj\);/);
assert.match(fn(source, 'open_popup_without_tile'), /popup_surface::g_open_screen = -1;/);
const shell = read('src/ui/popups/popup_shell.cpp');
assert.match(fn(shell, 'wanted_surface'), /return want < 0 && screen >= 0 && \(screen == 1\) != ui_theme::panel_light\(\) \? screen : want;/,
  'a popup takes the roles of its opener\'s screen');
assert.match(fn(shell, 'follow_mark'), /recolor_surface\(mark\.root, mark\.surface < 0 \? panel : mark\.surface, want < 0 \? panel : want\);/,
  'from the panel\'s roles it was built with, whatever screen is open');
// Between screens every popup takes the design before the opened one applies
// its content: recolored after it, the Climate popup's white mode on its
// orange pill turned dark (simulator, b656). A body records the surface it
// is built with (built for a light screen, it is not mapped again).
const followScreen = fn(shell, 'popup_follow_screen');
assert.match(followScreen, /bool screen = want == 0 \|\| want == 1;/);
assert.match(followScreen, /if \(entry\.root\) follow_mark\(entry, want\);/);
assert.match(fn(shell, 'create_popup_body'), /add_mark\(parts\.card, current_surface\(\)\);/);
for (const name of ['set_open_popup_surface', 'forget_popup_source', 'open_popup_without_tile']) {
  assert.match(fn(source, name), /popup_follow_screen\(\);/, `${name}: the popups take the opener's screen first`);
}
// The head's X takes the head's text like the title.
assert.match(fn(shell, 'sync_popup_shell'), /lv_color_eq\(x_now, lv_color_hex\(ui_theme::kLight\.icon\)\)\) &&\s*!lv_color_eq\(x_now, header_text\)\) \{\s*lv_obj_set_style_text_color\(x, header_text, 0\);/);
for (const file of ['src/ui/popups/calendar/calendar_popup.cpp', 'src/ui/popups/device/device_popup.cpp',
  'src/ui/popups/vacuum/vacuum_popup.cpp']) {
  const text = read(file);
  assert.match(text, /uint32_t theme_default_card\(lv_obj_t\* source\) \{\n[^]*?SurfaceScope theme\(ui_theme::kThemeSurface, nullptr, tile_screen::of\(source\)\);/, file);
  assert.doesNotMatch(text, /theme_default_card\(\)/, file + ': every default card on the opener\'s screen');
}
assert.match(read('src/ui/popups/media/media_popup.cpp'), /return ui_theme::SurfaceScope\(drawn, nullptr, popup_surface::drawn_screen\(drawn\)\);/);
assert.match(read('src/ui/popups/weather/weather_popup.cpp'), /open_surface \? popup_surface::g_open_screen\s*: popup_surface::drawn_screen\(drawn\)\);/);

// The neutral popups (Light, Climate, Cover) take their values in the tile's
// screen: the Switch tile handed the Light popup the dark panel's card while
// the popup drew light text on it (user 2026-11-02, b655: dark panel, light
// screensaver).
for (const [file, before] of [['src/types/switch/renderer.cpp', 'LightPopupInit init = build_light_popup_init(data);'],
  ['src/types/cover/renderer.cpp', 'CoverPopupInit init = popup_init(data->grid_type, data->index);'],
  ['src/types/climate/renderer.cpp', 'ClimatePopupInit init = popup_init_for(data);']]) {
  const text = read(file);
  const at = text.indexOf(before);
  const scope = text.lastIndexOf('const ui_theme::SurfaceScope screen =', at);
  assert.ok(at > 0 && scope > 0 && at - scope < 400 &&
    /const ui_theme::SurfaceScope screen =\s*tile_screen::scope\(static_cast<lv_obj_t\*>\(lv_event_get_current_target\(e(vent)?\)\)\);/.test(text.slice(scope, at)),
    `${file}: the popup's values on the tile's screen`);
}

// ---- The Web Admin preview: the same rules ----------------------------------
const preview = read('src/web/admin/tiles/grid-preview.js');
assert.match(preview, /function designLight\(\) \{\n    const screen = globalThis\.hometilesPreviewScreen;/);
const follows = preview.slice(preview.indexOf('function tileFollowsTint('), preview.indexOf('function applyFrameLine('));
assert.doesNotMatch(follows, /return false;/, 'the preview fills screensaver tiles too');
assert.match(follows, /const screen = previewScreenOf\(tab\);\n    const light = screen === 0 \|\| screen === 1 \? screen === 1 : panelThemeLight\(\);/);
assert.match(preview, /if \(designLight\(\) && tinted\) return icon;/, 'real icon colors in the light design');
assert.match(preview, /else if \(!designLight\(\)\) line = card\.map/, 'frame lines of the design');
assert.match(preview, /return designLight\(\) \? panelRoleHex\('icon-unavailable'/);
assert.match(preview, /tileElem\.dataset\.followTint === '1' \|\| !designLight\(\)\) && toneNoColor\(given\)/);
assert.match(preview, /globalThis\.hometilesPreviewScreen = previewScreenOf\(tab\);/);
assert.match(read('src/web/admin/tiles/live-preview.js'), /globalThis\.hometilesPreviewScreen = typeof previewScreenOf === 'function' \? previewScreenOf\(tab\) : undefined;/);

console.log('Screensaver own design: its tiles and popups follow the pages\' rules in its Dark or Light, filled tiles included');
