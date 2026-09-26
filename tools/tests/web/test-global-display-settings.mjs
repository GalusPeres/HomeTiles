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
assert.match(surface, /if \(follows_global && !configManager\.getConfig\(\)\.icon_discs\) return LV_OPA_TRANSP;/);
assert.match(surface, /g_icon_disc_refresh_pending\.exchange\(false\)[\s\S]*lv_obj_report_style_change\(&entry\.style\);/);
assert.ok(read('src/tiles/runtime/tile_icon_disc.h').includes('ui_surface_style::apply_icon_disc(disc, false, 3, false, true);'));

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
// The global block uses the Tile Settings style in two compact columns:
// checkboxes like Glow, small labels above the radius, glow and color fields,
// and the tile Color field with its reset button.
for (const marker of [
  'html += "<section class=\\"global-settings-panel\\"><h3>";',
  'html += "</h3><div class=\\"global-settings-grid\\"><label class=\\"inline-checkbox\\">"',
  '"<input class=\\"normal-tile-border-toggle\\" id=\\"" +',
  'html += "</label><label class=\\"inline-checkbox\\"><input class=\\"global-icon-disc-toggle\\" id=\\"" +',
  'html += "</label><div class=\\"global-settings-field\\"><label for=\\"" + radius_id + "\\">";',
  'html += "</div></div><div class=\\"global-settings-field\\"><label for=\\"" + glow_id + "\\">";',
  'appendHtmlEscaped(html, tr.icon_glow);',
  '"\\" oninput=\\"previewIconGlowLive(this.value)\\" onchange=\\"saveIconGlow(this.value)\\">"',
  '"onclick=\\"saveIconGlow(" + String(icon_glow::kDefault) + ")\\"><i class=\\"mdi mdi-restore\\"></i></button>"',
  'html += "<div class=\\"global-settings-field\\"><label for=\\"" + color_id + "\\">";',
  'html += "</label><div class=\\"tile-color-row\\"><input class=\\"global-tile-color\\" id=\\"" + color_id +',
  '"<button type=\\"button\\" class=\\"tile-color-reset-btn\\" title=\\"Reset\\" "',
  'static_cast<unsigned>(tile_color::kDefault));',
  '<label class="inline-checkbox"><input id="screensaverTileBorder" type="checkbox"> )html";',
]) assert.ok(html.includes(marker), `global panel HTML: ${marker}`);
const css = read('src/web/assets/admin.css');
assert.match(css, /\.global-settings-panel \{\s*flex:0 0 100%;\s*box-sizing:border-box;\s*background:var\(--panel\);\s*border:1px solid #232323;\s*border-radius:18px;\s*padding:20px;/,
  'Same card as the Tile Settings panel');
assert.ok(css.includes('.tile-settings label, .global-settings-panel label { font-size:12px; margin-bottom:4px; }'));
assert.ok(css.includes('.tile-settings h3, .global-settings-panel h3 { margin:0 0 14px; color:var(--text); font-size:17px; }'));
assert.match(css, /\.global-settings-grid \{\s*display:grid;\s*grid-template-columns:repeat\(2, minmax\(0, 1fr\)\);/,
  'Two compact columns');
assert.ok(css.includes('@media (max-width:520px) { .global-settings-grid { grid-template-columns:minmax(0, 1fr); } }'),
  'One column on phones');
assert.doesNotMatch(css, /global-settings-rows|global-settings-label|global-settings-control/, 'No stale row styles');

// Global Glow strength: one setting in percent (icon_glow.h), persisted in
// NVS, validated over HTTP, read by tile and popup discs (borders stay the
// neutral hairline), and previewed live through --icon-glow-pct.
const glow = read('src/core/config/icon_glow.h');
for (const marker of ['inline constexpr uint8_t kMinimum = 0;', 'inline constexpr uint8_t kMaximum = 100;',
  'return static_cast<uint8_t>((kNeutralOpa * clamp(percent) + kDefault / 2) / kDefault);',
  'inline constexpr uint8_t kStep = 5;', 'inline constexpr uint8_t kDefault = 25;',
  'return static_cast<uint8_t>((percent * 255 + 50) / 100);',
  'inline uint8_t disc_opa(int percent) { return to_opa(clamp(percent)); }'])
  assert.ok(glow.includes(marker), `icon_glow.h: ${marker}`);
assert.doesNotMatch(glow, /kBorderExtra|border_opa/, 'Borders take no glow');
const configCpp = read('src/core/config/config_manager.cpp');
for (const marker of ['config.icon_glow = icon_glow::clamp(prefs.getUChar("icon_glow", icon_glow::kDefault));',
  'prefs.putUChar("icon_glow", normalized.icon_glow);', 'a.icon_glow == b.icon_glow &&',
  'normalized.icon_glow = icon_glow::clamp(normalized.icon_glow);', 'bool ConfigManager::saveIconGlow(uint8_t percent) {'])
  assert.ok(configCpp.includes(marker), `config: ${marker}`);
assert.equal((configCpp.match(/config\.icon_glow = icon_glow::kDefault;/g) || []).length, 2, 'Both default paths reset the glow');
const glowHandlers = read('src/web/server/handlers/web_admin_handlers.cpp');
assert.match(glowHandlers, /void WebAdminServer::handleSaveIconGlow\(\) \{[\s\S]*?percent < icon_glow::kMinimum \|\| percent > icon_glow::kMaximum[\s\S]*?tiles_request_reload_all\(\);/);
assert.ok(read('src/web/server/web_admin.cpp').includes('server.on("/api/display/icon-glow", HTTP_POST,'));
const glowSurface = read('src/ui/shared/ui_surface_style.cpp');
assert.ok(glowSurface.includes('return icon_glow::disc_opa(configManager.getConfig().icon_glow);'));
assert.ok(!glowSurface.includes('icon_glow_border_opa'), 'No glow border opacity');
const toOpa = p => Math.floor((p * 255 + 50) / 100);
assert.deepEqual([toOpa(25), toOpa(45), toOpa(10), toOpa(80)], [64, 115, 26, 204], 'Default glow is minimally stronger (20 -> 25 %)');
const displayJs = read('src/web/admin/settings/display-borders.js');
for (const marker of ["document.documentElement.style.setProperty('--icon-glow-pct', String(percent));",
  "document.querySelectorAll('.tile').forEach(tile => applyIconDiscTint(tile));",
  "const response = await fetch('/api/display/icon-glow', {", "body: 'percent=' + percent",
  "tabEl.querySelectorAll('.global-icon-glow').forEach(input => { input.value = String(glow); });"])
  assert.ok(displayJs.includes(marker), `display JS: ${marker}`);
const glowTint = read('src/web/admin/tiles/grid-preview.js');
assert.ok(glowTint.includes("const glowOpa = Math.floor((glowPct * 255 + 50) / 100);") &&
  !glowTint.includes('glowBorderOpa'), 'Preview uses the device formula');
assert.ok(read('src/web/server/render/web_admin_styles.cpp').includes('html += "%;--icon-glow-pct:";'));

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

// Tile color is one choice (Global | Custom | From icon color) and replaces
// the tile color reset button: Global follows the global color (default
// marker), picking a color selects Custom. The behavior runs in
// test-tile-color-choice.mjs.
for (const marker of [
  '<div class="tile-color-row no-reset)html";',
  '_tile_color_modes">)html";',
  '{{"global", tr.tile_color_mode_global},',
  'html += R"html(" onclick="setTileColorMode(\')html";',
  'append_tile_color_from_icon_html(html, tab_id);',
]) assert.ok(html.includes(marker), `tile color HTML: ${marker}`);
assert.doesNotMatch(html, /onclick="resetTileColor\(|_tile_color_global|use_global_tile_color/, 'No reset button, no global checkbox');
assert.match(gridPreview, /function markTileColorInputExplicit\(tab\) \{[\s\S]*?input\.dataset\.bgColorDefault = '0';[\s\S]*?syncTileColorMode\(tab\);/);
assert.match(gridPreview, /function setTileColorMode\(tab, mode\) \{[\s\S]*?input\.dataset\.bgColorDefault = '1';[\s\S]*?scheduleAutoSave\(tab\);/);
assert.equal((gridPreview.match(/syncTileColorMode\(tab\);/g) || []).length, 5, 'Every color state change syncs the choice');
for (const text of ['"From icon color"', '"Aus Icon-Farbe"', '"Couleur de l\'icône"', '"Custom"', '"Eigene"', '"Personnalisée"'])
  assert.ok(i18n.includes(text), `translation ${text}`);
assert.doesNotMatch(i18n, /Use global tile color|Globale Kachelfarbe verwenden|Utiliser la couleur de tuile globale/);
// Tile Settings groups: Icon (icon, icon color, circle options), Tile (color,
// global color, layout) and Rules; clear names for what each field colors.
const editorHtml = read('src/web/server/render/web_admin_html.cpp');
assert.ok(editorHtml.includes('appendHtmlEscaped(html, tr.tile_group_icon);') && editorHtml.includes('appendHtmlEscaped(html, tr.tile_group_tile);'));
assert.ok(editorHtml.indexOf('append_tile_icon_color_fixed_html(html, tab_id);') < editorHtml.indexOf('_tile_icon_disc_fields">'),
  'The icon color sits with the icon');
assert.ok(i18n.includes('"Tile color",') && i18n.includes('"Kachelfarbe",') && i18n.includes('"Circle strength",'));
assert.ok(gridPreview.includes('const isDefaultBg = tileBgFollowsDefault(tile.bg_color);'));
assert.ok(gridPreview.includes("input.dataset.bgColorDefault === '1' || tileColorHexIsDefaultGrey(input.value)"));
assert.ok(read('src/web/admin/settings/access.js').includes(': tileBgFollowsDefault(bgValue);'));
assert.match(read('src/web/admin/tiles/live-preview.js'),
  /const isDefaultBg = tileColorInputIsDefault\(tab\);\s*if \(isDefaultBg\) \{[\s\S]*?colorInput\.dataset\.bgColorDefault = '1';/,
  'The live preview decides before replacing the input with the global color');

console.log('Global icon discs and default tile color: config, endpoints, live apply, translations and preview pass');
