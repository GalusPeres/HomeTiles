// The shared unit helper (src/ui/shared/value_unit*: the unit a step smaller
// after its number) for the LVGL host harnesses that compile code using it:
// the sources with their includes stripped. Put it after the harness's font
// declarations (the helper picks the unit's font from the panel fonts).
import fs from 'node:fs';
import path from 'node:path';

export function valueUnitSource(root) {
  const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
  const strip = text => text.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
  return '#include <cstring>\n#include <string>\n' +
    ['src/ui/shared/value_unit_rules.h', 'src/ui/shared/value_unit.h', 'src/ui/shared/value_unit.cpp']
      .map(file => strip(read(file))).join('\n');
}
