// The Guition S3 has about 44 KB of internal heap. Opening a Select popup
// left its state history (segments, activity rows, palette, the pending
// response) and the Select options in small internal blocks that stayed with
// the resident popup: free internal RAM fell from 46 to 33 KB with a largest
// block of 7-11 KB, and the folder cache stopped growing. These long-lived
// containers now keep their buffers in PSRAM, as the Bridge entity index does.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const allocator = read('src/core/memory/psram_allocator.h');
assert.match(allocator, /heap_caps_malloc\(n \* sizeof\(T\), MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT\)/);
assert.match(allocator, /using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;/);
assert.match(allocator, /template <typename T>\nusing PsVector = std::vector<T, PsramAllocator<T>>;/);

// One allocator definition, shared with the Bridge entity index.
const bridge = read('src/network/bridge/ha_bridge_config.h');
assert.ok(bridge.includes('#include "src/core/memory/psram_allocator.h"'));
assert.doesNotMatch(bridge, /struct PsramAllocator/);

const popup = read('src/ui/popups/sensor/sensor_popup.cpp');
const context = popup.match(/struct SensorPopupContext \{[\s\S]*?\n};/)[0];
for (const field of [
  'PsVector<BinarySegment> binary_segments;',
  'PsVector<uint8_t> binary_timeline_bins;',
  'PsVector<String> state_history_palette;',
  'PsVector<BinaryActivityEntry> binary_activity;',
]) assert.ok(context.includes(field), field);
assert.doesNotMatch(context, /std::vector</, 'no popup history container uses the internal heap');
assert.match(popup, /struct PendingHistoryUpdate \{[\s\S]*?PsString payload;[\s\S]*?\n\};/);
assert.match(popup, /g_pending_history\.payload\.assign\(payload_text\.c_str\(\), payload_text\.length\(\)\);/);
assert.match(popup, /const PsVector<String>& palette,\n\s*PsVector<uint8_t>& output\)/);

const value = read('src/types/value/value_control.h');
assert.match(value, /struct EditableValue \{[\s\S]*?PsVector<String> options;[\s\S]*?\n\};/);

console.log('Popup history and Select options keep their buffers in PSRAM.');
