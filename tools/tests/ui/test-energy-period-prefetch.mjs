// The Energy popup has fixed periods Year, Month, Week, Today (user
// 2026-10-29). Each period has its own chart, built with the popup and filled
// from the cache, so a switch only swaps finished charts (b90 still redrew
// about 100 objects). The other periods load in the background after
// opening, one request after the other's answer: the Bridge runs one energy
// request per panel and queues two (request_limits.py), four at once lost
// one. A Week request names the panel's first weekday.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

// Raw source: the period names are string literals, which maskCpp hides.
const popup = readRepoFile('src/ui/popups/energy/energy_popup.cpp').replace(/\r\n?/g, '\n');
const fn = name => {
  const found = cppFunctionDefinitions(popup).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};
assert.match(popup, /struct EnergyPopupContext : EnergyChartView \{\s*EnergyChartView hidden\[kPeriodCount - 1\];/,
  'the context shows one chart and keeps the other periods hidden');
assert.match(fn('build_popup_ui'), /for \(uint8_t p = 1; p < kPeriodCount; \+\+p\) \{\s*build_chart_view\(ctx, body_box, static_cast<EnergyPeriod>\(p\)\);/,
  'every period gets its chart with the popup');
assert.match(fn('queue_hidden_after_frame'), /for \(const EnergyChartView& view : ctx->hidden\) queue_energy_popup_refresh\(period_name\(view\.period\)\);\s*ctx->spare_frame\.begin\(\);/,
  'opening fills the hidden charts from the cache after the content frame');
assert.equal((popup.match(/queue_hidden_after_frame\(g_energy_popup_ctx\);/g) || []).length, 2,
  'both opening paths prepare the hidden charts');
assert.equal((popup.match(/energy_request_period\("day", true\);/g) || []).length, 2, 'both opening paths ask for today');

// Background loading: one at a time, once per opening.
const prefetch = fn('prefetch_next_period');
assert.match(prefetch, /if \(!popup_visible\(ctx\) \|\| energy_request_waiting\(\)\) return;/,
  'the next period waits for any answer still on its way');
assert.match(prefetch, /if \(\(ctx->prefetched & bit\) \|\| period == ctx->shown_period\) continue;\s*ctx->prefetched \|= bit;\s*if \(energy_period_fresh\(period_name\(period\)\)\) continue;\s*energy_request_period\(period_name\(period\), false\);\s*return;/,
  'each period once per opening, none newer than a minute');
const queue = fn('process_energy_popup_queue');
assert.ok(queue.indexOf('prefetch_next_period(g_energy_popup_ctx);') > queue.indexOf('if (PopupFirstFrame::any_pending()) return;'),
  'background loading starts after the content frame');
assert.match(fn('apply_init_to_context'), /ctx->prefetched = 0;/, 'every opening loads them again');
assert.match(queue, /if \(!shown_pending\) \{[\s\S]*?refresh_hidden_from_cache\(g_energy_popup_ctx, static_cast<EnergyPeriod>\(p\)\);\s*return;/,
  'an answer for another period fills its hidden chart, one per pass');

const click = fn('on_period_click');
assert.match(click, /if \(ctx->chart_wrap\) lv_obj_invalidate\(ctx->chart_wrap\);[\s\S]*show_period_view\(ctx\);[\s\S]*refresh_from_cache\(ctx\);\s*energy_request_period\(period_name\(next\), true\);/,
  'a switch shows the prepared chart, redraws only the chart area and asks again');
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
  assert.match(fn(name), /if \(ctx->chart_wrap\) lv_obj_invalidate\(ctx->chart_wrap\);/,
    `${name} marks the whole chart dirty once`);
}

const data = maskCpp(readRepoFile('src/types/energy/energy_data.cpp'));
assert.match(data, /if \(!force && request\.last_attempt_ms != 0 &&\s*\(uint32_t\)\(now - request\.last_attempt_ms\) < kEnergyRequestThrottleMs\) \{\s*return true;/,
  'a background request is throttled per period');
const rawData = readRepoFile('src/types/energy/energy_data.cpp');
for (const period of ['week', 'month', 'year']) {
  assert.ok(rawData.includes(`if (strcmp(p, "${period}") == 0) return g_${period}_entries;`), `${period} has its own cache`);
}
assert.ok(rawData.includes('if (len == 4 && strncmp(begin, "year", len) == 0) return "year";'), 'a year answer goes to its own queue');

// The Week request names the panel's first weekday (tm_wday); the Bridge
// runs the week from it.
const mqtt = readRepoFile('src/network/mqtt/mqtt_handlers.cpp');
assert.match(mqtt, /if \(strcmp\(p, "week"\) == 0\) \{\s*payload \+= ",\\"week_start\\":";\s*payload \+= String\(week_start::panel\(\)\);/);
assert.ok(mqtt.includes('if (strcmp(p, "week") != 0 && strcmp(p, "month") != 0 && strcmp(p, "year") != 0) {'));

console.log('Energy popup: four periods prepared, loaded one after another in the background');
