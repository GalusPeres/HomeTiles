// Global display settings next to Tile borders / Tile radius: icon discs on/off
// and the default tile color. Both follow the tile border setting end to end:
// NVS config, Web Admin row, live device apply and live preview.
import assert from 'node:assert/strict';
import {extractFunction, readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const config = read('src/core/config/config_manager.cpp');
const header = read('src/core/config/config_manager.h');

// Config: defaults, equality, load, save and single-value saves with NVS keys.
assert.match(header, /bool icon_discs = true;/);
assert.match(header, /uint32_t default_tile_color = tile_color::kDefault;/);
const tileColor = read('src/core/config/tile_color.h');
assert.match(tileColor, /constexpr uint32_t kDefault = 0x222222;/, 'The built-in default is slightly darker');
assert.match(tileColor, /constexpr uint32_t kLegacyDefault = 0x2A2A2A;/);
assert.match(tileColor, /return normalize\(rgb\) == kDefault \|\| normalize\(rgb\) == kLegacyDefault;/);
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
assert.match(registry, /return tile_color::isDefaultGrey\(entry\.default_bg_color\);/);
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
// The global block uses the Tile Settings card: same heading, checkbox rows,
// labeled radius field and the tile Color field with its reset button.
for (const marker of [
  'html += "<section class=\\"global-settings-panel\\"><h3>";',
  '"<label class=\\"inline-checkbox\\"><input class=\\"normal-tile-border-toggle\\" "',
  'html += "</label></div><div class=\\"global-settings-field\\"><label for=\\"" + radius_id + "\\">";',
  'html += "</span></div><div class=\\"tile-color-row\\"><input class=\\"global-tile-color\\" type=\\"color\\" value=\\"";',
  '"<button type=\\"button\\" class=\\"tile-color-reset-btn\\" title=\\"Reset\\" "',
  'static_cast<unsigned>(tile_color::kDefault));',
  '<label class="inline-checkbox"><input id="screensaverTileBorder" type="checkbox"> )html";',
]) assert.ok(html.includes(marker), `global panel HTML: ${marker}`);
const css = read('src/web/assets/admin.css');
assert.match(css, /\.global-settings-panel \{\s*flex:0 0 100%;\s*box-sizing:border-box;\s*background:var\(--panel\);\s*border:1px solid #232323;\s*border-radius:18px;\s*padding:20px;/,
  'Same card as the Tile Settings panel');
assert.ok(css.includes('.tile-settings label, .global-settings-panel label { font-size:12px; margin-bottom:4px; }'));
assert.ok(css.includes('.tile-settings h3, .global-settings-panel h3 { margin:0 0 14px; color:var(--text); font-size:17px; }'));
assert.match(css, /\.global-settings-fields \{\s*display:grid;\s*grid-template-columns:repeat\(auto-fit, minmax\(180px, 1fr\)\);/,
  'The global fields wrap on narrow widths');

// A stored built-in default grey (older editors saved it explicitly) follows
// the global default tile color like an unset color, on the device and in
// both previews; every other stored color is kept.
const tileHeader = read('src/tiles/config/tile_config.h');
assert.match(tileHeader, /return stored == 0 \|\| tile_color::isDefaultGrey\(stored\);/);
assert.match(tileHeader, /return tileBgColorFollowsDefault\(tile\.bg_color\) \? tileDefaultBgColor\(\) : tileBgColorRgb\(tile\);/);
assert.ok(read('src/ui/ui_manager.cpp').includes('? (tileBgColorFollowsDefault(snapshot_color)'), 'Hidden Settings gesture color');
assert.ok(html.includes('tileBgColorFollowsDefault(tile.bg_color) && tile_type_follows_default_tile_color(tile.type)'));
assert.ok(html.includes('snapshot.valid && !tileBgColorFollowsDefault(snapshot.bg_color)'));
const gridPreview = read('src/web/admin/tiles/grid-preview.js');
const follows = new Function(`${extractFunction('isDefaultTileGrey', gridPreview)}
${extractFunction('tileBgFollowsDefault', gridPreview)}; return tileBgFollowsDefault;`)();
for (const [value, expected] of [[0, true], [undefined, true], [0x2A2A2A, true], [0x012A2A2A, true],
                                 [0x01222222, true], [0x01353535, false], [0x01000000, false], [0x01FF0000, false]]) {
  assert.equal(follows(value), expected, `preview follows the global color for ${value}`);
}
const firmwareFollows = stored => stored === 0 || [0x222222, 0x2A2A2A].includes(stored & 0xFFFFFF);
for (const value of [0, 0x012A2A2A, 0x01222222, 0x01353535, 0x01000000]) assert.equal(follows(value), firmwareFollows(value));

// "Use global color" replaces the tile color reset button: checked follows the
// global color (default marker), picking a color unchecks it.
for (const marker of [
  '<div class="tile-color-row no-reset)html";',
  '_tile_color_global" checked onchange="toggleTileGlobalColor(\')html";',
  'appendHtmlEscaped(html, tr.use_global_tile_color);',
]) assert.ok(html.includes(marker), `tile color HTML: ${marker}`);
assert.doesNotMatch(html, /onclick="resetTileColor\(/, 'The tile Color field has no reset button');
assert.match(gridPreview, /function markTileColorInputExplicit\(tab\) \{[\s\S]*?input\.dataset\.bgColorDefault = '0';\s*syncTileColorGlobalToggle\(tab\);/);
assert.match(gridPreview, /function toggleTileGlobalColor\(tab, useGlobal\) \{[\s\S]*?input\.dataset\.bgColorDefault = useGlobal \? '1' : '0';[\s\S]*?scheduleAutoSave\(tab\);/);
assert.equal((gridPreview.match(/syncTileColorGlobalToggle\(tab\);/g) || []).length, 5, 'Every color state change syncs the checkbox');
assert.ok(i18n.includes('"Use global color"') && i18n.includes('"Globale Farbe verwenden"') && i18n.includes('"Utiliser la couleur globale"'));
assert.ok(gridPreview.includes('const isDefaultBg = tileBgFollowsDefault(tile.bg_color);'));
assert.ok(gridPreview.includes("input.dataset.bgColorDefault === '1' || tileColorHexIsDefaultGrey(input.value)"));
assert.ok(read('src/web/admin/settings/access.js').includes(': tileBgFollowsDefault(bgValue);'));
assert.match(read('src/web/admin/tiles/live-preview.js'),
  /const isDefaultBg = tileColorInputIsDefault\(tab\);\s*if \(isDefaultBg\) \{[\s\S]*?colorInput\.dataset\.bgColorDefault = '1';/,
  'The live preview decides before replacing the input with the global color');

console.log('Global icon discs and default tile color: config, endpoints, live apply, translations and preview pass');
