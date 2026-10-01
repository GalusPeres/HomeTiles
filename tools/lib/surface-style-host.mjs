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

export function surfaceStyleHost(root) {
  return toneColorHost(root) + `void image_screensaver_config_changed() {}\n` +
    strip(read(root, 'src/ui/shared/ui_surface_style.h')) + '\n' +
    strip(read(root, 'src/ui/shared/ui_surface_style.cpp'));
}
