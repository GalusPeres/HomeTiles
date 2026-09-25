// Per-tile glow (default on): a colored icon tints its disc with its own hue
// at the global Glow strength (default 25 %) instead of white at 38, but only
// on a neutral (grey) card; an own tile color or a rules tint keeps the
// neutral disc so disc and card never clash. The tint is computed centrally
// from the icon's current color and the card color, so every runtime change
// reaches the disc, and the Web Admin preview uses the same rule.
import assert from 'node:assert/strict';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const code = source => source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');

// Persistence: stored inverted in the V7 spare bit so existing tiles glow.
assert.match(read('src/tiles/config/tile_config.h'), /bool icon_glow = true;/);
const config = read('src/tiles/config/tile_config.cpp');
for (const marker of [
  'static constexpr uint16_t kIconGlowOffBit = 0x1u << 15;',
  '(tile.icon_glow ? 0u : kIconGlowOffBit));',
  'tile.icon_glow = (packed & kIconGlowOffBit) == 0;',
]) assert.ok(config.includes(marker), `tile_config: ${marker}`);
const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.ok(tiles.includes('if (server.hasArg("icon_glow")) tile.icon_glow = server.arg("icon_glow").toInt() != 0;'));
assert.ok(tiles.includes('out += tile.icon_glow ? "1" : "0";'));

// Central fill rule and the one icon color path.
const disc = code(read('src/tiles/runtime/tile_icon_disc.h'));
for (const marker of [
  'inline constexpr char kTags[6] = {};',
  'return r != g || g != b;',
  'inline constexpr uint8_t kNeutralSpread = 12;',
  'return hi - lo <= kNeutralSpread;',
  'const bool neutral_card = !card_color(disc, card) || card_is_neutral(card);',
  'const bool tinted = glow_of(disc) && icon_color_tints(rgb) && neutral_card;',
  ': scaled_opa(tinted ? ui_surface_style::icon_glow_opa() : kOpa, step);',
  'inline void set_icon_color(lv_obj_t* icon, lv_color_t color) {',
  'if (lv_obj_t* disc = disc_of(icon)) apply_fill(disc);',
  'set_tag(child, disc_mode, glow);',
]) assert.ok(disc.includes(marker), `tile_icon_disc: ${marker}`);
assert.ok(read('src/tiles/runtime/tile_renderer.cpp').includes(
  'tile_icon_disc::apply_tile_options(tile_obj, tile.icon_disc_mode, tile.icon_glow);'));
// Runtime icon color changes (light/switch, climate, binary sensor, cover)
// go through set_icon_color; nothing recolors a tile icon directly. The
// binary sensor state color reaches it through the per-tile icon color rules
// (tile_icon_color_rules::apply), which call set_icon_color.
assert.ok(code(read('src/tiles/runtime/tile_icon_color_rules.h')).includes('tile_icon_disc::set_icon_color(icon, color);'));
for (const [file, count] of [['src/tiles/runtime/tile_renderer.cpp', 2],
                             ['src/types/binary_sensor/renderer.cpp', 1],
                             ['src/types/cover/renderer.cpp', 1]]) {
  const source = code(read(file));
  assert.equal((source.match(/tile_icon_(?:disc::set_icon_color|color_rules::apply)\(/g) || []).length -
    (file.endsWith('tile_renderer.cpp') ? (source.match(/tile_icon_color_rules::apply\(/g) || []).length : 0),
    count, `${file} icon color path`);
  assert.doesNotMatch(source, /lv_obj_set_style_text_color\(\s*(?:widgets?\.)?icon_label/, `${file} must not bypass the disc tint`);
}

// Preview: same rule, same opacities from the firmware constants.
const styles = read('src/web/server/render/web_admin_styles.cpp');
assert.ok(styles.includes('html += String(tile_icon_disc::kOpa / 255.0f, 3);'));
assert.ok(styles.includes('html += String(icon_glow::disc_opa(glow) * 100.0f / 255.0f, 1);'));
const iconDiscTinted = new Function(`${extractDeliveredFunction('iconDiscTinted')}; return iconDiscTinted;`)();
const cppRule = rgb => { const r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255; return r !== g || g !== b; };
for (const rgb of [0xFFFFFF, 0xB0B0B0, 0x000000, 0xFFD54F, 0x3B82F6, 0xFF7043, 0xFFFFFE]) {
  const css = `rgb(${rgb >> 16 & 255}, ${rgb >> 8 & 255}, ${rgb & 255})`;
  assert.equal(iconDiscTinted(css), cppRule(rgb), `preview tint rule for ${css}`);
}
// Neutral card rule: preview == firmware for grey, near-grey and colored cards.
const iconDiscCardNeutral = new Function(`${extractDeliveredFunction('iconDiscCardNeutral')}; return iconDiscCardNeutral;`)();
const cppNeutral = rgb => { const c = [rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255]; return Math.max(...c) - Math.min(...c) <= 12; };
for (const rgb of [0x222222, 0x2A2A2A, 0x2A2B36, 0x2A2B37, 0x7B2E2E, 0x1E3A5F, 0x3D3D3D]) {
  const match = [null, String(rgb >> 16 & 255), String(rgb >> 8 & 255), String(rgb & 255)];
  assert.equal(iconDiscCardNeutral(match), cppNeutral(rgb), 'preview card rule for ' + rgb.toString(16));
}
assert.equal(iconDiscCardNeutral(null), true, 'Unknown card background counts as neutral');
assert.match(read('src/web/admin/tiles/grid-preview.js'),
  /icon\.classList\.toggle\('tile-icon-tinted',\s*glow && iconDiscTinted\(getComputedStyle\(icon\)\.color\) && iconDiscCardNeutral\(bg\)\);/);
const css = read('src/web/assets/admin.css');
assert.match(css, /\.tile\.sensor-compact > \.tile-icon\.tile-icon-tinted \{\s*background:color-mix\(in srgb, currentColor var\(--icon-disc-glow, 25%\), transparent\);/);
assert.ok(css.indexOf('.tile-icon.tile-icon-tinted') < css.indexOf('.icon-discs-off .tile.sensor-compact'),
  'Off rules win over the tint');
assert.match(read('src/web/admin/tiles/grid-preview.js'), /el\.innerHTML = html;\s*if \(typeof applyTileRulesTint === 'function'\) \{[\s\S]*?\}\s*applyIconDiscTint\(el\);/);
assert.match(read('src/web/admin/tiles/live-preview.js'), /tileElem\.innerHTML = html;\s*if \(typeof applyTileRulesTint === 'function'[\s\S]*?\}\s*applyIconDiscTint\(tileElem\);/);
assert.match(read('src/types/switch/admin.js'), /applySwitchPreviewColors\(tileElem, state\);\s*\/\/[^\n]*\s*applyIconDiscTint\(tileElem\);/);

// Editor, drafts, import/export and translations.
const html = read('src/web/server/render/web_admin_html.cpp');
assert.ok(html.includes('_tile_icon_glow" checked> )html";') && html.includes('appendHtmlEscaped(html, tr.icon_glow);'));
assert.ok(html.includes('html += "\\" data-icon-glow=\\"";'));
const snapshots = read('src/web/admin/tiles/snapshots.js');
// Glow is stored only for types whose icon can take a color; others keep on.
assert.ok(snapshots.includes("icon_glow: tileTypeHasColoredIcon(typeValue) && glow?.checked === false ? '0' : '1',"));
assert.ok(snapshots.includes("if (glow) glow.checked = true;"), 'Reset turns glow back on');
assert.ok(read('src/web/admin/tiles/editor.js').includes("_tile_icon_glow'), 'change', 'tileIconGlow'"));
assert.ok(read('src/web/admin/tiles/import-export.js').includes("fd.append('icon_glow',"));
const i18n = read('src/core/i18n/i18n.cpp');
for (const text of ['"Kreis leuchtet"', '"Circle glow"', '"Halo du cercle"']) assert.ok(i18n.includes(text), `i18n ${text}`);
console.log('Icon glow: persistence, central tint rule, runtime color path, preview rule and editor pass');
