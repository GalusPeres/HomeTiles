// Switching an Energy popup between 24H and 7D waited for a Bridge response
// (250-600 ms) because only 24H was requested on opening. Both opening paths
// now also request 7D in the background (throttled, not forced). Each period
// has its own chart; the hidden one is filled from the cache, so a switch
// only swaps two finished charts (b90 still redrew about 100 objects).
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

// Raw source: the period names are string literals, which maskCpp hides.
const popup = readRepoFile('src/ui/popups/energy/energy_popup.cpp');
const prefetches = popup.match(
  /energy_request_period\("day", true\);(?:\s|\/\/[^\n]*)*energy_request_period\("week", false\);/g) || [];
assert.equal(prefetches.length, 2, 'both opening paths load 7D in the background');
assert.match(popup, /struct EnergyPopupContext : EnergyChartView \{\s*EnergyChartView spare;/,
  'the context shows one chart and keeps the other period hidden');
assert.match(popup, /static void queue_spare_after_frame\(EnergyPopupContext\* ctx\) \{\s*queue_energy_popup_refresh\(ctx->spare\.week \? "week" : "day"\);\s*ctx->spare_frame\.begin\(\);/,
  'opening fills the hidden chart from the cache after the content frame');
assert.equal((popup.match(/queue_spare_after_frame\(g_energy_popup_ctx\);/g) || []).length, 2,
  'both opening paths prepare the hidden chart');
assert.match(popup, /if \(!shown_pending\) \{[\s\S]*?refresh_spare_from_cache\(g_energy_popup_ctx\);/,
  'an answer for the other period fills the hidden chart');
const click = /void on_period_click\([\s\S]*?\n\}/.exec(popup)[0];
assert.match(click, /if \(ctx->chart_wrap\) lv_obj_invalidate\(ctx->chart_wrap\);[\s\S]*show_period_view\(ctx\);[\s\S]*refresh_from_cache\(ctx\);/,
  'a switch shows the prepared chart and redraws only the chart area');
assert.doesNotMatch(maskCpp(click), /clear_chart\(/, 'a switch does not clear the prepared chart');
assert.match(popup, /if \(ctx->shown_slots && same_chart_data\(ctx->shown_entry, entry\)\) \{\s*update_header_value\(ctx, entry\);\s*refresh_energy_readout\(ctx\);\s*return;/,
  'a chart that already shows the cached entry is not rebuilt');

// b88: a switch still took 235 ms (P4) to 410 ms (S3) on the UI task, and
// the fresh answer redrew the same bars for another 170-330 ms.
assert.doesNotMatch(maskCpp(popup), /lv_obj_update_layout\(/,
  'labels are measured from their font, without layout passes over the whole screen');
assert.match(popup, /g_energy_popup_ctx->shown_slots &&\s*energy_find_entry\([^;]*\) &&\s*same_chart_data\(g_energy_popup_ctx->shown_entry, entry\)\) \{/,
  'an answer that repeats the shown data keeps the chart');
assert.match(popup, /void clear_chart\(EnergyChartView\* ctx\) \{\s*if \([^)]*\) return;\s*ctx->shown_slots = 0;/,
  'a cleared chart is never taken for the shown data');
// More than 32 dirty areas make LVGL redraw the whole screen (244 ms, V2 b90).
for (const name of ['clear_chart', 'apply_entry_to_chart']) {
  const body = new RegExp(`void ${name}\\([\\s\\S]*?\\n\\}`).exec(popup)[0];
  assert.match(body, /if \(ctx->chart_wrap\) lv_obj_invalidate\(ctx->chart_wrap\);/,
    `${name} marks the whole chart dirty once`);
}

const data = maskCpp(readRepoFile('src/types/energy/energy_data.cpp'));
assert.match(data, /if \(!force && request\.last_attempt_ms != 0 &&\s*\(uint32_t\)\(now - request\.last_attempt_ms\) < kEnergyRequestThrottleMs\) \{\s*return true;/,
  'a background request is throttled per period');

console.log('Energy popup: 7D preloads in the background on opening');
