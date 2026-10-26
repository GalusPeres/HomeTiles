// Filled tiles in the light theme (user 2026-10-10, "Hell = Dunkel-Rezept";
// 2026-10-11: one color rule): a tile with "Fill" is filled from the start
// (tile_surface::kJewel, ui_theme::kDarkTinted: white text, a white second
// line), with its icon's own color (white light in a neutral grey) or,
// without a color, the idle grey (kNeutralIdle). Only a rule row with its
// own "Fuellen" (user 2026-10-11) changes it to the white card while that row
// keeps the tile unfilled; every other tile keeps the theme's white card with
// the fixed frame line. The frame line of a filled card follows the card. A
// popup opened from a filled tile draws with its rules
// (popup_surface::g_open_surface), every other popup with the theme. Runs the
// production code on real LVGL objects.
import {popupSurfaceHost, radiusPolicyHost, surfaceStyleHost} from '../../lib/surface-style-host.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const source = read('src/tiles/runtime/tile_icon_source.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert(found, name);
  return found.source;
};

// The hooks: the surface changes before a tint is computed and before it is
// cleared, so the tint takes the new surface's rules.
assert.match(fn('tint_card'), /follow_tint\(card, true\);[\s\S]*SurfaceScope surface = tile_surface::scope\(card\);/);
assert.match(fn('clear_tile_tint'), /follow_tint\(card, false\);[\s\S]*kTintStore\) != LV_STYLE_RES_FOUND\) return;/);
// A Camera tile's picture: the frame line over it, everything placed inside
// that border (types/camera/picture.cpp).
const picture = read('src/types/camera/picture.cpp');
assert.ok(picture.includes('ui_surface_style::draw_frame_over_children(card);') &&
  picture.includes('x - lv_obj_get_style_space_left(card, LV_PART_MAIN)') &&
  picture.includes('y - lv_obj_get_style_space_top(card, LV_PART_MAIN)'), 'camera picture: the frame line over the picture');
// The renderer gives a tile with "Fill" the rule in the light theme
// (tile_surface_rule_of), not the screensaver, empty places or own colors.
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
// A Camera tile's head over its picture keeps its fixed surface (no tint
// rule; types/camera/picture.h head_over_picture).
assert.match(renderer, /static uint8_t tile_surface_rule_of\(const Tile& tile, GridType grid_type\) \{\s*if \(grid_type == GridType::SCREENSAVER \|\| tile\.type == TILE_EMPTY \|\| !ui_theme::theme_light\(\) \|\|\s*tile_has_own_color\(tile\) \|\| camera_picture::head_over_picture\(tile\)\) \{\s*return 0;\s*\}/);
// A tile "From icon" and a Media tile are filled from the start and stay
// filled: no later surface change (user 2026-10-10: colors built up late).
assert.ok(renderer.includes('if (tile_surface_rule_of(tile, grid_type) & tile_surface::kNeutralIdle) return tile_surface::kJewel;') &&
  renderer.includes('tile_surface::set_rule(tile_obj, surface == tile_surface::kJewel ? tile_surface::kJewel : tile_surface::kNone,') &&
  renderer.includes('tile_icon_source::settle_filled(tile_obj);'), 'render_tile: the surface and rule before the first tint');
// No option and no per-tile Design any more (b343 removed with b344).
for (const file of ['src/core/config/config_manager.cpp', 'src/core/config/config_manager.h', 'src/web/server/web_admin.cpp',
  'src/web/server/handlers/web_admin_handlers.cpp', 'src/web/server/render/web_admin_html.cpp', 'src/core/i18n/i18n.cpp',
  'src/web/admin/settings/display-borders.js']) {
  assert.doesNotMatch(read(file), /dark_tint|DarkTinted\(|dark-tinted|darkTinted/, `${file}: no dark tinted option`);
}
for (const file of ['src/tiles/config/tile_config.h', 'src/tiles/config/tile_config.cpp', 'src/web/server/handlers/web_admin_tiles.cpp',
  'src/web/admin/tiles/snapshots.js', 'src/web/admin/tiles/import-export.js', 'src/web/admin/tiles/grid-preview.js']) {
  assert.doesNotMatch(read(file), /TileDesign|TILE_DESIGN|tile\.design\b|normalizeTileDesign|'design'/, `${file}: no per-tile Design`);
}
// The preview fills the same way (icon-colors.js setTileTintBackground) and
// leaves it with the tint (grid-preview.js).
const colors = read('src/web/admin/tiles/icon-colors.js');
assert.ok(colors.includes("if (el?.dataset?.followTint === '1' && el.dataset.surface !== 'jewel' && typeof applyTileSurface === 'function') {") &&
  colors.includes("const card = idle ? toneJewelIdleCard() : toneJewelCard(given, Number(percent) || 20);"), 'preview: the tint fills the tile');
assert.ok(!read('src/web/admin/tiles/grid-preview.js').includes("applyTileSurface(tileElem, '');"),
  'preview: a filled tile never leaves its surface');
// White and almost white light fill in the neutral grey (with_white, device
// and preview); a tile "From icon" or a Media tile "From cover" shows it
// without a tint. A Media tile in Global is a white card like every tile
// (user 2026-10-11: no Media design left).
// Only Home Assistant's light grey of a white light, not a pure white icon.
assert.match(fn('with_white'), /if \(tone_color::no_color\(icon\) && tone_color::from_rgb\(icon\)\.L <= tone_color::kWhiteIconL\) \{\s*choice\.color = icon;\s*choice\.percent = fill;/);
// Without a color the idle grey (a little darker than white light).
assert.match(fn('clear_tile_tint'), /\(flags & tile_surface::kNeutralIdle\) && !plain\) \{\s*tint_card\(card, 0xFFFFFF, 0, true\);\s*return;/);
// A rule row without "Fuellen" shows the plain card, also on a tile filled
// from the start.
assert.match(fn('clear_tile_tint'), /if \(plain && rule\) change_surface\(card, tile_surface::kNone\);\s*else follow_tint\(card, false\);/);
assert.match(fn('tint_card'), /tint = idle \? tone_color::jewel_idle_card\(\) : tone_color::jewel_card\(color, percent\);/);
// A Switch tile (lights, switches) is filled only while on: no idle grey
// (#130, user 2026-10-26).
assert.ok(renderer.includes('return tile.type == TILE_SWITCH ? tile_surface::kFollowTint : tile_surface::kFollowTint | tile_surface::kNeutralIdle;') &&
  renderer.includes('return tile_icon_colors::rows_fill(tile.icon_colors.c_str()) ? tile_surface::kFollowTint : 0;'),
  'Only a tile with Fill or a rule row with Fuellen gets the rule; every other tile keeps its card');
const gridPreview = read('src/web/admin/tiles/grid-preview.js');
// White light fills grey in the dark theme too; a Switch tile has no idle
// grey (#130, user 2026-10-26).
assert.ok(gridPreview.includes("if (!choice && given && (tileElem.dataset.followTint === '1' || !panelThemeLight()) && toneNoColor(given) &&") &&
  gridPreview.includes("if (el.dataset.followTint === '1' && tileFillOf(tile?.icon_colors) > 0 && surfaceType !== '5') {") &&
  colors.includes("const card = idle ? toneJewelIdleCard() : toneJewelCard(given, Number(percent) || 20);"),
  'preview: white light grey, the idle grey without a color');
// The frame line (applyFrameLine, ui_surface_style frame_line) through --tile-border-tint.
assert.ok(read('src/web/assets/admin.css').includes(
  'outline:1px solid var(--tile-border-tint, rgba(var(--pv-text-rgb, 255,255,255),0.20));'),
  'preview: the hairline');
// Popups: the opener sets the open surface; the Light, Climate and Cover
// popups (neutral cards) keep the theme, the others take the tile's rules.
assert.match(fn('popup_background'), /^uint32_t popup_background\(lv_obj_t\* obj, uint32_t fallback\) \{\s*set_open_popup_surface\(obj\);/);
assert.match(fn('forget_popup_source'), /popup_surface::g_open_surface = ui_theme::kThemeSurface;/);
assert.match(fn('open_popup_without_tile'), /popup_surface::g_open_surface = ui_theme::kThemeSurface;/);
const scoped = 'const ui_theme::SurfaceScope popup_scope = popup_surface::scope();';
const themed = 'const ui_theme::SurfaceScope popup_scope = popup_surface::theme_scope();';
const opens = {weather: 'show_weather_popup', media: 'show_media_popup', sensor: 'show_sensor_popup', energy: 'show_energy_popup',
  device: 'show_device_popup', camera: 'show_camera_popup', pin: 'show_pin_popup',
  light: 'show_light_popup', climate: 'show_climate_popup', cover: 'show_cover_popup'};
for (const [popup, name] of Object.entries(opens)) {
  const file = `src/ui/popups/${popup}/${popup}_popup.cpp`;
  const text = read(file);
  assert.ok(text.includes('#include "src/ui/popups/popup_surface.h"'), `${file}: popup_surface`);
  // The device popup's opener is open_for (popup_background): its entry
  // draws with the theme until then (V2 b365: a stale surface).
  const opener = popup === 'device' ? 'open_for' : name;
  const body = cppFunctionDefinitions(text).find(f => f.name === opener)?.source || '';
  const neutral = ['light', 'climate', 'cover'].includes(popup);
  assert.ok(body.includes(neutral ? themed : scoped), `${name}: ${neutral ? 'theme' : 'open surface'}`);
  if (neutral) assert.ok(!text.includes(scoped), `${file}: never the tile's surface`);
  if (popup === 'device') {
    assert.ok((cppFunctionDefinitions(text).find(f => f.name === name)?.source || '').includes(themed), `${name}: theme`);
  }
}
// Updates of the Light popup from its tile (tile scope open) draw with the theme.
const lightPopup = read('src/ui/popups/light/light_popup.cpp');
for (const name of ['update_light_popup', 'light_popup_follow_tile_color']) {
  assert.ok((cppFunctionDefinitions(lightPopup).find(f => f.name === name)?.source || '').includes(themed), `${name}: theme`);
}
const shell = read('src/ui/popups/popup_shell.cpp');
assert.ok(shell.includes('void popup_follow_surface(lv_obj_t* root) {') &&
  shell.includes('tile_icon_source::recolor_surface(root, mark->surface, want);'), 'popup texts follow the open surface');
// Remembered objects clear themselves when deleted (crash b364): a popup
// body's surface mark, the opening tile's circle.
assert.ok(shell.includes('lv_obj_add_event_cb(root, surface_root_deleted, LV_EVENT_DELETE, nullptr);') &&
  source.includes('if (disc) lv_obj_add_event_cb(disc, forget_popup_disc, LV_EVENT_DELETE, nullptr);'),
  'remembered objects clear themselves on delete');
// LVGL 9.6's lv_obj_is_valid walks the object's own parents: it reads a freed
// object instead of rejecting it, so it never guards a remembered pointer.
const srcFiles = fs.readdirSync(path.join(root, 'src'), {recursive: true})
  .filter(f => /\.(c|cpp|h)$/.test(f));
const validChecks = srcFiles.filter(f => /\blv_obj_is_valid\s*\(/.test(read(path.join('src', f))));
assert.deepEqual(validChecks, [], 'no lv_obj_is_valid on remembered pointers');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: filled tile surface needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/filled-tile-surface');
fs.mkdirSync(out, {recursive: true});
const fonts = read('src/tiles/runtime/tile_renderer_fonts.h').replace(/^#include.*$/gm, '')
  .replace('#pragma once', '').replaceAll('constexpr lv_coord_t', 'constexpr long');
const tintStore = source.match(/constexpr lv_style_selector_t kTintStore = [^;]*;/)[0];
const cpp = String.raw`
#include <lvgl.h>
#include <atomic>
#include <cstdio>
#include <string>
#include "src/ui/shared/title_label.h"
extern "C" { LV_FONT_DECLARE(ui_font_12);LV_FONT_DECLARE(ui_font_14);LV_FONT_DECLARE(ui_font_16);LV_FONT_DECLARE(ui_font_20);LV_FONT_DECLARE(ui_font_24);LV_FONT_DECLARE(ui_font_28);LV_FONT_DECLARE(ui_font_32);LV_FONT_DECLARE(ui_font_40);LV_FONT_DECLARE(mdi_icons_32);LV_FONT_DECLARE(mdi_icons_48); }
${fonts}
#define FONT_MDI_ICONS (&mdi_icons_48)
${radiusPolicyHost(root)}
#include "src/core/config/icon_glow.h"
struct Config{bool tile_borders=true;bool icon_discs=true;uint8_t icon_glow=icon_glow::kDefault;int tile_radius=tile_radius::kMinimum;const char*language="en";};struct Manager{Config cfg;const Config&getConfig(){return cfg;}}configManager;
${surfaceStyleHost(root)}
${popupSurfaceHost(root, {recolor: false})}
constexpr int GRID_CELL_H=CELL_H,GRID_GAP=GAP;
#include "src/types/climate/layout.h"
#include "src/tiles/runtime/tile_press.h"
${read('src/tiles/runtime/tile_icon_disc.h').replace(/^#.*$/gm, '')}
#include "src/tiles/config/tile_tint.h"
// The default card of the surface (tile_config.cpp tileDefaultBgColor).
static uint32_t tileDefaultBgColor() { return ui_theme::light() ? ui_theme::card() : 0x1A1A1A; }
struct Tile {};
static uint32_t tileBgColorOrDefault(const Tile&, uint32_t fallback) { return fallback; }
namespace tile_icon_source {
${tintStore}
void change_surface(lv_obj_t* card, uint8_t surface, const Tile* tile = nullptr);
void set_card_base(lv_obj_t* card, uint32_t rgb);
void clear_tile_tint(lv_obj_t* card, bool plain = false);
${fn('apply_card_background')}
${fn('set_open_popup_surface')}
${fn('follow_tint')}
${fn('tint_card')}
${fn('set_tile_tint')}
${fn('clear_tile_tint')}
${fn('icon_glyph')}
${fn('recolor_roles')}
${fn('change_surface')}
${fn('set_card_base')}
lv_obj_t* g_popup_disc = nullptr;
${fn('forget_popup_disc')}
${fn('set_popup_disc')}
${fn('popup_tile_circle')}
}
static uint32_t rgb(lv_color_t c) { return lv_color_to_u32(c) & 0xFFFFFF; }
static uint32_t text(lv_obj_t* obj) { return rgb(lv_obj_get_style_text_color(obj, LV_PART_MAIN)); }
static uint32_t bg(lv_obj_t* obj) { return rgb(lv_obj_get_style_bg_color(obj, LV_PART_MAIN)); }
static uint32_t frame_line(lv_obj_t* obj) { return rgb(lv_obj_get_style_outline_color(obj, LV_PART_MAIN)); }
// The frame line of a card that is not the theme's plain card: a little
// lighter on a dark card, a little darker on a light one (frame_line).
// A filled card's line: darker on the light theme's screen, lighter on the
// dark one (user 2026-10-11).
static uint32_t expected_line(uint32_t card) {
  return rgb(ui_theme::theme_light() ? lv_color_mix(lv_color_black(), lv_color_hex(card), 36)
                                     : lv_color_mix(lv_color_white(), lv_color_hex(card), 23));
}
static int failures = 0;
static void expect(bool ok, const char* what, uint32_t got = 0, uint32_t want = 0) {
  if (ok) return;
  std::printf("FAIL %s: #%06X, expected #%06X\n", what, (unsigned)got, (unsigned)want);
  ++failures;
}
static lv_obj_t* label(lv_obj_t* parent, const char* value, uint32_t color) {
  lv_obj_t* obj = lv_label_create(parent);
  lv_label_set_text(obj, value);
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
  return obj;
}
int main() {
  lv_init();
  static uint32_t px[64 * 64];
  lv_display_t* display = lv_display_create(64, 64);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, px, nullptr, sizeof(px), LV_DISPLAY_RENDER_MODE_FULL);
  ui_theme::set(1);
  const uint32_t amber = 0xFFC107;

  // A tile with "Fill" (render_tile): filled from the start, the idle grey
  // without a color.
  lv_obj_t* card = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(card);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  tile_icon_source::apply_card_background(card, ui_theme::kLight.card);
  tile_surface::set(card, tile_surface::kJewel);
  tile_surface::set_rule(card, tile_surface::kJewel, tile_surface::kFollowTint | tile_surface::kNeutralIdle);
  ui_surface_style::apply_global_tile_border(card);
  lv_obj_t* row = lv_obj_create(card);
  lv_obj_t* title = label(row, "Desk", ui_theme::kDarkTinted.text);
  lv_obj_t* glyph = label(card, "\xF3\xB0\x8C\xB5", ui_theme::kDarkTinted.icon);
  lv_obj_t* note = label(row, "Artist", ui_theme::kDarkTinted.text_secondary);
  lv_obj_t* entity = label(row, "On", amber);
  tile_icon_source::clear_tile_tint(card);
  const uint32_t idle = tone_color::jewel_idle_card();
  expect(tile_surface::get(card) == tile_surface::kJewel && bg(card) == idle, "filled from the start: idle grey", bg(card), idle);
  expect(frame_line(card) == expected_line(idle), "frame line of the idle card", frame_line(card), expected_line(idle));
  // Its second line is white like the title (user 2026-10-11).
  expect(ui_theme::kDarkTinted.text_secondary == ui_theme::kDarkTinted.text, "second line white",
         ui_theme::kDarkTinted.text_secondary, ui_theme::kDarkTinted.text);

  // A state update fills it with the icon's color: the surface stays, the
  // whole update draws with the filled rules.
  {
    const ui_theme::SurfaceScope scope = tile_surface::scope(title);
    expect(ui_theme::text() == ui_theme::kDarkTinted.text, "the update draws filled", ui_theme::text(), ui_theme::kDarkTinted.text);
    tile_icon_source::set_tile_tint(card, amber, 20);
    expect(ui_theme::text() == ui_theme::kDarkTinted.text, "still filled after the tint", ui_theme::text(), ui_theme::kDarkTinted.text);
    // Its greys lie above its circle like the dark theme's (b348).
    expect(ui_theme::icon_off() == ui_theme::kDarkTinted.icon_off, "filled: its own off grey",
           ui_theme::icon_off(), ui_theme::kDarkTinted.icon_off);
    expect(tone_color::readable_icon(amber) == amber, "the icon keeps its own color", tone_color::readable_icon(amber), amber);
  }
  expect(ui_theme::g_surface == -1 && ui_theme::g_surface_owner == nullptr, "scopes close");
  const uint32_t filled = tone_color::jewel_card(amber, 20);
  expect(tile_surface::get(card) == tile_surface::kJewel && bg(card) == filled, "card from the icon", bg(card), filled);
  expect(tile_surface::strength(glyph) == 20, "strength kept for the circle", tile_surface::strength(glyph), 20);
  // The frame line follows the filled card in any scope, but a filled card of
  // the light theme draws none (b391: its darker line looked frayed).
  expect(frame_line(card) == expected_line(filled) && lv_obj_get_style_outline_opa(card, LV_PART_MAIN) == LV_OPA_TRANSP,
         "no frame line on the filled card", frame_line(card), expected_line(filled));
  {
    const ui_theme::SurfaceScope scope = tile_surface::scope(title);
    ui_surface_style::apply_global_tile_border(card);
  }
  expect(frame_line(card) == expected_line(filled), "the same line inside a scope", frame_line(card), expected_line(filled));
  expect(text(title) == ui_theme::kDarkTinted.text, "title white", text(title), ui_theme::kDarkTinted.text);
  expect(text(glyph) == ui_theme::kDarkTinted.icon, "role icon white", text(glyph), ui_theme::kDarkTinted.icon);
  expect(text(note) == ui_theme::kDarkTinted.text, "second line white", text(note), ui_theme::kDarkTinted.text);
  expect(text(entity) == amber, "entity color stays", text(entity), amber);

  // A popup opened from it draws with its rules.
  tile_icon_source::set_open_popup_surface(title);
  expect(popup_surface::g_open_surface == ui_theme::kDarkTintedSurface, "popup of a filled tile", popup_surface::g_open_surface,
         ui_theme::kDarkTintedSurface);
  {
    // The Light popup updated inside the tile's scope keeps the theme.
    const ui_theme::SurfaceScope tile_scope = tile_surface::scope(title);
    const ui_theme::SurfaceScope popup_scope = popup_surface::theme_scope();
    expect(ui_theme::text() == ui_theme::kLight.text, "neutral popup: theme inside a tile scope", ui_theme::text(),
           ui_theme::kLight.text);
  }

  // Off: the idle grey again, still filled (no change of surface).
  tile_icon_source::clear_tile_tint(card);
  expect(tile_surface::get(card) == tile_surface::kJewel && bg(card) == idle, "off: idle grey, still filled", bg(card), idle);
  expect(text(title) == ui_theme::kDarkTinted.text, "title stays white", text(title), ui_theme::kDarkTinted.text);
  expect(frame_line(card) == expected_line(idle), "the line follows back", frame_line(card), expected_line(idle));

  // White and almost white light fill it in the neutral grey.
  const uint32_t grey = tone_color::jewel_card(0xFFFFFFu, 20);
  expect(idle != grey, "off is a little darker than white light", idle, grey);
  for (const uint32_t white : {0xFFFFFFu, 0xFFFEFAu, 0xF5F7FFu}) {
    tile_icon_source::set_tile_tint(card, white, 20);
    expect(tile_surface::get(card) == tile_surface::kJewel && bg(card) == grey && text(title) == ui_theme::kDarkTinted.text,
           "white light: grey", bg(card), grey);
  }
  tile_icon_source::clear_tile_tint(card);

  // A rule row without "Fuellen" (user 2026-10-11): the theme's white card
  // with its dark text and fixed line, also on a tile filled from the start;
  // a row with "Fuellen" fills it again, and without a deciding row it shows
  // the idle grey again.
  tile_icon_source::set_tile_tint(card, amber, 20);
  tile_icon_source::clear_tile_tint(card, true);
  expect(tile_surface::get(card) == tile_surface::kNone && bg(card) == ui_theme::kLight.card, "row without Fuellen: white card",
         bg(card), ui_theme::kLight.card);
  expect(text(title) == ui_theme::kLight.text, "row without Fuellen: dark title", text(title), ui_theme::kLight.text);
  expect(frame_line(card) == 0xDDDDDDu, "row without Fuellen: the plain line", frame_line(card), 0xDDDDDDu);
  tile_icon_source::set_tile_tint(card, amber, 20);
  expect(tile_surface::get(card) == tile_surface::kJewel && bg(card) == filled && text(title) == ui_theme::kDarkTinted.text,
         "a row with Fuellen fills it again", bg(card), filled);
  tile_icon_source::clear_tile_tint(card, true);
  tile_icon_source::clear_tile_tint(card);
  expect(tile_surface::get(card) == tile_surface::kJewel && bg(card) == idle && text(title) == ui_theme::kDarkTinted.text,
         "no deciding row: idle grey again", bg(card), idle);

  // A tile without "Fill": the theme's white card with the fixed line, a
  // popup from it draws with the theme.
  lv_obj_t* white_card = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(white_card);
  lv_obj_set_style_bg_opa(white_card, LV_OPA_COVER, 0);
  tile_icon_source::apply_card_background(white_card, ui_theme::kLight.card);
  tile_surface::set(white_card, tile_surface::kNone);
  ui_surface_style::apply_global_tile_border(white_card);
  lv_obj_t* white_title = label(white_card, "Lamp", ui_theme::kLight.text);
  expect(frame_line(white_card) == 0xDDDDDD && lv_obj_get_style_outline_opa(white_card, LV_PART_MAIN) == LV_OPA_COVER,
         "plain light card: #DDDDDD", frame_line(white_card), 0xDDDDDD);
  tile_icon_source::set_open_popup_surface(white_title);
  expect(popup_surface::g_open_surface == ui_theme::kThemeSurface, "popup of a plain tile: theme");

  // A Light popup opened from a grey tile that is off (Bathroom, user
  // 2026-10-10): its header circle and icon are exactly the tile's, the icon
  // the filled surface's off grey on that circle, not the white popup's.
  lv_obj_t* lamp = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(lamp);
  lv_obj_set_size(lamp, 300, 145);
  lv_obj_set_style_bg_opa(lamp, LV_OPA_COVER, 0);
  tile_icon_source::apply_card_background(lamp, ui_theme::kLight.card);
  tile_surface::set(lamp, tile_surface::kJewel);
  tile_surface::set_rule(lamp, tile_surface::kJewel, tile_surface::kFollowTint | tile_surface::kNeutralIdle);
  tile_icon_source::clear_tile_tint(lamp);
  lv_obj_t* bulb = lv_label_create(lamp);
  lv_obj_set_style_text_font(bulb, FONT_MDI_ICONS, 0);
  lv_label_set_text(bulb, "\xF3\xB0\x8C\xB5");
  // An update outside the tile's surface gave it the theme's off grey.
  lv_obj_set_style_text_color(bulb, lv_color_hex(ui_theme::kLight.icon_off), 0);
  lv_obj_t* bulb_disc = tile_icon_disc::add_round(lamp, bulb);
  tile_icon_disc::apply_fill(bulb_disc);
  expect(text(bulb) == ui_theme::kDarkTinted.icon_off, "grey tile: the off icon on its circle", text(bulb),
         ui_theme::kDarkTinted.icon_off);
  tile_icon_source::g_popup_disc = bulb_disc;
  uint32_t header_circle = 0, header_icon = 0;
  uint8_t header_opa = 0;
  expect(tile_icon_source::popup_tile_circle(ui_theme::kLight.icon_off, header_circle, header_opa, header_icon),
         "the popup finds its tile's circle");
  expect(header_circle == bg(bulb_disc), "popup circle = tile circle", header_circle, bg(bulb_disc));
  expect(header_icon == text(bulb), "popup icon = tile icon", header_icon, text(bulb));

  // Without the rule (an own color, the screensaver) a tinted tile stays.
  lv_obj_t* plain = lv_obj_create(lv_screen_active());
  tile_surface::set(plain, tile_surface::kNone);
  lv_obj_t* plain_title = label(plain, "Lamp", ui_theme::kLight.text);
  tile_icon_source::set_tile_tint(plain, amber, 20);
  expect(tile_surface::get(plain) == tile_surface::kNone && text(plain_title) == ui_theme::kLight.text, "no rule: light");

  // Crash b364: a grid rebuild (a PIN change, a folder switch) deletes the
  // opening tile's circle; the next popup header must not read it.
  lv_obj_t* gone = lv_obj_create(lv_screen_active());
  lv_obj_t* gone_icon = lv_label_create(gone);
  lv_obj_t* gone_disc = tile_icon_disc::add_round(gone, gone_icon);
  tile_icon_source::set_popup_disc(gone_disc);
  expect(tile_icon_source::g_popup_disc == gone_disc, "the popup keeps its tile's circle");
  lv_obj_delete(gone);
  expect(tile_icon_source::g_popup_disc == nullptr, "a deleted tile's circle clears itself");
  uint32_t no_circle = 0, no_icon = 0;
  uint8_t no_opa = 0;
  expect(!tile_icon_source::popup_tile_circle(ui_theme::kLight.icon, no_circle, no_opa, no_icon),
         "no circle after the tile is gone");

  // A Camera tile's picture covers its card (user 2026-10-11: "no frame on
  // the camera tiles"): the frame line becomes a border drawn after the
  // children, in the same color, at rest and pressed; the tile border option
  // only changes its opacity, so the picture never moves. In the light theme
  // the picture draws no line (user 2026-10-12: the light line stepped along
  // the picture's rounded corners, worst on the S3), its width stays.
  lv_obj_t* cam = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(cam);
  lv_obj_set_style_bg_opa(cam, LV_OPA_COVER, 0);
  tile_icon_source::apply_card_background(cam, ui_theme::kLight.card);
  ui_surface_style::apply_tile_border(cam, true);
  ui_surface_style::draw_frame_over_children(cam);
  expect(lv_obj_get_style_border_post(cam, LV_PART_MAIN) && lv_obj_get_style_border_width(cam, LV_PART_MAIN) == 1 &&
         lv_obj_get_style_border_opa(cam, LV_PART_MAIN) == LV_OPA_TRANSP && lv_obj_get_style_outline_width(cam, LV_PART_MAIN) == 0,
         "camera, light theme: no line over the picture, its width stays");
  ui_theme::set(0);
  tile_icon_source::apply_card_background(cam, ui_theme::kDark.card);
  ui_surface_style::apply_tile_border(cam, true);
  expect(lv_obj_get_style_border_opa(cam, LV_PART_MAIN) == LV_OPA_COVER, "camera, dark theme: the line over the picture");
  expect((lv_color_to_u32(lv_obj_get_style_border_color(cam, LV_PART_MAIN)) & 0xFFFFFF) == 0x292929u, "camera: the plain line",
         lv_color_to_u32(lv_obj_get_style_border_color(cam, LV_PART_MAIN)) & 0xFFFFFF, 0x292929u);
  lv_obj_add_state(cam, LV_STATE_PRESSED);
  expect(lv_obj_get_style_border_opa(cam, LV_PART_MAIN) == LV_OPA_COVER, "camera: the line stays while pressed");
  lv_obj_remove_state(cam, LV_STATE_PRESSED);
  ui_surface_style::apply_tile_border(cam, false);
  expect(lv_obj_get_style_border_width(cam, LV_PART_MAIN) == 1 && lv_obj_get_style_border_opa(cam, LV_PART_MAIN) == LV_OPA_TRANSP,
         "camera: borders off hide the line, its width stays");
  ui_surface_style::draw_frame_over_children(cam);
  ui_surface_style::apply_tile_border(cam, true);
  expect(lv_obj_get_style_border_opa(cam, LV_PART_MAIN) == LV_OPA_COVER, "camera: borders on again");
  ui_theme::set(1);

  if (failures) return 1;
  std::printf("OK\n");
  return 0;
}
`;
const src = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(src, cpp);
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-DSCREEN_WIDTH=1280', '-DSCREEN_HEIGHT=800',
  '-DCELL_W=168', '-DCELL_H=145', '-DGAP=16', src, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log('Filled tiles: filled from the start (card, frame line, white texts, updates), white grey, plain cards keep the fixed line, popups by their opener.');
