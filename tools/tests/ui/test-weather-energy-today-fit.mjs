// French and Polish in the Weather and Energy popups (V2, 2026-10-03).
//   - The Weather Today button cut "Aujourd'hui" to "ujourd'h": it now keeps
//     the unit font and lengthens into a pill to the left; 7D and the date
//     pills move over by the same width.
//   - The Energy Today button shrank "Aujourd'hui" to 20 px although there
//     was room: it keeps the 24 px font of 7D and lengthens instead.
//   - The 7D column header wrapped "Aujourd'hui" inside the word: it stays on
//     one line and steps its font down only when the column is too narrow.
//   - The popup said "sunny" above a tile saying "partly cloudy": every
//     forecast entry carries "condition", the scanner takes the first key it
//     finds, so the header now reads the entity state first, like the tile.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const read = file => maskCpp(readRepoFile(file)).replace(/\r\n?/g, '\n');
const body = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const weather = read('src/ui/popups/weather/weather_popup.cpp');
const energy = read('src/ui/popups/energy/energy_popup.cpp');

// Weather Today pill.
const todayWidth = body(weather, 'footer_today_width');
assert.match(todayWidth, /lv_text_get_size\(&size, weather_today_button_text\(\), FONT_UNIT,/);
assert.match(todayWidth, /const int width = size\.x \+ kFooterButtonHeight \/ 2;/);
assert.match(todayWidth, /return width > kFooterActionButtonWidth \? width : kFooterActionButtonWidth;/);
const buttons = body(weather, 'update_mode_buttons');
assert.match(buttons, /const int today_extra = today_w - kFooterActionButtonWidth;/);
assert.match(buttons, /set_footer_pill_width\(ctx->week_range_pill, ctx->week_range_label, kFooterDatePillWidth - today_extra\);/);
assert.match(buttons, /set_footer_pill_width\(ctx->detail_title_pill, ctx->detail_title_label,\s*kFooterDatePillWidth - today_extra\);/);
assert.match(buttons, /lv_obj_set_width\(ctx->header_today_btn, today_w\);/);
assert.match(buttons, /const int week_x = today_day >= 0 \? kFooterOuterActionX - today_extra : kFooterInnerActionX;/);

// Energy Today pill keeps 24 px.
const fit = body(energy, 'fit_today_button');
assert.doesNotMatch(fit, /font20/, 'Energy Today never shrinks its text');
assert.match(fit, /const int width = text_width\(text, popup_layout::font24\(\)\) \+ kRangeButtonHeight \/ 2;/);
assert.match(fit, /const int button_w = width > kRangeButtonWidth \? width : kRangeButtonWidth;/);

// 7D column header on one line.
assert.match(weather, /const lv_font_t\* day_font =\s*fitting_day_font\(text\.c_str\(\), col_w, popup_layout::font20\(\)\);/);
// No tile header here: it brings tile_renderer_shared.h, whose global
// set_label_style() made every call in this file ambiguous (b203 build).
assert.doesNotMatch(weather, /#include "src\/tiles\/runtime\/tile_header\.h"/);
assert.match(weather, /lv_obj_set_height\(fw\.day_label, full_line\);/, 'the icons below stay aligned');

// Header condition like the tile: state first.
// String literals stay unmasked here.
const raw = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const current = body(raw('src/ui/popups/weather/weather_popup.cpp'), 'resolve_current_weather_fields');
assert.ok(current.indexOf('"state"') > 0 && current.indexOf('"state"') < current.indexOf('"condition"'),
  'the entity state comes before any forecast condition');
assert.match(body(weather, 'apply_weather_header'), /resolve_current_weather_fields\(json, condition, icon_name\);/);
const tile = raw('src/tiles/runtime/tile_renderer.cpp');
assert.match(tile, /if \(!extract_json_string_field\(json, "state", condition\)\) \{\s*extract_json_string_field\(json, "condition", condition\);/,
  'the tile reads the same order');

// The round Today buttons take the standard short form where the word is long
// (French "Auj."); the date pill keeps the word, a 7D column uses it where it fits.
const i18n = raw('src/core/i18n/i18n.cpp');
const todayFields = code => {
  const head = 'static const LocaleProfile kLocale' + code + ' = {';
  const block = i18n.slice(i18n.indexOf(head), i18n.indexOf('\nstatic const ', i18n.indexOf(head) + head.length));
  const literals = [...block.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m => m[1]);
  return {today: literals[3], button: literals[5]};
};
assert.deepEqual(todayFields('De'), {today: 'Heute', button: 'Heute'});
assert.deepEqual(todayFields('En'), {today: 'Today', button: 'Today'});
assert.deepEqual(todayFields('Fr'), {today: "Aujourd'hui", button: 'Auj.'});
assert.deepEqual(todayFields('Pl'), {today: 'Dziś', button: 'Dziś'});
const weatherRaw = raw('src/ui/popups/weather/weather_popup.cpp');
assert.match(body(weatherRaw, 'weather_today_button_text'), /i18n::weather_today_button_label\(/);
assert.match(weatherRaw, /String title = is_today \? String\(weather_today_text\(\)\)/, 'the date pill keeps the word');
assert.match(weatherRaw, /text = weather_today_text\(\);[\s\S]{0,260}if \(full\.x > col_w\) text = weather_today_button_text\(\);/,
  'a 7D column shows the word where it fits, else the short form');
assert.match(body(energy, 'fit_today_button'), /const char\* text = today_button_label\(\);/);

// Why the order matters: the scanner returns the first "condition" in the text.
const payload = '{"forecast":[{"datetime":"2026-10-03","condition":"sunny"}],"state":"partlycloudy","icon":"partlycloudy"}';
const firstValue = key => (payload.match(new RegExp(`"${key}":"([^"]*)"`)) || [])[1];
assert.equal(firstValue('condition'), 'sunny');
assert.equal(firstValue('state'), 'partlycloudy');

console.log('Weather and Energy: Today lengthens instead of cutting or shrinking, 7D header on one line, header condition as on the tile');
