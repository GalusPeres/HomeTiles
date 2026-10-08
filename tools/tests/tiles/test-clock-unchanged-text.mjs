// The clock ticks every second but shows minutes. Rewriting the same text
// redrew the time, the date and their nine shadow copies each second, which
// cost 120 ms (S3) to 265 ms (V2) per second in the screensaver (b88 logs)
// and delayed touch reads. Unchanged text now leaves the labels alone.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const renderer = readRepoFile('src/types/clock/renderer.cpp');
assert.match(renderer,
  /bool set_text\(const char\* text\) \{\s*if \(!text\) text = "";\s*if \(main_label && strcmp\(lv_label_get_text\(main_label\), text\) == 0\) return false;/,
  'set_text returns early for unchanged text');
assert.match(renderer, /changed \|= data->time_shadows\.set_text\(buf\);/, 'the time line reports a change');
assert.match(renderer, /changed \|= data->date_shadows\.set_text\(buf\);/, 'the date line reports a change');
// The shadow pictures (b305) are drawn again only after a change as well.
assert.match(renderer, /if \(changed\) \{\s*apply_clock_line_alignment\(data\);[\s\S]*?refresh_shadow\(\);[\s\S]*?refresh_shadow\(\);\s*\}/,
  'widths, alignment and shadow pictures are refreshed only after a change');

console.log('Clock: unchanged minutes are not redrawn');
