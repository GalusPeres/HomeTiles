// The UI theme (src/ui/shared/ui_theme.h): every dark role is exactly the
// color the firmware used before the roles, so the dark theme draws the same
// pixels; the light theme mirrors the dark rules (light card, steps down,
// icons lowered until readable) as set with the user in the simulator
// (2026-10-09). The theme is a stored setting read at boot.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');

// Dark roles = the former constants.
const theme = read('src/ui/shared/ui_theme.h');
// One table per theme (the firmware and the Web Admin preview read both).
const roleNames = ['screen', 'card', 'popup_card', 'text', 'icon', 'icon_off', 'icon_inactive', 'icon_rest',
  'text_secondary', 'text_soft', 'icon_unavailable'];
const table = name => theme.match(new RegExp(`inline constexpr Roles ${name} = \\{([^}]*)\\};`))[1]
  .split(',').map(value => value.trim());
// Unavailable (user 2026-10-10): the dark theme keeps its off grey, the light
// theme shows a fainter grey than off, like Home Assistant.
assert.deepEqual(table('kDark'), ['0x000000', '0x1A1A1A', '0x2A2A2A', '0xFFFFFF', '0xFFFFFF', '0xB0B0B0', '0x9E9E9E',
  '0x9E9E9E', '0xD8DEE9', '0xD8D8D8', '0xB0B0B0'], 'dark roles = the former constants');
assert.deepEqual(table('kLight'), ['0xE9EBEE', '0xFFFFFF', '0xFFFFFF', '0x1A1A1A', '0x464646', '0x8B8B8B', '0x8B8B8B',
  '0x464646', '0x5F6368', '0x3A3A3A', '0xBDBDBD'], 'light roles as set in the simulator');
for (const role of roleNames) {
  assert.ok(theme.includes(`inline uint32_t ${role}() { return roles().${role}; }`), `ui_theme::${role}`);
  assert.match(theme, new RegExp(`  uint32_t ${role};`), `Roles::${role}`);
}
for (const factor of ['kLightStepNum = 5;', 'kLightStepDen = 6;', 'kLightTintNum = 4;', 'kLightTintDen = 5;']) {
  assert.ok(theme.includes('inline constexpr int ' + factor), factor);
}
assert.ok(read('src/ui/shared/tone_color.h').includes(
  'if (ui_theme::light()) step = -step * ui_theme::kLightStepNum / ui_theme::kLightStepDen;'));
assert.ok(read('src/tiles/config/tile_tint.h').includes(
  'percent * ui_theme::kLightTintNum / ui_theme::kLightTintDen);'));
assert.ok(read('src/ui/popups/popup_surface.h')
  .includes('inline uint32_t default_card() { return ui_theme::popup_card(); }'));
const style = read('src/ui/tabs/settings/settings_style.h');
for (const [name, light, dark] of [['good_color', '0x019432', '0x51CF66'], ['warn_color', '0xA07004', '0xFFC04D'],
  ['error_color', '0xD03F45', '0xFF6B6B']]) {
  assert.ok(style.includes(`inline uint32_t ${name}() { return ui_theme::light() ? ${light} : ${dark}; }`), name);
}
assert.ok(read('src/ui/shared/tone_color.h').includes('inline uint32_t off_icon() { return ui_theme::icon_off(); }'));

// The screen behind tiles, head and Settings takes the theme's screen color
// (the simulator once painted these over and hid them).
for (const [file, obj, count] of [['src/ui/ui_manager.cpp', 'scr', 1], ['src/ui/ui_manager.cpp', 'tab_content_container', 1],
  ['src/ui/ui_manager.cpp', 'panel', 1], ['src/ui/tabs/settings/tab_settings.cpp', 'tab', 1],
  ['src/ui/tabs/tiles/tab_tiles_unified.cpp', 'grid', 1], ['src/ui/tabs/tiles/tab_tiles_unified.cpp', 'parent', 1]]) {
  const text = read(file);
  assert.equal(text.split(`lv_obj_set_style_bg_color(${obj}, lv_color_hex(ui_theme::screen()), 0);`).length - 1, count, `${file}: ${obj}`);
  assert.ok(!text.includes(`lv_obj_set_style_bg_color(${obj}, lv_color_hex(0x000000), 0);`), `${file}: ${obj} still black`);
}
// No popup falls back to the dark card, and no text falls back to white.
for (const file of ['src/ui/popups/camera/camera_popup.cpp', 'src/ui/popups/energy/energy_popup.cpp',
  'src/ui/popups/media/media_popup.cpp', 'src/ui/popups/sensor/sensor_popup.cpp', 'src/ui/popups/weather/weather_popup.cpp',
  'src/ui/popups/camera/camera_popup.h', 'src/ui/popups/pin/pin_popup.h', 'src/ui/popups/popup_shell.h', 'src/ui/ui_manager.h',
  'src/types/camera/renderer.cpp', 'src/types/media/renderer.cpp']) {
  assert.ok(!read(file).includes('0x2A2A2A'), `${file}: popup card fallback 0x2A2A2A`);
}
for (const file of ['src/ui/tabs/settings/settings_parts.cpp', 'src/ui/tabs/settings/settings_keyboard.cpp',
  'src/tiles/runtime/tile_icon_disc.h', 'src/tiles/runtime/tile_icon_source.cpp', 'src/tiles/runtime/tile_renderer.cpp',
  'src/ui/popups/climate/climate_popup.cpp']) {
  // (A switch's knob on its accent track stays white, like text on an accent.)
  const text = read(file).replace('lv_color_hex(on ? 0xFFFFFF : 0x8A8A8A)', '');
  assert.doesNotMatch(text, /[?:]\s*0xFFFFFF\b/, `${file}: white text fallback`);
}

// The stored setting: default dark, read at boot before the UI, kept by a
// full save and its own save.
const header = read('src/core/config/config_manager.h');
const config = read('src/core/config/config_manager.cpp');
assert.ok(header.includes('  uint8_t theme = 0;') && header.includes('  bool saveTheme(uint8_t theme);'));
assert.equal((config.match(/  config\.theme = 0;\n/g) || []).length, 2, 'defaults and reset');
assert.ok(config.includes('  config.theme = prefs.getUChar("theme", 0);\n  if (config.theme > 2) config.theme = 0;'));
assert.ok(config.includes('  prefs.putUChar("theme", normalized.theme);') && config.includes('         a.theme == b.theme &&'));
assert.match(config, /bool ConfigManager::saveTheme\(uint8_t theme\) \{\n  if \(theme > 2\) return false;[\s\S]*?prefs\.putUChar\("theme", theme\);/);
const ino = read('HomeTiles.ino');
const bootTheme = 'ui_theme::set(theme_schedule::at_boot(configManager.getConfig().theme));';
assert.ok(ino.indexOf(bootTheme) > ino.indexOf('bool has_config = configManager.load();') &&
  ino.indexOf(bootTheme) < ino.indexOf('tileConfig.load();'),
  'the theme is set after loading the config, before the tiles');

// The theme switches while running (user 2026-10-09: "warum muss neugestartet
// werden", like the global tile color): the Web Admin's field stores it and
// requests the change; the loop applies it outside camera and popup openings.
const handlers = read('src/web/server/handlers/web_admin_handlers.cpp');
const saveTheme = handlers.slice(handlers.indexOf('void WebAdminServer::handleSaveTheme() {'),
  handlers.indexOf('void WebAdminServer::handleSaveTileBorders() {'));
assert.ok(saveTheme.includes('if (value != "0" && value != "1" && value != "2") {') &&
  saveTheme.includes('if (!theme_schedule::store(stored)) {'),
  'The theme is validated, then stored');
// One place stores it for the panel's Settings, the Web Admin and Home
// Assistant's select (user 2026-10-23): saved, Automatic's last theme,
// requested, and Home Assistant hears it.
const store = read('src/ui/shared/theme_schedule.cpp');
const storeBody = store.slice(store.indexOf('bool store(uint8_t theme) {'));
assert.match(storeBody, /if \(theme > kAutomatic \|\| !configManager\.saveTheme\(theme\)\) return false;\s*if \(theme == kAutomatic\) configManager\.saveThemeAutoLast\(effective\(theme\)\);\s*uiManager\.requestThemeChange\(\);\s*mqttPublishDeviceSettings\(\);/);
assert.ok(read('src/web/server/web_admin.cpp').includes('server.on("/api/display/theme", HTTP_POST,'));
const html = read('src/web/server/render/web_admin_html.cpp');
assert.ok(html.includes('<div class=\\"icon-color-segmented choice-row global-theme\\" id=\\"" + theme_id +') &&
  html.includes('"\\" onclick=\\"saveTheme(\'" +') &&
  html.includes('if (display.theme == theme) global_panel += " class=\\"active\\"";') &&
  html.includes('appendHtmlEscaped(global_panel, theme == 2 ? tr.theme_auto : theme ? tr.theme_light : tr.theme_dark);') &&
  html.includes('appendHtmlEscaped(global_panel, tr.theme_label);'), 'Global settings: the theme field');
const admin = read('src/web/admin/settings/display-borders.js');
assert.ok(admin.includes("const response = await fetch('/api/display/theme', {") && admin.includes("body: 'theme=' + theme"));
// The parked Settings tile is drawn again in the new theme too (user
// 2026-10-10: it stayed white after switching to dark).
assert.ok(admin.includes("if (parked && typeof renderSettingsHiddenSlot === 'function') renderSettingsHiddenSlot(parked.dataset.hidden === '1');"),
  'theme switch redraws the parked Settings tile');
const i18n = read('src/core/i18n/i18n.cpp');
for (const words of [['"Design",', '"Dunkel",', '"Hell",'], ['"Theme",', '"Dark",', '"Light",'],
  ['"Thème",', '"Sombre",', '"Clair",'], ['"Motyw",', '"Ciemny",', '"Jasny",']]) {
  assert.ok(i18n.includes(words.map(word => '    ' + word).join('\n')), 'theme texts: ' + words[0]);
}
const ui = read('src/ui/ui_manager.cpp');
const apply = ui.slice(ui.indexOf('void UIManager::processThemeChange() {'), ui.indexOf('// Initialize the status bar.'));
for (const step of ['if (camera_popup_is_busy() || PopupFirstFrame::any_pending()) return;',
  'ui_theme::set(theme);', 'viewNavigationClosePopups();', 'popup_shell_delete_popups();', 'preloadPopups();',
  'lv_obj_set_style_bg_color(lv_screen_active(), screen, 0);', 'tiles_apply_screen_color();',
  'tiles_invalidate_folder(tileConfig.rootFolderId());', 'tiles_request_reload_all();',
  'image_screensaver_tiles_changed();', 'settings_screen::texts_changed();']) {
  assert.ok(apply.includes(step), 'processThemeChange: ' + step);
}
assert.ok(apply.indexOf('popup_shell_delete_popups();') < apply.indexOf('preloadPopups();'));
// The light screen ends round in black corners (user 2026-10-10), with the
// popup card's radius; hidden in the dark theme. Built right after the
// screen color at boot (the tiles refresh the panel while they are built),
// updated on a switch.
const build = ui.slice(ui.indexOf('void UIManager::buildUI('), ui.indexOf('tab_content_container = lv_obj_create(scr);'));
assert.ok(apply.includes('applyScreenCorners();') &&
  build.indexOf('lv_obj_set_style_bg_color(scr, lv_color_hex(ui_theme::screen()), 0);') < build.indexOf('applyScreenCorners();'));
const corners = ui.slice(ui.indexOf('void UIManager::applyScreenCorners() {'), ui.indexOf('void UIManager::processThemeChange() {'));
// The P4's quick Settings switch fills the framebuffer black and draws only
// the Settings controls again; the light theme lets LVGL draw the whole page
// instead (black bars, then square corners after a fill, user 2026-10-10).
const settingsSwitch = ui.slice(ui.indexOf('void UIManager::switchToTab(uint8_t index) {'));
assert.match(settingsSwitch, /const bool fill = !ui_theme::light\(\);\n    if \(!fill\) \{[\s\S]*?lv_obj_invalidate\(tab_panels\[index\]\);\n    \} else \{\n      BoardHAL::displayFillScreen\(0x0000\);/);
for (const marker of ['lv_obj_t* corner = lv_obj_create(lv_layer_sys());',
  'ui_surface_style::apply_radius(ring, popup_layout::kCardRadius + Device::kGridGap, 0);',
  'lv_obj_set_style_outline_color(ring, lv_color_black(), 0);',
  'if (ui_theme::light()) {\n      lv_obj_remove_flag(screen_corners[i], LV_OBJ_FLAG_HIDDEN);']) {
  assert.ok(corners.includes(marker), 'screen corners: ' + marker);
}
const inoLoop = read('HomeTiles.ino');
assert.equal(inoLoop.split('uiManager.processThemeChange();\n').length - 1, 2, 'Both loop branches apply a change');
// Every popup goes with its overlay: the shell keeps the overlays, deletes them
// and itself; camera and device popups free their state on delete.
const shell = read('src/ui/popups/popup_shell.cpp');
assert.ok(shell.includes('lv_obj_add_event_cb(parts.overlay, popup_overlay_deleted, LV_EVENT_DELETE, nullptr);'));
assert.match(shell, /void popup_shell_delete_popups\(\) \{\n  detach\(\);[\s\S]*?if \(overlay\) lv_obj_delete\(overlay\);[\s\S]*?if \(shell\.overlay\) lv_obj_delete\(shell\.overlay\);/);
const camera = read('src/ui/popups/camera/camera_popup.cpp');
assert.ok(camera.includes('lv_obj_add_event_cb(ctx->overlay, overlay_deleted_cb, LV_EVENT_DELETE, ctx);') &&
  camera.includes('if (ctx->full_touch) lv_obj_delete(ctx->full_touch);') &&
  camera.includes('if (g_camera_popup == ctx) g_camera_popup = nullptr;'));
const device = read('src/ui/popups/device/device_popup.cpp');
assert.ok(device.includes('lv_obj_add_event_cb(pop.overlay, on_overlay_delete, LV_EVENT_DELETE, nullptr);') &&
  /void on_overlay_delete\(lv_event_t\*\) \{[\s\S]*?lv_timer_delete\(g_live_timer\);[\s\S]*?pop = Popup\{\};/.test(device));
for (const [file, owner] of [['src/ui/popups/light/light_popup.cpp', 'g_light_popup_ctx'],
  ['src/ui/popups/climate/climate_popup.cpp', 'g_climate_popup'], ['src/ui/popups/cover/cover_popup.cpp', 'g_ctx'],
  ['src/ui/popups/energy/energy_popup.cpp', 'g_energy_popup_ctx'], ['src/ui/popups/media/media_popup.cpp', 'g_media_popup_ctx'],
  ['src/ui/popups/pin/pin_popup.cpp', 'g_ctx'], ['src/ui/popups/sensor/sensor_popup.cpp', 'g_sensor_popup_ctx'],
  ['src/ui/popups/weather/weather_popup.cpp', 'g_weather_popup_ctx']]) {
  const text = read(file);
  assert.ok(/LV_EVENT_DELETE, ctx\);/.test(text) && text.includes(owner + ' = nullptr;'), file + ' frees its context on delete');
}
// The color caches keep each theme apart.
const tone = read('src/ui/shared/tone_color.h');
assert.ok(tone.includes('entry.see_through == see_through && entry.light == light') &&
  tone.includes('const uint32_t key = icon | (light ? 0x1000000u : 0u);'));
assert.ok(read('src/tiles/runtime/tile_icon_source.cpp').includes('entry.percent == percent && entry.light == light'));

// The Web Admin preview takes both role tables and the light factors from
// ui_theme.h (one place for every light value) and switches with the field.
const styles = read('src/web/server/render/web_admin_styles.cpp');
// Both tables also as --pvd-* and --pvl-* for tiles on the other theme's
// surface (tile_surface, user 2026-10-10).
for (const marker of ['emit_roles(ui_theme::kDark, "pv");', 'emit_roles(ui_theme::kLight, "pv");',
  'emit_roles(ui_theme::kDark, "pvd");', 'emit_roles(ui_theme::kLight, "pvl");',
  'static_cast<double>(ui_theme::kLightStepNum) / ui_theme::kLightStepDen,',
  'static_cast<double>(ui_theme::kLightTintNum) / ui_theme::kLightTintDen,',
  'static_cast<double>(ui_theme::kLightSurfaceL), static_cast<double>(ui_theme::kLightCircleShare),',
  'static_cast<double>(ui_theme::kJewelNeutralL), static_cast<double>(ui_theme::kJewelIdleL));',
  '  appendPanelThemeVars(html);']) {
  assert.ok(styles.includes(marker), 'web_admin_styles: ' + marker);
}
assert.ok(html.includes('html += ui_theme::theme_light() ? "\\" data-panel-theme=\\"light" : "\\" data-panel-theme=\\"dark";'));
const previewJs = read('src/web/admin/tiles/grid-preview.js');
for (const marker of ["return -step * (Number.isFinite(scale) ? scale : 5 / 6);",
  "const lift = step => tinted ? toneRgb(base.L + toneStep(step), seed.C * 0.55, seed.h)",
  "return text || (seed.L > 0.95 && (!tinted || seed.C < 0.08)) ? (panelRole('icon') || icon) : icon;"]) {
  assert.ok(previewJs.includes(marker), 'grid-preview: ' + marker);
}
assert.ok(read('src/web/admin/tiles/icon-colors.js').includes("const scale = Number(panelVar('--pv-light-tint', '0.8'));"));
assert.ok(admin.includes("  if (theme !== '2') applyPanelThemePreview(theme);") &&
  admin.includes("if (shown !== shownPanelTheme()) applyPanelThemePreview(shown === '1' ? '1' : '0');"),
  'The preview switches with the field; Automatic shows the theme the panel picked');
const adminCss = read('src/web/assets/admin.css');
for (const marker of ['background:var(--pv-screen, #000000);', 'color:var(--pv-icon, #fff);']) {
  assert.ok(adminCss.includes(marker), 'admin.css: ' + marker);
}

// The screensaver is a photo frame in both themes (user 2026-10-10): its
// shadowed clock stays white, the preview's screensaver grid black.
const clockRenderer = read('src/types/clock/renderer.cpp');
assert.match(clockRenderer, /if \(!label\) return nullptr;  \/\/ The stack owns and cleans up line\.\n(?:  \/\/[^\n]*\n)+  set_label_style\(label, lv_color_white\(\), font\);/);
assert.ok(adminCss.includes('.tile-grid.screensaver-tile-grid { background:#000000; }'));

// The preview has no bezel (test-preview-frame.mjs): the screen ends where
// the panel's does; the layout window paints its screen in the screen color;
// the screenshot shows the screen corners (system layer).
const layoutWindow = read('src/web/admin/tiles/layout-window.js');
assert.ok(layoutWindow.includes("rx='${r}' fill='rgb(${screen})'/></svg>`;"));
assert.ok(read('src/web/server/handlers/web_admin_diagnostics.cpp')
  .includes('for (lv_obj_t* layer : {lv_display_get_layer_top(disp), lv_display_get_layer_sys(disp)}) {'));

const host = await lvglHost(root);
if (!host) {
  console.log('UI theme: roles and setting pass; SKIP: color math needs a host compiler');
  process.exit(0);
}
// The color math in both themes (one process per theme).
const out = path.join(root, 'build/tests/ui-theme');
fs.mkdirSync(out, {recursive: true});
const colors = [0xFFFFFF, 0x1A1A1A, 0xF44336, 0x4CAF50, 0x2196F3, 0xFFD54F, 0xFF9800, 0x9C27B0, 0xB0B0B0, 0x9E9E9E];
const cpp = `#include <cstdio>
#include <cstdlib>
#include "src/ui/shared/tone_color.h"
// The tint before the themes (tile_tint::background, dark only).
static uint32_t former_background(uint32_t base, uint32_t color, unsigned percent) {
  uint32_t out = tile_tint::mix(base & 0xFFFFFF, color & 0xFFFFFF, percent > 100 ? 100 : percent);
  for (int i = 0; i < 40 && tile_tint::white_contrast(out) < 4.5; ++i) out = tile_tint::scale(out, 95);
  return out;
}
int main(int argc, char** argv) {
  ui_theme::set(static_cast<uint8_t>(atoi(argv[1])));
  const unsigned colors[] = {${colors.map(c => `${c}u`).join(', ')}};
  for (unsigned c : colors) {
    std::printf("%u %u %u %u %u\\n", tone_color::readable_icon(c), tile_tint::background(0x1A1A1A, c, 20),
                former_background(0x1A1A1A, c, 20), tone_color::lifted(0x1A1A1A, c, true, 0.06f),
                tone_color::lifted(0xFFFFFF, c, false, 0.06f));
  }
  return 0;
}
`;
const source = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, cpp);
let result = spawnSync(host.cxx, ['-std=c++17', '-I', root, source, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const run = which => {
  const r = spawnSync(binary, [String(which)], {encoding: 'utf8'});
  assert.equal(r.status, 0, r.stderr);
  return r.stdout.trim().split(/\r?\n/).map(line => line.split(' ').map(Number));
};
const L = rgb => {
  const lin = v => { v /= 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; };
  const [r, g, b] = [(rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255].map(lin);
  const l = Math.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  const m = Math.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  const s = Math.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
};
const mix = (base, color, percent) => [16, 8, 0].reduce((out, shift) =>
  out | Math.floor((((base >> shift) & 255) * (100 - percent) + ((color >> shift) & 255) * percent + 50) / 100) << shift, 0);

// Dark: the tint is the former one, white stays white, circles step up.
run(0).forEach(([shown, tint, former, tintedUp], i) => {
  assert.equal(tint, former, `dark tint of #${colors[i].toString(16)} unchanged`);
  if (colors[i] === 0xFFFFFF) assert.equal(shown, 0xFFFFFF, 'dark: a white icon stays white');
  assert.ok(L(tintedUp) > L(0x1A1A1A), 'dark: the circle steps up from the card');
});
// Light: tints on the light card at 4/5 strength, icons without a color in
// the icon grey, colored icons in their own color (like Home Assistant),
// circles step down.
run(1).forEach(([shown, tint, , , neutralDown], i) => {
  const c = colors[i];
  const neutral = ((c >> 16) & 255) === ((c >> 8) & 255) && ((c >> 8) & 255) === (c & 255);
  assert.equal(tint, mix(0xFFFFFF, c, 16), `light tint of #${c.toString(16)}`);
  if (c === 0xFFFFFF || c === 0x1A1A1A) assert.equal(shown, 0x464646, 'light: white and text-colored icons take the icon grey');
  if (!neutral) assert.equal(shown, c, `light: #${c.toString(16)} keeps its own color`);
  assert.ok(Math.abs(L(neutralDown) - (1 - 0.05)) < 0.01, 'light: a circle steps down by 5/6 of the step');
});
console.log('UI theme: dark roles = former colors, dark tint unchanged, light mirrors the rules, setting read at boot');
