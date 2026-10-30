// One opacity for every screensaver tile (user 2026-10-02: "in die globale
// Screensaver verschieben"): set beside borders, radius and shadows, stored in
// the screensaver configuration, taken over once from the former per-tile
// values, used by the panel and by every preview of a screensaver tile.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = p => readRepoFile(p).replace(/\r\n/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};

// Configuration: stored, read, and taken over from the tiles once.
assert.match(read('src/ui/screensaver/screensaver_config.h'), /uint8_t tile_opacity = kScreensaverDefaultTileOpacity;/);
const config = read('src/ui/screensaver/screensaver_config.cpp');
assert.match(fn(config, 'ScreensaverConfigStore::loadPath'),
  /tile_opacity_stored_ = doc\["tile_opacity"\]\.is<int>\(\);\n\s*loaded\.tile_opacity = tile_opacity_stored_\n\s*\? static_cast<uint8_t>\(constrain\(doc\["tile_opacity"\]\.as<int>\(\), 0, 255\)\)\n\s*: data_\.tile_opacity;/);
assert.match(config, /doc\["tile_opacity"\] = data_\.tile_opacity;/);
const load = fn(config, 'ScreensaverConfigStore::load');
assert.match(load, /if \(!tile_opacity_stored_\) \{[\s\S]*?const uint16_t count = \+\+counts\[tile\.background_opacity\];[\s\S]*?data_\.tile_opacity = tile\.background_opacity;[\s\S]*?if \(best && config_ok\) config_needs_migration = true;/,
  'the most common former per-tile opacity becomes the global one and is saved');
assert.ok(load.indexOf('if (!tile_opacity_stored_)') > load.indexOf('tileConfig.loadScreensaverGrid(gridStorage())'),
  'the take-over reads the loaded tiles');

// Panel: every card takes the global value; a new value rebuilds the cards.
const screensaver = read('src/ui/screensaver/image_screensaver.cpp');
assert.match(fn(screensaver, 'build_slot_tile'), /const lv_opa_t opacity =\s*screensaver_tile_look::fill_opacity\(screensaverConfig\.get\(\)\.tile_opacity, tile\);/);
assert.doesNotMatch(screensaver, /tile\.background_opacity/);
assert.match(fn(screensaver, 'refresh_live_background_and_clock'),
  /if \(st->built_opacity != screensaverConfig\.get\(\)\.tile_opacity\) \{\n\s*rebuild_slot_grid\(st\);\n\s*refresh_slot_values\(st\);/);
assert.match(fn(screensaver, 'remember_shown_grid'), /st->built_opacity = screensaverConfig\.get\(\)\.tile_opacity;/);

// Web Admin: the slider sits in the screensaver footer, not in the tile settings.
const html = read('src/web/server/render/web_admin_html.cpp');
assert.match(html, /<span class=\\"screensaver-opacity-control\\"><input id=\\"screensaverTileOpacity\\" type=\\"range\\" "\s*"min=\\"0\\" max=\\"255\\" step=\\"1\\" value=\\""/);
assert.doesNotMatch(html, /screensaver_tile_opacity|has-opacity/);
assert.match(html, /String\(screensaver_tile_look::fill_opacity\(\s*screensaverConfig\.get\(\)\.tile_opacity, tile\) \* 100\.0f \/ 255\.0f, 2\)/);
for (const file of ['autosave', 'clipboard', 'drafts', 'editor', 'grid-preview', 'live-preview', 'snapshots', 'type-selection']) {
  assert.doesNotMatch(read(`src/web/admin/tiles/${file}.js`), /screensaver_tile_opacity/, file + ' still reads a per-tile opacity');
}
for (const file of ['grid-preview', 'live-preview']) {
  assert.match(read(`src/web/admin/tiles/${file}.js`), /const opacity = screensaverLookFilled\((?:look|tile)\) \? screensaverTileFillOpacity\((?:look|tile)\.ss_opacity\) : 0;/, file);
}
const editor = read('src/web/admin/screensaver/editor.js');
assert.match(editor, /tile_opacity: Math\.round\(ssClamp\(d\.tile_opacity \?\? SCREENSAVER_TILE_DEFAULT_OPACITY, 0, 255\)\),/);
assert.match(editor, /bind\('screensaverTileOpacity', 'input', el => \{\n\s*screensaverDraft\.tile_opacity = Number\(el\.value\);\n\s*refreshScreensaverTileOpacity\(\);\n\s*\}, false\);/);
assert.match(editor, /bind\('screensaverTileOpacity', 'change', el => \{ screensaverDraft\.tile_opacity = Number\(el\.value\); \}\);/);

// Its label in every language.
const i18n = read('src/core/i18n/i18n.cpp');
for (const label of ['"Füll-Deckkraft",', '"Fill opacity",', '"Opacité du fond",', '"Krycie wypełnienia",',
                     '"Neprůhlednost výplně",', '"Opacidad del relleno",', '"Nepriehľadnosť výplne",']) assert.ok(i18n.includes(label), label);
console.log('Screensaver tile opacity: one global value, taken over from the tiles, used by panel and previews');

// The screensaver's Global tab (user 2026-10-11: Tile | Slideshow | Global):
// borders, shadow, the opacity in one row and the design, in the tile page's
// Global style; nothing stays under the preview but its hint.
assert.ok(html.includes('html += "<div class=\\"panel-options\\"><section class=\\"global-settings-panel\\"><h3>";'));
assert.ok(html.includes('<div class=\\"global-settings-field screensaver-opacity-field\\"><label for=\\"screensaverTileOpacity\\">'));
assert.doesNotMatch(html, /screensaver-tile-options/);
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('.screensaver-opacity-field input[type="range"] { flex:1 1 auto;'));
assert.doesNotMatch(css, /screensaver-tile-options/);
