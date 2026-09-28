// Switching an Energy popup between 24H and 7D waited for a Bridge response
// (250-600 ms) because only 24H was requested on opening. Both opening paths
// now also request 7D in the background (throttled, not forced), and the
// popup applies responses only for the period it shows.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

// Raw source: the period names are string literals, which maskCpp hides.
const popup = readRepoFile('src/ui/popups/energy/energy_popup.cpp');
const prefetches = popup.match(
  /energy_request_period\("day", true\);(?:\s|\/\/[^\n]*)*energy_request_period\("week", false\);/g) || [];
assert.equal(prefetches.length, 2, 'both opening paths load 7D in the background');
assert.match(popup, /if \(!g_energy_popup_ctx->period\.equalsIgnoreCase\(period\)\) return;/,
  'a response for the other period only fills the cache');

// b88: a switch still took 235 ms (P4) to 410 ms (S3) on the UI task, and
// the fresh answer redrew the same bars for another 170-330 ms.
assert.doesNotMatch(maskCpp(popup), /lv_obj_update_layout\(/,
  'labels are measured from their font, without layout passes over the whole screen');
assert.match(popup, /g_energy_popup_ctx->shown_slots &&\s*energy_find_entry\([^;]*\) &&\s*same_chart_data\(g_energy_popup_ctx->shown_entry, entry\)\) \{/,
  'an answer that repeats the shown data keeps the chart');
assert.match(popup, /void clear_chart\(EnergyPopupContext\* ctx\) \{\s*if \([^)]*\) return;\s*ctx->shown_slots = 0;/,
  'a cleared chart is never taken for the shown data');

const data = maskCpp(readRepoFile('src/types/energy/energy_data.cpp'));
assert.match(data, /if \(!force && request\.last_attempt_ms != 0 &&\s*\(uint32_t\)\(now - request\.last_attempt_ms\) < kEnergyRequestThrottleMs\) \{\s*return true;/,
  'a background request is throttled per period');

console.log('Energy popup: 7D preloads in the background on opening');
