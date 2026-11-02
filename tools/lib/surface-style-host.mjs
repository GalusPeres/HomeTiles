import fs from 'node:fs';
import path from 'node:path';

const strip = source => source.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const read = (root, file) => fs.readFileSync(path.join(root, file), 'utf8');

// Existing layout harnesses supply display dimensions without a hardware SDK.
// Keep the production radius policy/styles, substituting only board constants.
export function radiusPolicyHost(root, cellHeight = '145', gap = '16') {
  return '#include <atomic>\n' + strip(read(root, 'src/core/config/tile_radius.h'))
    .replaceAll('Device::kGridCellH', cellHeight).replaceAll('Device::kGridGap', gap);
}

// The shared circle and control colors (tone_color.h), guarded so a harness
// can take it more than once. Its dependency-free config headers stay real
// includes (#pragma once), so a harness may include them itself too.
export function toneColorHost(root) {
  return '#include <math.h>\n#include "src/core/config/icon_glow.h"\n#include "src/core/config/tile_color.h"\n' +
    '#include "src/tiles/config/tile_tint.h"\n#ifndef HOMETILES_TONE_COLOR_HOST\n#define HOMETILES_TONE_COLOR_HOST\n' +
    strip(read(root, 'src/ui/shared/tone_color.h')) + '\n#endif\n';
}

// The frame line reads the design of its card's screen (tile_screen.h, a real
// include: #pragma once, harnesses may include it themselves too).
export function surfaceStyleHost(root) {
  return toneColorHost(root) + '#include "src/tiles/runtime/tile_screen.h"\n' +
    `void image_screensaver_config_changed() {}\n` +
    strip(read(root, 'src/ui/shared/ui_surface_style.h')) + '\n' +
    strip(read(root, 'src/ui/shared/ui_surface_style.cpp'));
}

// The open popup's surface (popup_surface.h): every popup entry opens its
// scope. Harnesses that compile popup code without tile_icon_source.cpp take
// a no-op recolor for popup_shell.cpp's popup_follow_surface.
// `surface: false` when the harness already includes popup_surface.h itself.
export function popupSurfaceHost(root, {recolor = true, surface = true} = {}) {
  return '#ifndef HOMETILES_POPUP_SURFACE_HOST\n#define HOMETILES_POPUP_SURFACE_HOST\n' +
    (surface ? strip(read(root, 'src/ui/popups/popup_surface.h')) : '') +
    (recolor ? '\nnamespace tile_icon_source { inline void recolor_surface(lv_obj_t*, int8_t, int8_t) {}\n' +
      'inline bool popup_tile_circle(uint32_t, uint32_t&, uint8_t&, uint32_t&) { return false; } }\n' : '\n') +
    '#endif\n';
}
