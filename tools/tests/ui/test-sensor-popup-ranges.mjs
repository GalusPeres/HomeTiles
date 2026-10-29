// The Sensor popup's numeric graph offers five ranges, 30D 7D 24H 6H 1H, the
// longest on the left (user 2026-10-29). 1H by the minute with a line every
// quarter hour, 30D in three-hour steps (eight a day, fewer points than any
// graph has columns) with a line and the date at every Monday; its readout
// shows the date. Binary, state and editable histories keep 24H and 7D.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const source = readRepoFile('src/ui/popups/sensor/sensor_popup.cpp').replace(/\r\n?/g, '\n');
const fn = name => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

for (const line of ['constexpr uint16_t kHistoryHours1h = 1;', 'constexpr uint16_t kHistoryPeriodMinutes1h = 1;',
  'constexpr uint16_t kHistoryPoints1h = 60;', 'constexpr uint16_t kHistoryHours30d = 720;',
  'constexpr uint16_t kHistoryPeriodMinutes30d = 180;', 'constexpr uint16_t kHistoryPoints30d = 240;']) {
  assert.ok(source.includes(line), line);
}
const config = fn('get_history_range_config');
assert.ok(config.includes('return {kHistoryHours1h, kHistoryPeriodMinutes1h, kHistoryPoints1h};') &&
  config.includes('return {kHistoryHours30d, kHistoryPeriodMinutes30d, kHistoryPoints30d};'));

// The row: five buttons, the longest range left, all clickable.
const body = fn('build_popup_body');
const order = ['"30D"', '"7D"', '"24H"', '"6H"', '"1H"'].map(label => body.indexOf(`make_range_button(${label})`));
assert.ok(order.every((at, i) => at > 0 && (i === 0 || at > order[i - 1])), 'longest left');
assert.ok(body.includes('lv_obj_set_size(range_row, (kRangeButtonWidth * 5) + kRangeButtonGap * 4, popup_layout::kNavHeight);'));
assert.ok(body.includes('lv_obj_add_event_cb(btn, on_range_click, LV_EVENT_CLICKED, ctx);'));
const click = fn('on_range_click');
assert.ok(click.includes(': target == ctx->range_hour_btn  ? SensorHistoryRange::Hour1') &&
  click.includes(': target == ctx->range_month_btn ? SensorHistoryRange::Day30'));
const buttons = fn('update_range_buttons');
assert.ok(buttons.includes('selected == SensorHistoryRange::Hour1') && buttons.includes('selected == SensorHistoryRange::Day30'));
// Only numeric graphs show the three extra ranges.
const shown = fn('show_hours_button');
assert.ok(shown.includes('const int buttons = shown ? 5 : 2;') &&
  shown.includes('for (lv_obj_t* btn : {ctx->range_month_btn, ctx->range_hours_btn, ctx->range_hour_btn}) {'));

// The time axis and the readout.
const axis = fn('calc_time_axis');
assert.ok(axis.includes('SensorHistoryRange::Hour1) return calc_quarter_axis(') &&
  axis.includes('SensorHistoryRange::Day30) return calc_day30_axis('));
assert.ok(fn('calc_quarter_axis').includes('constexpr time_t kQuarter = 15 * 60;'));
const month = fn('calc_day30_axis');
assert.ok(month.includes('const int first_weekday = week_start::panel();') &&
  month.includes('if (day_tm.tm_wday != first_weekday) continue;') && month.includes('out_labels[count] = short_date_label(day_tm);'),
  'a line and the date at every first weekday (Settings > Localization, user 2026-10-29)');
assert.ok(fn('short_date_label').includes('i18n::format_short_date(language, day.tm_mday, i18n::weather_month_short(language, day.tm_mon + 1))'),
  'the date in the configured language');
const readout = fn('append_readout_time');
assert.ok(readout.includes('if (key != date_key || strncmp(language, date_language, sizeof(date_language)) != 0) {'),
  'formatted again only for another day or language, not per cursor move');
assert.ok(source.includes('ctx->history_range == SensorHistoryRange::Day30);'), '30D readout with the date');

console.log('Sensor popup: five ranges 30D 7D 24H 6H 1H');
