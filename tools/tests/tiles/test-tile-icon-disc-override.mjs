// Per-tile icon disc override (global / on / off) for every type with an icon.
// It follows the Clock/Text per-tile border path: persisted in V7-compatible
// spare bits, sent as one form field, kept through drafts, copy/paste and
// import/export, and applied once per rendered tile.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const header = read('src/tiles/config/tile_config.h');
const config = read('src/tiles/config/tile_config.cpp');

// Model and persistence: defaults to global; the top bits of the V7 slideshow
// field, which only the animation tile uses, carry the mode.
assert.match(header, /uint8_t icon_disc_mode = 0;/);
assert.match(header, /TILE_ICON_DISC_GLOBAL = 0,\s*TILE_ICON_DISC_ON = 1,\s*TILE_ICON_DISC_OFF = 2/);
for (const marker of [
  'static constexpr uint16_t kIconDiscModeShift = 13;',
  'static constexpr uint16_t kSlideshowValueMask = 0x1FFFu;',
  'return type != TILE_PIXELANIM && type != TILE_EMPTY;',
  '(out.image_slideshow_sec & kSlideshowValueMask) | packIconDiscOptions(in));',
  'unpackIconDiscOptions(slideshow, out);',
  'if (tileStoresIconDiscOptions(out.type)) slideshow &= kSlideshowValueMask;',
]) assert.ok(config.includes(marker), `tile_config: ${marker}`);
assert.equal(3600 & 0xE000, 0, 'The largest slideshow value leaves the disc bits free');
// A round trip through the packed bits keeps every mode and the fps value.
for (const mode of [0, 1, 2]) {
  const packed = (10 & 0x1FFF) | (mode << 13);
  assert.equal((packed & (0x3 << 13)) >> 13, mode);
  assert.equal(packed & 0x1FFF, 10);
}

// HTTP: partial requests keep the stored mode; GET exports it.
const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(tiles, /if \(server\.hasArg\("icon_disc"\)\) \{\s*tile\.icon_disc_mode = normalizeTileIconDiscMode\(server\.arg\("icon_disc"\)\.toInt\(\)\);/);
assert.ok(tiles.includes('out += "\\",\\"icon_disc\\":";'));

// Device: one call for every type after rendering; Off stays transparent and
// Global follows the shared option style.
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.ok(renderer.includes('tile_icon_disc::apply_tile_options(tile_obj, tile.icon_disc_mode, tile.icon_glow);'));
const disc = read('src/tiles/runtime/tile_icon_disc.h');
for (const marker of [
  'enum class Mode : uint8_t { Global = 0, On = 1, Off = 2 };',
  'inline constexpr char kTags[6] = {};',
  'mode == Mode::Global);',
  'set_tag(child, disc_mode, glow);',
]) assert.ok(disc.includes(marker), `tile_icon_disc: ${marker}`);

// Web Admin editor: a localized checkbox like Tile borders in the common fields for icon types; Glow only for types whose icon can take a color.
const html = read('src/web/server/render/web_admin_html.cpp');
for (const marker of ['tr.icon_disc_label', '_tile_icon_glow_row',
  '_tile_icon_disc_fields', '_tile_icon_disc" checked> ', 'html += "\\" data-icon-disc=\\"";']) {
  assert.ok(html.includes(marker), `admin HTML: ${marker}`);
}
const snapshots = read('src/web/admin/tiles/snapshots.js');
assert.match(snapshots, /const out = collectIconDiscFields\(prefix, typeValue\);\s*if \(!meta\.save\) return out;/,
  'Draft snapshots, copy/paste and autosave carry the mode for every type, including Back/Settings');
assert.match(snapshots, /'background_opacity', 'icon_disc', 'icon_glow'\];/);
assert.match(snapshots, /return !\['0', '16'\]\.includes\(String\(typeValue \?\? '0'\)\);/);
for (const file of ['type-selection.js', 'drafts.js', 'clipboard.js']) {
  assert.ok(read(`src/web/admin/tiles/${file}`).includes('loadIconDiscFields(prefix, '), `${file} loads the mode`);
}
assert.ok(read('src/web/admin/tiles/autosave.js').includes('resetIconDiscFields(tab);'), 'Reset restores Global');
assert.ok(read('src/web/admin/tiles/editor.js').includes("_tile_icon_disc'), 'change', 'tileIconDisc'"));
assert.match(read('src/web/admin/tiles/import-export.js'), /fd\.append\('icon_disc', tile\.icon_disc\);/);
assert.ok(read('src/web/admin/tiles/live-preview.js').includes("tileElem.dataset.iconDisc = tileTypeHasDiscToggle(type)"));
// Glow is offered only where the icon can take a color (entity, color bar,
// state colors or a fixed color): sensor, scene, folder, switch, back, energy,
// climate, camera, cover, binary, number, select, date/time.
const snapshotsJs = read('src/web/admin/tiles/snapshots.js');
assert.ok(snapshotsJs.includes("return ['1', '2', '4', '5', '8', '12', '14', '15', '17', '18', '19', '20', '21', '22', '23']\n      .includes(String(typeValue ?? '0'));"));
assert.ok(snapshotsJs.includes("document.getElementById(tab + '_tile_icon_glow_row')"));
assert.ok(snapshotsJs.includes("return box?.checked === false ? '2' : '0';"));
// Like per-tile Tile borders, only Back, Clock and Text can hide their own disc.
assert.ok(snapshotsJs.includes("return ['8', '9', '10'].includes(String(typeValue ?? '0'));"));
assert.ok(snapshotsJs.includes("document.getElementById(tab + '_tile_icon_disc_row')"));
assert.ok(read('src/web/admin/tiles/grid-preview.js').includes("el.dataset.iconDisc = ['1', '2'].includes(String(tile?.icon_disc)) ? String(tile.icon_disc) : '0';"));
assert.match(read('src/web/assets/admin.css'), /\.icon-discs-off \.tile\.sensor-compact:not\(\[data-icon-disc="1"\]\) > \.tile-icon,\s*\.tile\.sensor-compact\[data-icon-disc="2"\] > \.tile-icon \{ background:transparent; \}/);

// Translations for every language.
const i18n = read('src/core/i18n/i18n.cpp');
for (const text of ['"Icon-Kreis"', '"Icon circle"', '"Cercle d\'icône"', '"Ein"', '"On"', '"Activé"', '"Aus"', '"Off"', '"Désactivé"']) {
  assert.ok(i18n.includes(text), `i18n ${text}`);
}
console.log('Per-tile icon disc override: persistence, HTTP, render, editor, drafts, import/export and preview pass');
