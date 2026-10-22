// The light theme's color rules (user 2026-10-10, build/analysis/2026-10-09-analyse.md):
//   - the tile color decides the surface, fixed when the tile is drawn: Global
//     a white card (Home Assistant's look), "From icon" (and Media "From cover") filled (the
//     dark theme's rules on the icon's color or a grey), an own color by its
//     lightness (OKLCH L 0.72);
//   - a filled card: kJewelBelowIcon below its icon's own lightness (at most
//     kJewelCardMaxL) in its hue; white light (Home Assistant's light grey) a
//     neutral grey kJewelNeutralL; no color (off, unavailable, idle) the
//     slightly darker kJewelIdleL; its circle one step above the card; its
//     greys above the circle (unavailable fainter than off); white text and the
//     dark theme's hairline;
//   - every colored icon keeps its own color in the light theme; only a pure
//     white icon takes the icon grey;
//   - a light shows its color as Home Assistant does (saturation at least
//     0.4, white a grey of value 225), on the tile and in its popup;
//   - on a white card the circle is the icon's color at 20 %, a white light's
//     grey takes Home Assistant's neutral control grey (#BDBDBD) instead;
//   - a slider's handle line is the card's color on a light fill; the power
//     glyph of the Light popup is the text color;
//   - a popup's header circle and icon are exactly its tile's.
// Device (C++) and Web Admin preview (JS) compute the same values.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');

// One place for the values (ui_theme.h).
const theme = read('src/ui/shared/ui_theme.h');
for (const marker of ['inline constexpr float kLightSurfaceL = 0.72f;', 'inline constexpr float kLightCircleShare = 0.8f;',
  'inline bool light() { return g_surface >= 0 ? g_surface == 1 : theme_light(); }',
  'if (!(owner_ && saved_owner_ == owner_)) g_surface = saved_;'])
  assert.ok(theme.includes(marker), `ui_theme.h: ${marker}`);
// The filled surface's greys lie above its circle; unavailable fainter; its
// second line is white like the title (user 2026-10-11).
assert.ok(theme.includes('kDark.text,           kDark.icon,      0xDBDBDB,') &&
  theme.includes('0xCECECE,             0xCECECE,        kDark.text,') &&
  theme.includes('kDark.text_soft,      0xA6A6A6};'), 'kDarkTinted greys');
// The preview's fallbacks are the device's values; the page passes them too.
const jewel = {below: 'kJewelBelowIcon', max: 'kJewelCardMaxL', chroma: 'kJewelChroma', 'card-share': 'kJewelCardChromaShare',
  step: 'kJewelCircleStep', 'circle-share': 'kJewelCircleChromaShare', 'circle-cap': 'kJewelCircleChromaCap',
  neutral: 'kJewelNeutralL', idle: 'kJewelIdleL'};
const grid = read('src/web/admin/tiles/grid-preview.js');
const styles = read('src/web/server/render/web_admin_styles.cpp');
for (const [name, constant] of Object.entries(jewel)) {
  const device = Number(theme.match(new RegExp(`inline constexpr float ${constant} = ([0-9.]+)f;`))?.[1]);
  assert.ok(Number.isFinite(device), constant);
  const fallbacks = [...grid.matchAll(new RegExp(`jewelVar\\('${name}', ([0-9.]+)\\)`, 'g'))].map(m => Number(m[1]));
  assert.ok(fallbacks.length && fallbacks.every(v => v === device), `preview --pv-jewel-${name} = ${constant}`);
  assert.ok(styles.includes(`--pv-jewel-${name}:%.3f;`) && styles.includes(`ui_theme::${constant})`), `emitted ${constant}`);
}
// The preview takes the filled surface's greys from its own table.
assert.ok(styles.includes('emit_roles(ui_theme::kDarkTinted, "pvj");') && grid.includes("return grey ? 'j' : (pageLight ? 'd' : '');"),
  'preview: the filled surface greys');

// The surface is fixed when the tile is drawn.
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.ok(renderer.includes('const ui_theme::SurfaceScope scope(surface_theme >= 0 ? surface_theme : ui_theme::kThemeSurface);') &&
  renderer.includes('tile_surface::set(tile_obj, surface);') &&
  renderer.includes('if (tile_surface_rule_of(tile, grid_type) & tile_surface::kNeutralIdle) return tile_surface::kJewel;') &&
  renderer.includes('return tone_color::light_surface(own) ? tile_surface::kLight : tile_surface::kDark;'),
  'render_tile: the surface from the tile color');
assert.ok(renderer.includes('if (ui_theme::theme_light()) rgb = tone_color::ha_light_color(rgb);'), 'lights as Home Assistant');
assert.doesNotMatch(renderer, /kDarkDesign|dark_design|media_follow_playback/, 'no dark design any more');
const disc = read('src/tiles/runtime/tile_icon_disc.h');
assert.ok(disc.includes('const ui_theme::SurfaceScope surface = tile_surface::scope(disc);') &&
  disc.includes('const uint32_t rgb = surface_icon_color(icon);'), 'circles and icons on the card surface');
assert.ok(disc.includes('return tone_color::jewel_fill(card, rgb, percent, tile_surface::strength(host));') &&
  disc.includes('const uint32_t over = tone_color::circle_source(tinted || !icon_color_tints(shown) ? shown : ui_theme::icon());'),
  'the filled circle and the white card circle');
const source = read('src/tiles/runtime/tile_icon_source.cpp');
assert.ok(source.includes('tint = idle ? tone_color::jewel_idle_card() : tone_color::jewel_card(color, percent);') &&
  !source.includes('border_rim') && !source.includes('g_idle_fill'), 'a filled card: from its icon or the idle grey');
for (const [file, count] of [['src/tiles/runtime/tile_renderer.cpp', 5], ['src/types/binary_sensor/renderer.cpp', 1],
  ['src/types/cover/renderer.cpp', 1], ['src/types/device/device_tile.cpp', 1]]) {
  assert.equal((read(file).match(/State colors follow the tile's surface/g) || []).length, count, `${file}: update scopes`);
}
const settings = read('src/ui/tabs/settings/settings_screen.cpp');
assert.ok(settings.includes('return ui_theme::theme_light() && index == static_cast<uint8_t>(g_category) ? ui_theme::kDarkTintedSurface : -1;') &&
  settings.includes('const uint32_t tile = tone_color::jewel_card(color, 0);'), 'the selected Settings category is filled');
const popup = read('src/ui/popups/light/light_popup.cpp');
assert.ok(popup.includes('ctx->brightness_draw_color = tone_color::light_accent(icon_rgb);') &&
  popup.includes('return ctx->available && ctx->is_on ? tone_color::light_icon(rgb) : rgb;') &&
  popup.includes('if (ui_theme::light() || !on) return lv_color_hex(ui_theme::text());') &&
  popup.includes('lv_color_hex(tone_color::notch_on(ctx->brightness_draw_color, lv_color_to_u32(brightness_dash_color(ctx))));') &&
  popup.includes('lv_obj_set_style_border_color(cursor, lv_color_hex(ui_theme::kDark.text), 0);'),
  'Light popup: fill, header icon, power glyph, handle line, color cursor');
const shell = read('src/ui/popups/popup_shell.cpp');
assert.ok(shell.includes('tile_icon_source::popup_tile_circle(rgb, tile_circle, tile_circle_opa, shown_icon)') &&
  shell.includes('const lv_color_t readable = lv_color_hex(shown_icon);'), 'the header circle and icon are the tile\'s');
// On its tile's card the tile's circle; on the neutral popup card (Light,
// Climate, Cover) exactly the controls' fill, the selected mode button.
assert.ok(shell.includes('const bool on_tile_card = popup_surface::g_open_surface != ui_theme::kThemeSurface;') &&
  shell.includes('fill.disc_color = fill.control_color;'), 'the header circle on a neutral popup: its controls\' fill');
// Unavailable: one grey for every domain in the light theme, like Home
// Assistant; the dark theme keeps each type's grey (device == preview).
const visual = read('src/types/device/device_visual.h');
assert.equal((visual.match(/v\.color = ui_theme::unavailable_icon\(v\.color\);/g) || []).length, 3, 'lock, alarm, fan unavailable');
assert.ok(read('src/types/cover/renderer.cpp').includes(
  'if (state.valid && !state.available) return ui_theme::unavailable_icon(cover_inactive());') &&
  read('src/ui/popups/cover/cover_popup.cpp').includes(
  'if (state.valid && !state.available) return ui_theme::unavailable_icon(ha_cover_inactive());'),
  'cover unavailable: tile and popup');
// An unavailable Lock or Alarm shows no "unlock is off" hint.
assert.match(read('src/ui/popups/device/device_popup.cpp'),
  /if \(d\.valid && !d\.available\) return nullptr;\s+return device_control::blocked_reason\(pop\.target\.type, d, nullptr\);/,
  'no permission hint while unavailable');
// The open surface stays set after a popup closes (V2 b365: after a filled
// Alarm tile the white Lock tile's popup got the dark default card under
// light text). A popup without a tile resets it, and no default card is read
// under the previous popup's surface.
const devicePopup = read('src/ui/popups/device/device_popup.cpp');
assert.match(devicePopup, /uint32_t theme_default_card\(\) \{\s*const ui_theme::SurfaceScope theme = popup_surface::theme_scope\(\);\s*return popup_surface::card_or_default\(tileDefaultBgColor\(\)\);/,
  'device popup: the default card read with the theme');
assert.match(devicePopup, /void show_device_popup\(const DevicePopupTarget& target\) \{\s*\/\/[^\n]*\n\s*const ui_theme::SurfaceScope popup_scope = popup_surface::theme_scope\(\);/,
  'device popup entry: the theme until its opener sets the surface');
assert.equal((devicePopup.match(/if \(!(?:pop|g_code)\.target\.source\) tile_icon_source::open_popup_without_tile\(\);/g) || []).length, 2,
  'device popup and code entry without a tile: the theme');
const srcText = fs.readdirSync(path.join(root, 'src'), {recursive: true})
  .filter(f => /\.(cpp|h)$/.test(f)).map(f => read(path.join('src', f))).join('\n');
assert.ok(!/popup_background\([^,;]*,\s*tileDefaultBgColor\(\)\)/.test(srcText), 'no default card evaluated before its opener');
assert.match(read('src/ui/ui_manager.cpp'), /pending_access_kind = PendingAccessKind::Settings;\s*pending_access_folder_id = 0;\s*\/\/[^\n]*\n\s*tile_icon_source::open_popup_without_tile\(\);/,
  'Settings PIN: never the last popup\'s surface');
assert.match(read('src/ui/navigation/view_navigation.cpp'), /tile_icon_source::open_popup_without_tile\(\);\s*uiManager\.requestFolderAccess\(next, folder->name, folder->icon_name\);/,
  'folder PIN from view navigation: the theme');
// A hidden popup that refreshes draws with the surface it was last shown with
// (V2 b366: black day names on a filled Weather tile's popup after a forecast
// arrived while another popup's surface was open).
assert.match(read('src/ui/popups/popup_shell.cpp'),
  /int8_t popup_drawn_surface\(lv_obj_t\* root\) \{\s*for \(const SurfaceMark& entry : g_surface_marks\) \{\s*if \(root && entry\.root == root\) return entry\.surface < 0 \? ui_theme::kThemeSurface : entry\.surface;/,
  'the surface a popup was last shown with');
assert.match(read('src/ui/popups/weather/weather_popup.cpp'),
  /void process_weather_popup_queue\(\) \{(?:\s*\/\/[^\n]*)*\s*const bool shown = g_weather_popup_ctx && \(g_weather_open_pending \|\| is_popup_visible\(g_weather_popup_ctx\)\);\s*const ui_theme::SurfaceScope popup_scope\(shown \|\| !g_weather_popup_ctx\s*\? popup_surface::g_open_surface\s*: popup_drawn_surface\(g_weather_popup_ctx->card\)\);/,
  'a hidden Weather refresh: its own last surface');
assert.ok(theme.includes('inline uint32_t unavailable_icon(uint32_t dark_grey) { return theme_light() ? icon_unavailable() : dark_grey; }') &&
  grid.includes("return panelThemeLight() ? panelRoleHex('icon-unavailable', '#BDBDBD') : darkGrey;") &&
  read('src/types/device/admin.js').includes('if (!d.available && typeof unavailableIconHex === \'function\') out.color = unavailableIconHex(out.color);') &&
  read('src/types/cover/admin.js').includes("return typeof unavailableIconHex === 'function' ? unavailableIconHex(rest) : rest;"),
  'preview: the same unavailable grey');
// The light popup's controls are a white card tile's circle (20 % of the icon).
assert.ok(shell.includes('const uint32_t over = tone_color::circle_source(tinted || !tile_tint::has_hue(shown) ? shown : ui_theme::icon());') &&
  shell.includes('return tone_color::tile_fill(card, over, tinted, ui_surface_style::icon_glow_percent());'),
  'light popup controls = the tile circle');
assert.ok(read('src/types/media/renderer.cpp').includes('if (primary) tile_icon_disc::mark_cutout(label);') &&
  source.includes('tile_icon_disc::refresh_cutouts(card);'), 'the play glyph follows the tinted card');

// The preview's functions, exactly as delivered.
const names = ['toneToLinear', 'toneToSrgb', 'toneOklch', 'toneLinear', 'toneRgb', 'toneBlend', 'toneHex', 'panelVar',
  'panelLight', 'panelThemeLight', 'panelRole', 'panelRoleHex', 'surfaceRoleTable', 'toneLightSurface', 'toneLightAccent',
  'toneHaLightColor', 'toneLightShown', 'toneMarkOn', 'toneNotchOn', 'toneNoColor', 'jewelVar', 'toneJewelIdleCard',
  'toneJewelCard', 'toneJewelFill', 'toneReadableIcon'];
const js = new Function(names.map(extractDeliveredFunction).join('\n') +
  '; return {toneOklch, toneLightSurface, toneLightAccent, toneHaLightColor, toneLightShown, toneMarkOn, toneNotchOn, toneNoColor,' +
  ' toneBlend, toneJewelIdleCard, toneJewelCard, toneJewelFill, toneReadableIcon};')();
const light = run => { globalThis.hometilesPreviewSurface = 1; try { return run(); } finally { delete globalThis.hometilesPreviewSurface; } };
const dark = run => { globalThis.hometilesPreviewSurface = 0; try { return run(); } finally { delete globalThis.hometilesPreviewSurface; } };
// A page in the light theme (panelThemeLight): the preview of a light panel.
const lightPage = run => {
  globalThis.document = {documentElement: {dataset: {panelTheme: 'light'}}};
  try { return run(); } finally { delete globalThis.document; }
};
const rgb = hex => [16, 8, 0].map(s => (hex >> s) & 255);
const hex = c => (c[0] << 16) | (c[1] << 8) | c[2];

const host = await lvglHost(root);
if (!host) { console.log('SKIP: light surface rules need a C++ compiler'); process.exit(0); }
const out = path.join(root, 'build/tests/light-surface-rules');
fs.mkdirSync(out, {recursive: true});
const surfaces = [0x43A047, 0xFFE082, 0x3D58A9, 0xFFFFFF, 0x1A1A1A, 0xF8BBD0];
const accents = [0xFFFFFF, 0xFFFEFA, 0xFFC107, 0x2196F3, 0x8B8B8B, 0xFFE0C0, 0xC0D0FF];
const icons = [0xFFC107, 0x2196F3, 0x43A047, 0xE53935, 0x9C27B0, 0xFFE082, 0xFF9800, 0xFFFFFF, 0xFFFEFA, 0xE1E1E1, 0x6A1B9A];
const strengths = [0, 20, 30];
const cpp = `#include <cstdio>
#include <initializer_list>
#include "src/ui/shared/tone_color.h"
int main() {
  const unsigned surfaces[] = {${surfaces.map(v => v + 'u').join(', ')}};
  const unsigned accents[] = {${accents.map(v => v + 'u').join(', ')}};
  const unsigned icons[] = {${icons.map(v => v + 'u').join(', ')}};
  const unsigned strengths[] = {${strengths.map(v => v + 'u').join(', ')}};
  for (unsigned s : surfaces) std::printf("surface %u %d\\n", s, tone_color::light_surface(s) ? 1 : 0);
  ui_theme::set(1);
  std::printf("grey %u\\n", ui_theme::icon());
  for (unsigned a : accents) {
    std::printf("light %u %u %u %u %u %u\\n", a, tone_color::light_accent(a), tone_color::mark_on(tone_color::light_accent(a), 0xFFFFFFu),
                tone_color::light_icon(a), tone_color::notch_on(tone_color::light_accent(a), 0xFFFFFFu), tone_color::ha_light_color(a));
  }
  {
    const ui_theme::SurfaceScope surface(0);
    for (unsigned a : accents) {
      std::printf("dark %u %u %u %u\\n", a, tone_color::light_accent(a), tone_color::mark_on(a, 0x2A2A2Au), tone_color::light_icon(a));
    }
  }
  std::printf("restored %d\\n", ui_theme::light() ? 1 : 0);
  // The white card's circle: the icon's color at 20 %, a white light's grey the neutral grey.
  for (unsigned icon : {0xFFC107u, 0xE1E1E1u, 0xE1E1DDu}) {
    const tone_color::Fill fill = tone_color::tile_fill(0xFFFFFFu, tone_color::circle_source(icon), true, 25);
    std::printf("tile %u %u %u %u\\n", icon, fill.disc, fill.control, fill.raised);
  }
  std::printf("idle %u\\n", tone_color::jewel_idle_card());
  for (unsigned icon : icons) {
    for (unsigned strength : strengths) {
      const unsigned card = tone_color::jewel_card(icon, strength);
      const tone_color::Fill circle = tone_color::jewel_fill(card, icon, 25, strength);
      std::printf("jewel %u %u %u %u %u %u\\n", icon, strength, card, circle.disc, circle.control, circle.raised);
    }
    std::printf("theme %u %u\\n", icon, tone_color::readable_icon(icon));
    {
      const ui_theme::SurfaceScope filled(ui_theme::kDarkTintedSurface);
      std::printf("icon %u %u\\n", icon, tone_color::readable_icon(icon));
    }
    {
      // A tile in an own dark color (dark rules) in the light theme.
      const ui_theme::SurfaceScope own(0);
      std::printf("owndark %u %u\\n", icon, tone_color::readable_icon(icon));
    }
  }
  return 0;
}
`;
const file = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(file, cpp);
let result = spawnSync(host.cxx, ['-std=c++17', '-I', root, file, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stderr);
const near = (a, b) => rgb(a).every((v, i) => Math.abs(v - rgb(b)[i]) <= 1);
const lines = result.stdout.trim().split(/\r?\n/).map(line => line.split(' '));
const rows = kind => lines.filter(line => line[0] === kind).map(line => line.slice(1).map(Number));
const value = kind => rows(kind)[0];
for (const [s, isLight] of rows('surface')) {
  assert.equal(isLight === 1, js.toneLightSurface(rgb(s)), `surface #${s.toString(16)}`);
}
for (const [a, accent, mark, , notch, ha] of rows('light')) {
  light(() => {
    assert.ok(near(accent, hex(js.toneLightAccent(rgb(a)))), `light accent #${a.toString(16)}`);
    assert.ok(near(ha, hex(js.toneHaLightColor(rgb(a)))), `ha light color #${a.toString(16)}`);
    assert.ok(near(mark, hex(js.toneMarkOn(js.toneLightAccent(rgb(a)), [255, 255, 255]))), `light mark #${a.toString(16)}`);
    assert.ok(near(notch, hex(js.toneNotchOn(js.toneLightAccent(rgb(a)), [255, 255, 255]))), `handle line #${a.toString(16)}`);
  });
  // The tile icon of a light (switch_state_icon_color) == its popup accent.
  lightPage(() => assert.ok(near(ha, parseInt(js.toneLightShown('#' + a.toString(16).padStart(6, '0')).slice(1), 16)),
    `tile light color #${a.toString(16)}`));
}
for (const [a, accent, mark] of rows('dark')) {
  dark(() => {
    assert.ok(near(accent, hex(js.toneLightAccent(rgb(a)))), `dark accent #${a.toString(16)}`);
    assert.ok(near(mark, hex(js.toneMarkOn(rgb(a), [42, 42, 42]))), `dark mark #${a.toString(16)}`);
  });
}
// Home Assistant's rule: white and almost white a grey of value 225, a pale
// color raised to saturation 0.4, colors stay.
const lightRows = rows('light');
const hsv = c => { const m = Math.max(...rgb(c)); return {v: m, s: m ? (m - Math.min(...rgb(c))) / m : 0}; };
assert.equal(lightRows[0][1], 0xE1E1E1);
assert.ok(hsv(lightRows[1][1]).v === 225 && hsv(lightRows[1][1]).s < 0.1, 'almost white: grey of value 225');
assert.equal(lightRows[2][1], 0xFFC107);
assert.equal(lightRows[3][1], 0x2196F3);
for (const row of lightRows.slice(5)) {
  assert.ok(Math.abs(hsv(row[1]).s - 0.4) < 0.01 && hsv(row[1]).v === 255, `pale #${row[0].toString(16)} at saturation 0.4`);
}
// The popup header icon of a light: exactly the accent, white light too.
for (const row of lightRows) assert.equal(row[3], row[1], `icon = accent #${row[0].toString(16)}`);
// The handle line on a light fill is the card's color (white here).
assert.equal(lightRows[2][4], 0xFFFFFF, 'handle line on amber: the card');
// The dark theme keeps its cut-out on light accents and colors, no accent.
const darkRows = rows('dark');
assert.deepEqual(darkRows.map(row => row[1]), accents, 'no light accent in the dark theme');
assert.deepEqual(darkRows.map(row => row[3]), accents, 'no light icon in the dark theme');
assert.equal(value('restored')[0], 1, 'the surface scope closes again');
// The white card's circle: 20 % amber; a white light's grey: 20 % #BDBDBD.
const tileRow = icon => rows('tile').find(r => r[0] === icon);
const [, amberDisc, amberControl, amberRaised] = tileRow(0xFFC107);
assert.ok(near(amberDisc, hex(js.toneBlend([255, 255, 255], [255, 193, 7], 51))), `tile circle #${amberDisc.toString(16)}`);
assert.equal(amberControl, amberDisc);
assert.ok(near(amberRaised, hex(js.toneBlend([255, 255, 255], [255, 193, 7], 102))));
for (const icon of [0xE1E1E1, 0xE1E1DD]) {
  const [, whiteDisc] = tileRow(icon);
  assert.ok(near(whiteDisc, hex(js.toneBlend([255, 255, 255], [189, 189, 189], 51))), `white light circle #${whiteDisc.toString(16)}`);
  assert.ok(js.toneNoColor(rgb(icon)), `#${icon.toString(16)} has no color`);
}
// Filled tiles: device == preview; the card below its icon (at most 0.70),
// the circle above the card, in the icon's hue; white light neutral grey.
assert.ok(near(value('idle')[0], hex(js.toneJewelIdleCard())), 'the idle grey');
for (const [icon, strength, card, circle, control, raised] of rows('jewel')) {
  const what = `#${icon.toString(16)} at ${strength}`;
  assert.ok(near(card, hex(js.toneJewelCard(rgb(icon), strength))), `card ${what}`);
  const previewFill = js.toneJewelFill(rgb(card), rgb(icon), 25, strength);
  assert.ok(near(circle, hex(previewFill.disc)) && near(control, hex(previewFill.control)), `circle ${what}`);
  const L = c => js.toneOklch(rgb(c)).L;
  // At least L 0.42 (ui_theme kJewelCardMinL, user 2026-10-19: a dark cover
  // made the card almost black), so a dark icon's card may sit at the floor.
  assert.ok(L(card) <= 0.705 && (L(card) <= L(icon) - 0.15 || Math.abs(L(card) - 0.42) < 0.01),
    `card below its icon or at the floor ${what}`);
  assert.ok(L(circle) > L(card) + 0.03 && L(raised) > L(control), `circle above the card ${what}`);
  if (js.toneNoColor(rgb(icon))) {
    const seed = js.toneOklch(rgb(card));
    assert.ok(Math.abs(seed.L - 0.63) < 0.01 && seed.C < 0.005, `white light: neutral grey ${what}`);
    assert.ok(js.toneOklch(rgb(circle)).C < 0.005, `white light: neutral circle ${what}`);
    assert.ok(L(value('idle')[0]) < seed.L, `off is a little darker than white light ${what}`);
  }
}
// A stronger tint is more colorful where the cap holds (a deep red);
// strength 0 is the default (Settings).
const chroma = (icon, s) => js.toneOklch(rgb(rows('jewel').find(r => r[0] === icon && r[1] === s)[2])).C;
assert.ok(chroma(0xE53935, 30) > chroma(0xE53935, 20), 'strength raises the chroma');
// User 2026-10-11: at the default strength a filled card shows its icon's
// color up to the middle cap 0.10 (0.07 looked dull, 0.14 too strong): green
// no olive, blue no grey-blue.
for (const icon of [0x2196F3, 0x43A047, 0xFF9800]) {
  const wanted = Math.min(js.toneOklch(rgb(icon)).C * 0.85, 0.10);
  assert.ok(Math.abs(chroma(icon, 20) - wanted) < 0.012 && chroma(icon, 20) > 0.085, `#${icon.toString(16)} in its own color`);
}
assert.equal(rows('jewel').find(r => r[0] === 0x2196F3 && r[1] === 0)[2], rows('jewel').find(r => r[0] === 0x2196F3 && r[1] === 20)[2]);
// Icons: every colored icon keeps its color on every surface; on a white
// card only a pure white icon takes the icon grey, Home Assistant's light
// grey of a white light stays.
const grey = value('grey')[0];
for (const [icon, shown] of rows('theme')) {
  const colored = rgb(icon).some((v, i, a) => v !== a[0]);
  if (colored) assert.equal(shown, icon, `white card: icon #${icon.toString(16)} unchanged`);
  if (icon === 0xFFFFFF) assert.equal(shown, grey, 'white card: pure white takes the icon grey');
  if (icon === 0xE1E1E1) assert.equal(shown, icon, 'white card: a white light\'s grey stays');
  lightPage(() => light(() => {
    if (colored || icon === 0xE1E1E1) assert.equal(hex(js.toneReadableIcon(rgb(icon))), shown, `preview icon #${icon.toString(16)}`);
  }));
}
for (const [icon, shown] of rows('icon')) {
  if (rgb(icon).some((v, i, a) => v !== a[0])) assert.equal(shown, icon, `filled: icon #${icon.toString(16)} unchanged`);
}
for (const [icon, shown] of rows('owndark')) {
  if (rgb(icon).some((v, i, a) => v !== a[0])) assert.equal(shown, icon, `own dark color: icon #${icon.toString(16)} unchanged`);
}

// The preview marks the surface and draws it like the device.
assert.ok(grid.includes("if (surface === 'jewel') return 2;") &&
  grid.includes("if (table) el.style.setProperty('--pv-' + name, 'var(--pv' + table + '-' + name + ')');") &&
  grid.includes('if (surfaceMode >= 0) globalThis.hometilesPreviewSurface = surfaceMode;'),
  'preview surfaces, surface 2 included');
assert.ok(read('src/web/admin/tiles/icon-colors.js').includes("if (surface === 'jewel' && typeof toneJewelCard === 'function') {"),
  'preview fills a tinted tile');
assert.doesNotMatch(grid, /darkdesign|darktint|toneDarkDesign/, 'no dark design in the preview');
assert.ok(read('src/web/assets/admin.css').includes('color:var(--tile-cut, var(--tile-card, #1A1A1A)); }'), 'preview play cut-out');
// The camera popup's status line (FPS, user 2026-10-11: invisible on the
// light card) takes the popup surface's secondary text, no fixed light grey.
const cameraStream = read('src/video/camera_stream.cpp');
assert.ok(cameraStream.includes('status_error ? lv_color_hex(0xFF6B6B) : lv_color_hex(ui_theme::text_secondary()), 0);') &&
  !cameraStream.includes('0xD8DEE9'), 'camera status in the secondary text role');
console.log('Light surface rules: fixed surfaces, filled and idle cards, greys, Home Assistant light colors, circles, handle line, device == preview');
