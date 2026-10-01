// "Circle in icon color" keeps one family of colors (user 2026-10-01): on a
// Global or Custom tile, and in popups, the circle is exactly the circle the
// tile color "From icon" gives (computed for the card From icon would give),
// instead of the icon's hue mixed into the own card's lightness. White, grey
// and black icons keep their neutral circle.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const source = read('src/tiles/runtime/tile_icon_source.cpp');
const fromIconCard = cppFunctionDefinitions(source).find(f => f.name === 'from_icon_card');
assert.ok(fromIconCard, 'tile_icon_source registers from_icon_card');
const brighten = cppFunctionDefinitions(read('src/tiles/runtime/tile_renderer_shared.h'))
  .find(f => f.name === 'brighten_rgb_color');
assert.ok(brighten);

const harness = String.raw`
#include "src/ui/shared/tone_color.h"
#include "src/tiles/config/tile_tint.h"
#include "src/tiles/config/tile_icon_colors.h"

#include <cstdio>
#include <initializer_list>

static uint32_t tileDefaultBgColor() { return 0x1A1A1A; }
${brighten.source}
${fromIconCard.source}

int main() {
  const uint32_t custom = 0x2E2A3A;
  for (const uint32_t icon : {0x9AFF7Au, 0xFFD54Fu, 0x00C8FFu, 0xFFB27Au}) {
    const uint32_t family = from_icon_card(icon, false);
    const uint32_t from_icon = tone_color::fill(tile_tint::background(0x1A1A1A, icon, 20), icon, true, 25).disc_color;
    const uint32_t circle = tone_color::fill(family, icon, true, 25).disc_color;
    const uint32_t old_global = tone_color::fill(0x1A1A1A, icon, true, 25).disc_color;
    const uint32_t old_custom = tone_color::fill(custom, icon, true, 25).disc_color;
    std::printf("color %06X %06X %06X %06X %06X %06X %06X\n", static_cast<unsigned>(icon),
                static_cast<unsigned>(circle), static_cast<unsigned>(from_icon),
                static_cast<unsigned>(old_global), static_cast<unsigned>(old_custom),
                static_cast<unsigned>(from_icon_card(icon, true)), static_cast<unsigned>(family));
  }
  return 0;
}
`;

const output = compileAndRun({label: 'Circle family', harness});
if (output !== null) {
  for (const line of output.trim().split('\n')) {
    const [, icon, circle, fromIcon, oldGlobal, oldCustom, pressed, family] = line.trim().split(' ');
    assert.equal(circle, fromIcon, `${icon}: Global/Custom circle = From icon circle`);
    assert.notEqual(circle, oldGlobal, `${icon}: no longer mixed into the grey card`);
    assert.notEqual(circle, oldCustom, `${icon}: no longer mixed into the custom card`);
    // Pressed: 0x10 lighter per channel, like a tinted card.
    const lighter = [16, 8, 0].map(s => Math.min(255, ((parseInt(family, 16) >> s) & 255) + 16));
    assert.equal(parseInt(pressed, 16), (lighter[0] << 16) | (lighter[1] << 8) | lighter[2], `${icon}: pressed`);
  }
}

// One rule everywhere: tile circles, popup headers and the Web preview.
const disc = read('src/tiles/runtime/tile_icon_disc.h');
assert.ok(disc.includes('if (!tinted || see_through_card || !tone_color::g_from_icon_card || card_follows_icon(host)) return card;'));
assert.ok(disc.includes('return tone_color::fill(circle_card(host, card, rgb, tinted, pressed, see_through_card), rgb, tinted,'));
assert.ok(source.includes('const bool g_from_icon_card_registered = (tone_color::g_from_icon_card = &from_icon_card, true);'));
const shell = read('src/ui/popups/popup_shell.cpp');
assert.ok(shell.includes('tinted && !options.from_icon && tone_color::g_from_icon_card ? tone_color::g_from_icon_card(rgb, false) : card;'));
const preview = read('src/web/admin/tiles/grid-preview.js');
assert.ok(preview.includes('if (tinted && !(fill > 0) && !seeThrough && typeof tileTintBackground === \'function\') {') &&
          preview.includes('const familyHex = tileTintBackground(base || \'#1A1A1A\', givenHex, ICON_FILL_DEFAULT);'));
assert.match(read('src/tiles/config/tile_icon_colors.h'), /inline constexpr uint8_t kTintDefault = 20;/);
assert.match(preview, /const ICON_FILL_DEFAULT = 20;/);
console.log('Circle in icon color keeps the From icon family');
