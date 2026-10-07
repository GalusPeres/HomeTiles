// ui_font_12 and ui_font_14 are compiled only for the 480x480 panels
// (src/fonts/ui_fonts.h). Every other use must sit inside a
// DEVICE_LAYOUT_480X480 branch, or the other profiles fail to compile (the
// Settings title fallback broke the V2 b260 build). The desktop emulator
// declares every font, so only this check catches it before a device build.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import {repoRoot} from '../../lib/admin-source.mjs';

const kGuard = /defined\s*\(\s*DEVICE_LAYOUT_480X480\s*\)/;
const kFont = /\bui_font_1[24]\b/;

function sourceFiles(dir) {
  const out = [];
  for (const entry of fs.readdirSync(dir, {withFileTypes: true})) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...sourceFiles(full));
    else if (/\.(c|cpp|h|inc)$/.test(entry.name)) out.push(full);
  }
  return out;
}

// True when the branch a line sits in asserts the 480x480 layout. `#else`
// counts as unguarded, `!defined(...)` too.
function unguardedUses(text) {
  const stack = [];
  const misses = [];
  text.split(/\r?\n/).forEach((line, index) => {
    const directive = line.match(/^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$/);
    if (directive) {
      const [, kind, rest] = directive;
      const positive = kGuard.test(rest) && !/!\s*defined\s*\(\s*DEVICE_LAYOUT_480X480/.test(rest);
      if (kind === 'if' || kind === 'ifdef' || kind === 'ifndef') stack.push(kind === 'if' && positive);
      else if (kind === 'elif') stack[stack.length - 1] = positive;
      else if (kind === 'else') stack[stack.length - 1] = false;
      else stack.pop();
      return;
    }
    if (kFont.test(line.replace(/\/\/.*$/, '')) && !stack.includes(true)) misses.push(`${index + 1}: ${line.trim()}`);
  });
  return misses;
}

const srcDir = path.join(repoRoot, 'src');
const fontsDir = path.join(srcDir, 'fonts');
const failures = [];
for (const file of sourceFiles(srcDir)) {
  if (file.startsWith(fontsDir)) continue;
  for (const miss of unguardedUses(fs.readFileSync(file, 'utf8'))) {
    failures.push(`${path.relative(repoRoot, file)}:${miss}`);
  }
}
assert.deepEqual(failures, [], `ui_font_12/14 outside a DEVICE_LAYOUT_480X480 branch:\n${failures.join('\n')}`);

console.log('Small fonts: ui_font_12/14 used only on 480x480 panels');
