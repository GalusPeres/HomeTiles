// The panel's first day of the week (user 2026-10-29): Web Admin Settings >
// Localization, with the Calendar tile's choices (Automatic = the language's,
// Monday, Sunday, Saturday). It runs the Energy popup's week and its month
// lines, the Calendar tile under Automatic and the Sensor graph's 30D lines.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

// Stored like the time and date formats: loaded, normalized, saved, compared
// and reset.
const header = read('src/core/config/config_manager.h');
assert.match(header, /uint8_t global_date_format;[\s\S]{0,400}uint8_t week_start;/);
const config = read('src/core/config/config_manager.cpp');
for (const line of [
  'a.week_start == b.week_start &&',
  'calendar_model::normalize_week_start(prefs.getUChar("week_start", calendar_model::kWeekStartAuto));',
  'normalized.week_start = calendar_model::normalize_week_start(normalized.week_start);',
  'prefs.putUChar("week_start", normalized.week_start);',
]) assert.ok(config.includes(line), line);
assert.equal((config.match(/config\.week_start = calendar_model::kWeekStartAuto;/g) || []).length, 2,
  'Automatic in the constructor and after a reset');

// The settings page: one select in the Localization grid, posted with the form.
const html = read('src/web/server/render/web_admin_html.cpp');
assert.ok(html.includes('<select id="locale_week_start" name="locale_week_start">'));
assert.ok(html.indexOf('locale_week_start') > html.indexOf('locale_date_format') &&
  html.indexOf('locale_week_start') < html.indexOf('</section>', html.indexOf('locale_date_format')), 'in the Localization section');
// With its colon like the rows above it (user 2026-10-29: "bitte mit
// Doppelpunkt"), French with the non-breaking space before it.
assert.ok(html.includes('appendHtmlEscaped(html, tr.week_start_label);'), 'the Localization label');
const strings = read('src/core/i18n/i18n.cpp');
for (const label of ['"Erster Wochentag:"', '"First day of the week:"', '"Premier jour de la semaine\\u00A0:"',
  '"Pierwszy dzień tygodnia:"', '"První den týdne:"', '"Primer día de la semana:"', '"Prvý deň týždňa:"']) {
  assert.ok(strings.includes(label), label);
}
const options = fn(html, 'buildWeekStartOptionsHtml');
assert.match(options, /append\(calendar_model::kWeekStartAuto, automatic\.c_str\(\)\);\s*append\(1, loc\.weekday_names\[1\]\);\s*append\(0, loc\.weekday_names\[0\]\);\s*append\(6, loc\.weekday_names\[6\]\);/);
const handlers = read('src/web/server/handlers/web_admin_handlers.cpp');
assert.match(handlers, /if \(server\.hasArg\("locale_week_start"\)\) \{\s*cfg\.week_start = calendar_model::normalize_week_start\(server\.arg\("locale_week_start"\)\.toInt\(\)\);/);

// One resolution for the whole panel.
const weekStart = read('src/core/config/week_start.h');
assert.match(weekStart, /return calendar_model::resolve_week_start\(cfg\.week_start, i18n::locale\(cfg\.language\)\.first_weekday\);/);
assert.match(read('src/types/calendar/calendar_text.h'), /return calendar_model::resolve_week_start\(stored, ::week_start::panel\(\)\);/,
  'a Calendar tile on Automatic follows the panel');
const calendarWeb = read('src/types/calendar/web.cpp');
assert.ok(calendarWeb.includes('automatic += loc.weekday_names[week_start::panel()];') &&
  calendarWeb.includes('doc["firstWeekday"] = week_start::panel();'), 'the tile editor and its preview name the panel\'s day');
assert.ok(read('src/ui/popups/sensor/sensor_popup.cpp').includes('if (day_tm.tm_wday != first_weekday) continue;'), 'Sensor 30D');
assert.ok(read('src/ui/popups/energy/energy_popup.cpp').includes('const int first_weekday = week_start::panel();'), 'Energy month lines');

// The Energy popup's period words in every language.
const i18n = read('src/core/i18n/i18n.cpp');
for (const code of ['De', 'En', 'Fr', 'Pl', 'Cs', 'Es', 'Sk']) {
  const head = `static const LocaleProfile kLocale${code} = {`;
  const start = i18n.indexOf(head);
  assert.ok(start >= 0, code);
  const block = i18n.slice(start, i18n.indexOf('\nstatic const ', start + head.length));
  assert.match(block, /\{"[^"]+", "[^"]+", "[^"]+"\}\};/, `${code}: Year, Month, Week at the end`);
}
assert.ok(i18n.includes('{"Jahr", "Monat", "Woche"}};') && i18n.includes('{"Year", "Month", "Week"}};'));

console.log('Week start: Localization setting for Energy, Calendar and the Sensor graph');
