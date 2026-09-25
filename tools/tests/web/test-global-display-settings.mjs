// Global display settings next to Tile borders / Tile radius: icon discs on/off
// and the default tile color. Both follow the tile border setting end to end:
// NVS config, Web Admin row, live device apply and live preview.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const config = read('src/core/config/config_manager.cpp');
const header = read('src/core/config/config_manager.h');

// Config: defaults, equality, load, save and single-value saves with NVS keys.
assert.match(header, /bool icon_discs = true;/);
assert.match(header, /uint32_t default_tile_color = tile_color::kDefault;/);
assert.match(read('src/core/config/tile_color.h'), /constexpr uint32_t kDefault = 0x2A2A2A;/);
for (const marker of [
  'a.icon_discs == b.icon_discs &&',
  'a.default_tile_color == b.default_tile_color &&',
  'config.icon_discs = prefs.getBool("icon_disc", true);',
  'tile_color::normalize(prefs.getUInt("tile_color", tile_color::kDefault));',
  'prefs.putBool("icon_disc", normalized.icon_discs);',
  'prefs.putUInt("tile_color", normalized.default_tile_color);',
  'bool ConfigManager::saveIconDiscs(bool enabled) {',
  'bool ConfigManager::saveDefaultTileColor(uint32_t rgb) {',
]) assert.ok(config.includes(marker), `config: ${marker}`);
assert.equal((config.match(/config\.icon_discs = true;/g) || []).length, 2, 'Both reset paths default the discs on');

// Endpoints only set flags; the UI loop applies the change.
const handlers = read('src/web/server/handlers/web_admin_handlers.cpp');
const routes = read('src/web/server/web_admin.cpp');
assert.match(routes, /"\/api\/display\/icon-discs", HTTP_POST,\s*withStorageHold\(\[this\]\(\) \{ this->handleSaveIconDiscs\(\); \}\)/);
assert.match(routes, /"\/api\/display\/tile-color", HTTP_POST,\s*withStorageHold\(\[this\]\(\) \{ this->handleSaveDefaultTileColor\(\); \}\)/);
const discHandler = handlers.slice(handlers.indexOf('void WebAdminServer::handleSaveIconDiscs()'));
assert.match(discHandler.slice(0, discHandler.indexOf('\n}\n')), /configManager\.saveIconDiscs\(enabled\)[\s\S]*ui_surface_style::request_icon_disc_refresh\(\);/);
const colorHandler = handlers.slice(handlers.indexOf('void WebAdminServer::handleSaveDefaultTileColor()'));
const colorBody = colorHandler.slice(0, colorHandler.indexOf('\n}\n'));
for (const marker of ['configManager.saveDefaultTileColor(rgb)', 'tiles_invalidate_folder(', 'tiles_request_reload_all();', 'image_screensaver_tiles_changed();']) {
  assert.ok(colorBody.includes(marker), `tile color handler: ${marker}`);
}
assert.doesNotMatch(colorBody + discHandler.slice(0, discHandler.indexOf('\n}\n')), /lv_obj_|lv_style_/, 'Handlers must not touch LVGL');

// Device: discs use one shared opacity style that follows the option, so
// cached and hidden grids update without a rebuild.
const surface = read('src/ui/shared/ui_surface_style.cpp');
assert.match(surface, /entry\.follows_global && !configManager\.getConfig\(\)\.icon_discs/);
assert.match(surface, /g_icon_disc_refresh_pending\.exchange\(false\)[\s\S]*lv_obj_report_style_change\(&entry\.style\);/);
assert.ok(read('src/tiles/runtime/tile_icon_disc.h').includes('ui_surface_style::apply_icon_disc_opa(disc, kOpa, true);'));

// Device: every tile without its own color uses the global default color.
assert.match(read('src/tiles/config/tile_config.cpp'), /uint32_t tileDefaultBgColor\(\) \{\s*return tile_color::normalize\(configManager\.getConfig\(\)\.default_tile_color\);/);
for (const type of ['binary_sensor', 'camera', 'climate', 'clock', 'cover', 'energy', 'media', 'navigate', 'scene', 'sensor', 'switch', 'text', 'weather']) {
  const source = read(`src/types/${type}/renderer.cpp`);
  assert.match(source, /tileBgColorOrDefault\(\*?tile, tileDefaultBgColor\(\)\)/, `${type} uses the global default tile color`);
  assert.doesNotMatch(source, /tileBgColorOrDefault\([^)]*0x2A2A2A\)/, `${type} must not hard-code the default color`);
}
assert.doesNotMatch(read('src/tiles/runtime/tile_renderer.cpp'), /tileBgColorOrDefault\([^)]*0x[0-9A-F]{6}\)/);
// The animation tile keeps its black default.
assert.match(read('src/types/pixelanim/renderer.cpp'), /tileBgColorOrDefault\(tile, 0x000000\)/);
const registry = read('src/types/types_registry.cpp');
assert.match(registry, /return entry\.default_bg_color == tile_color::kDefault;/);
assert.match(registry, /if \(follows_default_tile_color\(entry\)\) html \+= "sharedBg:true,";/);

// Web Admin row: heading and labels come from the central translations.
const html = read('src/web/server/render/web_admin_html.cpp');
for (const marker of [
  'appendHtmlEscaped(html, tr.global_settings_heading);',
  'appendHtmlEscaped(html, tr.icon_discs);',
  'appendHtmlEscaped(html, tr.default_tile_color);',
  'onchange=\\"saveIconDiscs(this.checked)\\"',
  'oninput=\\"previewDefaultTileColor(this.value)\\"',
  'onchange=\\"saveDefaultTileColor(this.value)\\"',
  'html += "\\" class=\\"icon-discs-off";',
  'tileStyle = "background:var(--tile-default-bg)";',
]) assert.ok(html.includes(marker), `admin HTML: ${marker}`);
assert.ok(read('src/web/server/render/web_admin_styles.cpp').includes('html += "--tile-default-bg:";'));

// Translated path: every language has distinct, non-empty labels.
const i18nHeader = read('src/core/i18n/i18n.h');
const i18n = read('src/core/i18n/i18n.cpp');
const fields = [...i18nHeader.slice(i18nHeader.indexOf('struct Strings {')).matchAll(/const char\* (\w+);/g)].map(m => m[1]);
const tables = ['kStringsDe', 'kStringsEn', 'kStringsFr'].map(name => {
  const start = i18n.indexOf(`static const Strings ${name} = {`);
  const body = i18n.slice(start, i18n.indexOf('\n};', start));
  return [...body.matchAll(/^\s*"((?:[^"\\]|\\.)*)",?\s*$/gm)].map(m => m[1]);
});
const expected = {
  global_settings_heading: ['Globale Einstellungen', 'Global settings', 'Paramètres globaux'],
  icon_discs: ['Icon-Kreise', 'Icon circles', "Cercles d'icônes"],
  default_tile_color: ['Kachelfarbe', 'Tile color', 'Couleur des tuiles'],
};
for (const [field, values] of Object.entries(expected)) {
  const index = fields.indexOf(field);
  assert.ok(index >= 0, `Strings.${field}`);
  tables.forEach((table, language) => assert.equal(table[index], values[language], `${field} [${language}]`));
}

// Browser: the root class and the root color variable reach every grid.
const display = read('src/web/admin/settings/display-borders.js');
for (const marker of [
  "document.documentElement.classList.toggle('icon-discs-off', !enabled);",
  "fetch('/api/display/icon-discs'",
  "document.documentElement.style.setProperty('--tile-default-bg', color);",
  'if (meta && meta.sharedBg) meta.defaultBg = color;',
  "fetch('/api/display/tile-color'",
  'previewDefaultTileColor(defaultTileColorConfirmed || color);',
]) assert.ok(display.includes(marker), `display settings: ${marker}`);
assert.ok(read('src/web/admin/folders/navigation.js').includes('syncGlobalDisplayControls(tabEl);'));
const preview = read('src/web/admin/tiles/grid-preview.js');
assert.ok(preview.includes("const sharedCss = 'var(--tile-default-bg, #2A2A2A)';"));
assert.match(read('src/web/admin/tiles/live-preview.js'), /tileBackgroundCss\(meta, isDefaultBg,/);
assert.match(read('src/web/assets/admin.css'), /\.icon-discs-off \.tile\.sensor-compact:not\(\[data-icon-disc="1"\]\) > \.tile-icon/);
assert.match(read('src/web/assets/admin.css'), /\.folder-footer-options \{[^}]*flex-wrap:wrap;/, 'The global row wraps on narrow widths');

console.log('Global icon discs and default tile color: config, endpoints, live apply, translations and preview pass');
