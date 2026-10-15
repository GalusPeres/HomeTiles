// The screensaver page (user 2026-10-11): its own design for its tiles
// (Dark, Light, Automatic = the panel's), on the panel and in the preview;
// and a click, a selection or a drag without a change saves nothing (every
// click into the preview used to show "Saved").
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// --- The design on the panel ----------------------------------------------------------------------
const header = read('src/ui/screensaver/screensaver_config.h');
assert.match(header, /uint8_t theme = 2;/);
const config = read('src/ui/screensaver/screensaver_config.cpp');
for (const marker of ['loaded.theme = static_cast<uint8_t>(constrain(doc["theme"] | 2, 0, 2));',
  'doc["theme"] = data_.theme;', 'if (data_.theme > 2) data_.theme = 2;']) assert.ok(config.includes(marker), marker);
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.match(renderer, /if \(grid_type == GridType::SCREENSAVER\) \{\s*const uint8_t theme = screensaverConfig\.get\(\)\.theme;\s*return theme == 0 \? tile_surface::kDark : theme == 1 \? tile_surface::kLight : tile_surface::kNone;/,
  'the screensaver tiles draw on the chosen surface');
const handler = read('src/web/server/handlers/web_admin_screensaver.cpp');
assert.ok(handler.includes('const uint8_t old_theme = before.theme;') &&
  handler.includes('if (after.theme != old_theme) image_screensaver_tiles_changed();'), 'an open screensaver draws its tiles again');
assert.ok(handler.indexOf('const uint8_t old_theme = before.theme;') < handler.indexOf('screensaverConfig.replaceFromJson('),
  'the old design is read before the new configuration replaces it');

// --- The page ---------------------------------------------------------------------------------------
const html = read('src/web/server/render/web_admin_html.cpp');
assert.ok(html.includes('html += "</label><div class=\\"icon-color-segmented choice-row screensaver-theme\\" id=\\"screensaverTheme\\" "'),
  'the design is a segmented choice like the panel design');
assert.ok(html.includes('if (saver.theme == theme) html += " class=\\"active\\"";'));
assert.ok(html.includes('html += " screensaver-tile-grid\\" data-ss-theme=\\"";'), 'the grid tells the preview the design');
assert.doesNotMatch(html, /screensaverBackgroundHeading/, 'the tab names the slideshow, no second heading');
assert.ok(!html.includes('class=\\"global-theme\\" id=\\"screensaverTheme\\"'),
  'the panel theme field never changes the screensaver design (saveTheme updates every .global-theme)');

// The preview draws the screensaver tiles on that surface, with that theme's card.
const grid = read('src/web/admin/tiles/grid-preview.js');
const context = vm.createContext({
  isScreensaverTileTab: tab => tab === 'screensaver',
  document: {getElementById: id => id === 'screensaverGrid' ? {dataset: {ssTheme: context.theme}} : null},
  theme: '0',
});
vm.runInContext(['tileSurfaceOf'].map(extractDeliveredFunction).join('\n'), context);
for (const [theme, surface] of [['0', 'dark'], ['1', 'light'], ['2', '']]) {
  context.theme = theme;
  assert.equal(context.tileSurfaceOf('screensaver', 'sensor', 0, '', '1'), surface, `design ${theme}`);
}
assert.ok(grid.includes("? (theme === 0 ? 'var(--tile-default-bg)' : 'var(--pvl-card)') : '';"),
  'a screensaver tile in the other design paints that theme card');

// --- Save only what changed ------------------------------------------------------------------------
const posts = [];
const notes = [];
const save = vm.createContext({
  JSON, Math, Number, String, Array,
  screensaverLoaded: true, screensaverDraft: null, screensaverWallpaperIndex: -1,
  screensaverTimeFontSizes: [20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 96],
  screensaverDateFontSizes: [20, 24, 28, 32, 40, 48, 56, 64, 72],
  SCREENSAVER_TILE_DEFAULT_OPACITY: 217,
  fetch: async (url, options) => { posts.push(JSON.parse(options.body)); return {ok: true, json: async () => ({success: true})}; },
  showNotification: text => notes.push(text), t: key => key,
});
vm.runInContext('let screensaverSavedKey = "";\n' + ['ssClamp', 'ssNearestClockFont', 'ssClockAlignment', 'ssClockFillColor', 'ssClockFillOpacity', 'ssNormalizeLoaded',
  'screensaverSaveKey', 'ssPayload', 'ssCurrentWallpaper', 'saveScreensaverNow'].map(extractDeliveredFunction).join('\n') +
  '\nglobalThis.setSavedKey = value => { screensaverSavedKey = value; };', save);
save.screensaverDraft = save.ssNormalizeLoaded({success: true, sd_ready: true, wallpapers: [], theme: 2, tile_opacity: 200});
save.setSavedKey(save.screensaverSaveKey(save.ssPayload(save.screensaverDraft, '')));
const settle = () => new Promise(resolve => setTimeout(resolve, 0));
save.saveScreensaverNow();
await settle(); await settle();
assert.equal(posts.length, 0, 'nothing changed: no save, no "Saved"');
assert.equal(notes.length, 0);
save.screensaverDraft.theme = 1;
save.saveScreensaverNow();
await settle(); await settle();
assert.equal(posts.length, 1, 'a changed design saves');
assert.equal(posts[0].theme, 1);
assert.deepEqual(notes, ['screensaverSaved']);
save.saveScreensaverNow();
await settle(); await settle();
assert.equal(posts.length, 1, 'saved once, then nothing again');

// The design's segmented choice: a click sets the draft, redraws the preview
// on that surface, marks the button and saves.
const editorSource = read('src/web/admin/screensaver/editor.js');
const click = editorSource.slice(editorSource.indexOf("document.getElementById('screensaverTheme')?.addEventListener('click'"));
assert.match(click, /screensaverDraft\.theme = \[0, 1, 2\]\.includes\(Number\(button\.dataset\.theme\)\)[\s\S]*?grid\.dataset\.ssTheme = String\(screensaverDraft\.theme\);\s*refreshScreensaverTiles\(\);\s*renderScreensaverEditor\(\);\s*scheduleScreensaverSave\(\);/);
assert.ok(editorSource.includes("button.classList.toggle('active', button.dataset.theme === String(d.theme));"),
  'the editor marks the saved design');

console.log('Screensaver design on panel and preview; saves only on a change');
