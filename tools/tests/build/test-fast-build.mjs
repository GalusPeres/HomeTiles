// The fast local build must reproduce what arduino-cli feeds the compiler:
// sketch copies carry a #line directive to the original file, recipes split
// like Windows command lines, and HomeTiles.ino.cpp keeps its prototype block
// anchored to the functions when lines are added above them.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {depFiles, regenerateInoCpp, sketchCopyContent, splitCommand} from '../../fast-build.mjs';

const bs = '\\';
assert.deepEqual(
  splitCommand(`"C:${bs}a b${bs}gcc.exe" -c @C:${bs}f${bs}cpp_flags  -o "out.o"`),
  [`C:${bs}a b${bs}gcc.exe`, '-c', `@C:${bs}f${bs}cpp_flags`, '-o', 'out.o']);

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'hometiles-fast-build-'));
try {
  const source = path.join(dir, 'src', 'x.cpp');
  fs.mkdirSync(path.dirname(source), {recursive: true});
  fs.writeFileSync(source, 'int x;\r\n');
  const escaped = source.split(bs).join(bs + bs);
  assert.equal(sketchCopyContent(source).toString('utf8'), `#line 1 "${escaped}"\nint x;\r\n`,
    'sketch copies start with the arduino-cli #line directive');

  const dep = path.join(dir, 'x.cpp.d');
  fs.writeFileSync(dep, `C:${bs}b${bs}x.cpp.o: C:${bs}b${bs}x.cpp ${bs}\r\n C:${bs}r${bs}a.h\r\n`);
  assert.deepEqual(depFiles(dep), [`C:${bs}b${bs}x.cpp`, `C:${bs}r${bs}a.h`]);

  const ino = path.join(dir, 'HomeTiles.ino');
  const generated = path.join(dir, 'HomeTiles.ino.cpp');
  const ref = `C:${bs}${bs}p${bs}${bs}HomeTiles.ino`;
  fs.writeFileSync(generated, [
    '#include <Arduino.h>', `#line 1 "${ref}"`, '#include "a.h"', 'int value = 1;',
    `#line 6 "${ref}"`, 'void helper();', `#line 8 "${ref}"`, 'void setup();',
    `#line 3 "${ref}"`, 'void loop() {', '  helper();', '}', 'void helper() {}',
    '', 'void setup() {}', ''].join('\n'));
  // Two lines added above the first function move every anchor by two.
  fs.writeFileSync(ino, ['#include "a.h"', '#include "camera.h"', '// camera', 'int value = 1;',
    'void loop() {', '  helper();', '}', 'void helper() {}', '', 'void setup() {}'].join('\r\n'));
  const out = regenerateInoCpp(ino, generated).split('\n');
  assert.deepEqual(out.slice(0, 12), ['#include <Arduino.h>', `#line 1 "${ref}"`, '#include "a.h"',
    '#include "camera.h"', '// camera', 'int value = 1;', `#line 8 "${ref}"`, 'void helper();',
    `#line 10 "${ref}"`, 'void setup();', `#line 5 "${ref}"`, 'void loop() {']);

  // A renamed function cannot be re-anchored: fall back to arduino-cli.
  fs.writeFileSync(ino, fs.readFileSync(ino, 'utf8').replace('void helper() {}', 'void helper2() {}'));
  assert.equal(regenerateInoCpp(ino, generated), null);
} finally {
  fs.rmSync(dir, {recursive: true, force: true});
}
console.log('Fast build helpers: PASS');
