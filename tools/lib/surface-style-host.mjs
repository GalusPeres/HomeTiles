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

export function surfaceStyleHost(root) {
  return `void image_screensaver_config_changed() {}\n` +
    strip(read(root, 'src/ui/shared/ui_surface_style.h')) + '\n' +
    strip(read(root, 'src/ui/shared/ui_surface_style.cpp'));
}
