// A rule's own icon on a label (src/tiles/runtime/tile_rule_icon.h, user
// 2026-10-28) for the LVGL host harnesses that compile code using it: the
// header with its includes stripped. Put it after tile_icon_colors.h and the
// harness's getMdiCodepoint (or pass `codepoint: true` for a stub that knows
// no names).
import fs from 'node:fs';
import path from 'node:path';

export function ruleIconSource(root, {codepoint = false} = {}) {
  const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
  const strip = text => text.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
  return '#include <cstring>\n' +
    (codepoint ? 'inline uint32_t getMdiCodepoint(const String&) { return 0; }\n' : '') +
    strip(read('src/tiles/runtime/tile_rule_icon.h'));
}
