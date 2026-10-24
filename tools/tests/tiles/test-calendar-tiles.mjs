// Calendar tile (27): persisted policies, registry, Bridge lists and topics,
// popup lifecycle hooks, translations, Web Admin fields and the preview, which
// follows the firmware's payload rules and layouts (types/calendar).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';

import {adminSource, extractDeliveredFunction} from '../../lib/admin-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

// ---- Type, policies and geometry ----
assert.match(read('src/types/tile_type.h'), /TILE_FAN = 26,\s*TILE_CALENDAR = 27\s*\};/, 'the next free persisted id');
const policy = read('src/types/tile_type_policy.h');
assert.match(policy, /static constexpr bool tileTypeIsCalendar\(int type\) \{ return type == TILE_CALENDAR; \}/);
for (const name of ['entityTileStoresSensorEntity', 'tileTypeStoresPopupMode', 'tileTypeStoresPopupModeDirectly']) {
  const body = policy.slice(policy.indexOf(name + '(TileType type)'), policy.indexOf('}', policy.indexOf(name + '(TileType type)')));
  assert.match(body, /tileTypeIsCalendar\(type\)/, name);
}
for (const name of ['tileTypeUsesCachedEntityState', 'tileTypeSubscribesDynamicState', 'tileTypeAllowedInScreensaver']) {
  const start = policy.indexOf(name + '(');
  const body = policy.slice(start, policy.indexOf('}', start));
  assert.doesNotMatch(body, /Calendar|CALENDAR/, `${name} leaves the Calendar out`);
}
const geometry = read('src/tiles/config/tile_geometry.h');
assert.match(geometry, /inline bool compact_calendar\(int type, float w, float h\) \{ return calendar\(type\) && w >= 1 && h == 0\.5f; \}/);

// ---- Registry and lifecycle ----
const registry = read('src/types/types_registry.cpp');
assert.match(registry, /TILE_CALENDAR,\s*"Kalender",\s*"calendar",\s*"calendar",\s*"calendar",\s*nullptr,\s*"loadCalendarFields",\s*"saveCalendarFields",\s*"resetCalendarFields",/);
assert.match(registry, /case TILE_CALENDAR: return i18n::calendar_label\(language, i18n::CalendarLabel::TypeCalendar\);/);
for (const type of ['camera', 'climate', 'cover', 'device', 'energy', 'light', 'media', 'pin', 'sensor', 'weather']) {
  assert.match(read(`src/ui/popups/${type}/${type}_popup.cpp`), /hide_calendar_popup\(\);/, `${type} hides the calendar popup`);
}
const calendarPopup = read('src/ui/popups/calendar/calendar_popup.cpp');
for (const type of ['pin', 'light', 'climate', 'cover', 'sensor', 'weather', 'energy', 'media', 'camera', 'device']) {
  assert.match(calendarPopup, new RegExp(`hide_${type}_popup\\(\\);`), `the calendar popup hides ${type}`);
}
assert.match(calendarPopup, /viewNavigationPopupShown\(pop\.card, pop\.target\.entities\.c_str\(\)\);/,
  'navigation compares the whole calendar list with the tile (viewNavigationService)');
assert.match(read('src/ui/navigation/view_navigation.cpp'), /hide_calendar_popup\(\);\s*hide_device_popup\(\);\n\}/);
assert.match(read('src/ui/navigation/view_navigation.cpp'), /case TILE_CALENDAR:\s*case TILE_LOCK:/);
assert.match(read('src/ui/ui_manager.cpp'), /preload_calendar_popup\(\);/);
assert.match(read('src/tiles/runtime/tile_icon_source.cpp'), /calendar_popup_follow_tile_color\(color\);/);
assert.match(read('src/tiles/runtime/tile_update_service.h'), /process_device_updates\(drain_all \? 0 : 4\);\s*process_calendar_updates\(drain_all \? 0 : 2\);/);
// The month view only where it is readable; the Web Admin offers it there only.
assert.match(read('src/types/calendar/calendar_tile.cpp'), /#if defined\(DEVICE_LAYOUT_480X480\)\s*constexpr bool kMonthAvailable = false;/);
assert.match(read('src/types/calendar/web.cpp'), /#if !defined\(DEVICE_LAYOUT_480X480\)\s*append_option\(html, "2", text\(i18n::CalendarLabel::ViewMonth\)\);\s*#endif/);

// ---- Bridge configuration, topics and Web Admin endpoints ----
const bridge = read('src/network/bridge/ha_bridge_config.cpp');
assert.match(bridge, /std::make_pair\("\\"calendars\\"", &merged\.calendars_text\)/, 'a reply without the key keeps the list');
assert.match(bridge, /parseEntityNameSection\(json, "calendar_meta", merged\.sensor_names_map\);/);
assert.match(bridge, /listEqualsIgnoringOrder\(a\.calendars_text, b\.calendars_text\)/);
const handlers = read('src/network/mqtt/mqtt_handlers.cpp');
assert.match(handlers, /\{&cfg\.calendars_text, "events"\}\};\s*\/\/ Events of calendars the Bridge stopped offering go\.\s*calendar_store::forget_except\(cfg\.calendars_text\);/);
assert.match(handlers, /if \(route\.topic\.endsWith\("\/events"\)\) \{\s*calendar_store::queue\(route\.entity_id, payload, payload_len\);\s*return true;\s*\}/);
const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(tiles, /appendList\("calendars", parseSensorList\(ha\.calendars_text\)\);/);
assert.match(tiles, /\\"calendar_values\\"/);
assert.match(read('src/network/bridge/ha_bridge_config.h'), /bool supportsCalendars\(\) const \{ return entity_search_ >= 5; \}/);

// ---- Store: bounded, coalesced, parsed on the loop ----
const store = read('src/types/calendar/calendar_store.cpp');
assert.match(store, /if \(length > calendar_events::kMaxPayload\) \{/, 'oversized payloads are dropped');
assert.match(store, /if \(g_entries\.size\(\) >= kMaxCalendars\) return;/, 'a bounded number of calendars');
assert.match(store, /entry\.payload\.compare\(0, length, payload, length\) == 0\) \{\s*return;/, 'an unchanged retained state is not parsed again');

// ---- Translations ----
const header = read('src/core/i18n/i18n.h');
const labelEnum = header.match(/enum class CalendarLabel : uint8_t \{([\s\S]*?)\n\};/)[1];
const labels = labelEnum.split('\n').map(line => line.replace(/\/\/.*$/, '').trim().replace(/,$/, '')).filter(Boolean);
assert.equal(labels.at(-1), 'Count');
assert.match(header, new RegExp(`const char\\* calendar_labels\\[${labels.length - 1}\\];`));
assert.match(header, /const char\* month_names\[12\];\s*\/\/[\s\S]*?uint8_t first_weekday;/);
const i18n = read('src/core/i18n/i18n.cpp');
for (const [lang, first, months, weekday] of [
  ['De', '"Kalender", "Kalender-Entität", "Kalender %u"', '"Januar", "Februar", "März"', '1,  // Monday'],
  ['En', '"Calendar", "Calendar entity", "Calendar %u"', '"January", "February", "March"', '0,  // Sunday'],
  ['Fr', '"Calendrier", "Entité calendrier", "Calendrier %u"', '"Janvier", "Février", "Mars"', '1,  // Monday'],
  ['Pl', '"Kalendarz", "Encja kalendarza", "Kalendarz %u"', '"Styczeń", "Luty", "Marzec"', '1,  // Monday'],
  ['Cs', '"Kalendář", "Entita kalendáře", "Kalendář %u"', '"Leden", "Únor", "Březen"', '1,  // Monday'],
  ['Es', '"Calendario", "Entidad de calendario", "Calendario %u"', '"Enero", "Febrero", "Marzo"', '1,  // Monday'],
  ['Sk', '"Kalendár", "Entita kalendára", "Kalendár %u"', '"Január", "Február", "Marec"', '1,  // Monday'],
]) {
  const start = i18n.indexOf(`static const LocaleProfile kLocale${lang} = {`);
  const body = i18n.slice(start, i18n.indexOf('\n};', start));
  const table = body.slice(body.indexOf(first));
  const strings = [...table.slice(0, table.indexOf('}') + 1).matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m => m[1]);
  assert.equal(strings.length, labels.length - 1, `${lang} calendar labels`);
  for (const text of strings) assert.ok(text.trim().length, `${lang} has no empty calendar label`);
  // printf patterns keep their arguments in every language.
  for (const [index, pattern] of [[2, /%u/], [12, /%u/], [13, /%u/], [14, /%u.*%u/], [15, /%u/], [17, /%s/], [19, /%u/]]) {
    assert.match(strings[index], pattern, `${lang} label ${index}`);
  }
  assert.ok(body.includes(months), `${lang} month names`);
  assert.ok(body.includes(weekday), `${lang} first weekday`);
}

// ---- Web Admin ----
const bundle = JSON.parse(read('src/web/admin/bundle.json')).sources;
assert.ok(bundle.indexOf('src/types/calendar/admin.js') > bundle.indexOf('src/types/device/admin.js'));
for (const name of ['loadCalendarFields', 'saveCalendarFields', 'resetCalendarFields', 'calendarPreviewExtraHtml',
  'parseCalendarPreviewPayload']) {
  assert.ok(adminSource.includes(`function ${name}(`), name);
  extractDeliveredFunction(name);
}
assert.match(read('src/web/admin/tiles/entity-picker.js'), /\['remote'\], \['calendar'\]\s*\];/);
assert.match(read('src/web/admin/tiles/entity-picker.js'), /'energy', 'scenes', 'calendars'\]\);/);
assert.match(read('src/web/admin/tiles/import-export.js'), /\} else if \(safeType === 27\) \{/);
assert.match(read('src/web/admin/tiles/snapshots.js'), /if \(Number\(tile\.type\) === 27 && snapshot\) \{/);

// The preview code itself, against the same data as the firmware test
// (test-calendar-model.mjs): a fixed "now" on Saturday 2026-10-24, 08:30.
const elements = {};
const element = id => (elements[id] ||= {value: '', options: [{value: '0'}, {value: '1'}, {value: '2'}]});
const sandbox = {
  console,
  escapeHtml: text => String(text).replace(/[&<>"]/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'})[c]),
  panelLight: () => false,
  maybeFillTitleFromEntity: () => {},
  document: {getElementById: element},
  CALENDAR_I18N: {
    labels: ['Calendar', 'Calendar entity', 'Calendar %u', 'View', 'Agenda', 'Week', 'Month', 'First day of the week',
      'Automatic', 'All day', 'No events', 'Nothing planned', 'in %u days', 'in %u h', 'in %u h %u min', 'in %u min',
      'Now', 'until %s', 'Up next', '%u events', '1 event', 'List', 'No calendar data'],
    months: ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October',
      'November', 'December'],
    monthsShort: ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'],
    weekdaysShort: ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'],
    today: 'Today', tomorrow: 'Tomorrow', shortDate: '{d} {m}', firstWeekday: 1, monthView: true
  },
  sensorMetaCache: {calendarValues: {
    'calendar.family': JSON.stringify({v: 1, state: 'on', from: '2026-09-24', to: '2026-12-31', e: [
      {s: '2026-10-24T19:00', e: '2026-10-24T22:00', t: 'Dinner <at> Anna\'s'},
      {s: '2026-10-27T08:00', e: '2026-10-27T08:45', t: 'Dentist'},
      {s: '2026-10-30', e: '2026-11-02', t: 'Weekend in Hamburg'}]}),
    'calendar.waste': JSON.stringify({v: 1, state: 'off', from: '2026-09-24', to: '2026-12-24', e: [
      {s: '2026-10-24', e: '2026-10-25', t: 'Bio waste'},
      {s: '2026-10-26', e: '2026-10-27', t: 'Paper'}]})
  }}
};
vm.createContext(sandbox);
vm.runInContext(read('src/types/calendar/admin.js') +
  '\nfunction calendarNow() { return {today: calendarDay(2026, 10, 24), minute: 8 * 60 + 30}; }', sandbox);
const run = code => vm.runInContext(code, sandbox);

assert.deepEqual(JSON.parse(run(`JSON.stringify(calendarEntitiesOf(' calendar.family,calendar.family,sensor.x,calendar.waste,calendar.a,calendar.b'))`)),
  ['calendar.family', 'calendar.waste', 'calendar.a', 'calendar.b']);
const family = run(`parseCalendarPreviewPayload(sensorMetaCache.calendarValues['calendar.family'])`);
assert.equal(family.valid && family.available, true);
assert.equal(family.events.length, 3);
assert.equal(family.events[2].endDay - family.events[2].startDay, 2, 'the all-day end date is exclusive');
assert.equal(run(`parseCalendarPreviewPayload('{"v":2}').valid`), false);
assert.equal(run(`parseCalendarPreviewPayload('').valid`), false);

const tile = (w, h, view = 0, list = 'calendar.family,calendar.waste') =>
  `({sensor_entity: '${list}', span_w: ${w}, span_h: ${h}, sensor_display_mode: ${view}, sensor_decimals: 255})`;
assert.equal(run(`calendarPreviewLayout(${tile(1, 0.5)})`), 'compact');
assert.equal(run(`calendarPreviewLayout(${tile(2, 1)})`), 'next');
assert.equal(run(`calendarPreviewLayout(${tile(2, 2)})`), 'agenda');
assert.equal(run(`calendarPreviewLayout(${tile(2, 1, 1)})`), 'week');
assert.equal(run(`calendarPreviewLayout(${tile(2, 2, 2)})`), 'month');
assert.equal(run(`calendarPreviewLayout(${tile(1, 2, 2)})`), 'agenda', 'a narrow month tile shows the agenda');
assert.equal(run(`calendarPreviewTitle(${tile(2, 2, 2)}, 'Family')`), 'October 2026');
assert.equal(run(`calendarPreviewTitle(${tile(1, 0.5)}, 'Family')`), '', 'half height shows the event instead');

// Today's all-day waste event runs now; the compact chip shows it.
const compact = run(`calendarPreviewExtraHtml(${tile(1, 0.5)})`);
assert.match(compact, /calendar-preview-when">All day<\/div><div class="tile-value calendar-preview-what">Bio waste</);
// Agenda: today, tomorrow-less Monday, escaped titles, calendar colors.
const agenda = run(`calendarPreviewExtraHtml(${tile(2, 3)})`);
assert.match(agenda, /calendar-preview-daylabel">Today</);
assert.match(agenda, />All day<\/span><span class="calendar-preview-bar" style="background:#4DB6AC"><\/span><span class="calendar-preview-title">Bio waste</);
assert.match(agenda, />19:00<\/span><span class="calendar-preview-bar" style="background:#5B9BF0"><\/span><span class="calendar-preview-title">Dinner &lt;at&gt; Anna's</);
assert.match(agenda, /calendar-preview-daylabel">Mon 26 Oct</);
// Without a payload: no data, not "nothing planned".
assert.match(run(`calendarPreviewExtraHtml(${tile(2, 2, 0, 'calendar.unknown')})`), /No calendar data/);
assert.match(run(`calendarPreviewExtraHtml(${tile(2, 2, 0, '')})`), /calendar-preview-message">--</);
// Next event of the family calendar alone counts down.
const next = run(`calendarPreviewExtraHtml(${tile(2, 1, 0, 'calendar.family')})`);
assert.match(next, /calendar-preview-what">Dinner &lt;at&gt; Anna's<\/div><div class="calendar-preview-when">19:00 · in 10 h 30 min</);
// Month: October 2026 from Monday 28 September, five rows, today marked.
const month = run(`calendarPreviewExtraHtml(${tile(2, 2, 2)})`);
assert.match(month, /--calendar-rows:5/);
assert.equal((month.match(/class="calendar-preview-day[^"]*"/g) || []).length, 35);
assert.match(month, /calendar-preview-day today"><b>24<\/b><span class="calendar-preview-dots"><i style="background:#5B9BF0"><\/i><i style="background:#4DB6AC"><\/i>/);
// Week strip on Sunday-first weeks starts Sunday the 18th.
assert.match(run(`calendarPreviewExtraHtml({...${tile(2, 1, 1)}, sensor_decimals: 0})`), /^<div class="calendar-preview-body"><div class="calendar-preview-grid week"[^>]*><span class="calendar-preview-day past"><small>Sun<\/small><b>18<\/b>/);

// Fields: save, load and reset keep the calendars, view and first weekday.
for (const [field, value] of [['calendar_entity', 'calendar.family'], ['calendar_entity_2', 'calendar.family'],
  ['calendar_entity_3', 'calendar.waste'], ['calendar_view', '2'], ['calendar_week_start', '0'],
  ['calendar_popup_open_mode', '0']]) {
  element('t_' + field).value = value;
}
run(`var saved = {}; saveCalendarFields('t', {append: (key, value) => { saved[key] = value; }});`);
assert.deepEqual(JSON.parse(run('JSON.stringify(saved)')), {
  calendar_entity: 'calendar.family', calendar_entity_2: 'calendar.waste', calendar_entity_3: '', calendar_entity_4: '',
  sensor_entity: 'calendar.family,calendar.waste', calendar_view: '2', calendar_week_start: '0', popup_open_mode: '0'
});
run(`resetCalendarFields('t')`);
assert.equal(element('t_calendar_entity').value, '');
assert.equal(element('t_calendar_week_start').value, '255');
run(`loadCalendarFields('t', {sensor_entity: 'calendar.waste,calendar.family', sensor_display_mode: 1, sensor_decimals: 6, popup_open_mode: 0})`);
assert.equal(element('t_calendar_entity').value, 'calendar.waste');
assert.equal(element('t_calendar_entity_2').value, 'calendar.family');
assert.equal(element('t_calendar_view').value, '1');
assert.equal(element('t_calendar_week_start').value, '6');
assert.equal(element('t_calendar_popup_open_mode').value, '0');

console.log('Calendar tiles: policies, registry, Bridge lists and events, popup hooks, translations and Web Admin preview');
