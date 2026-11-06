// Weather graph tile: a linked pair of full-width temperature graphs, one
// whole 00-23 day each, with a Yandex condition band (no numeric
// precipitation from the free integration). Passed hours of today merge from
// history entries with "src":"hist": hist wins in the past, forecast wins
// for the present and future.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (name) => fs.readFileSync(path.join(root, name), 'utf8').replace(/\r\n/g, '\n');
const has = (file, ...markers) => {
  const text = read(file);
  for (const marker of markers) assert.ok(text.includes(marker), `${file}: ${marker}`);
};

has('src/types/tile_type.h', 'TILE_WEATHER_GRAPH = 30');
has('src/types/tile_type_policy.h',
  'type == TILE_WEATHER_GRAPH', 'tileTypeHasDynamicMqttRoute');
assert.ok(!read('src/types/tile_type_policy.h').includes('TILE_WEATHER_GRAPH || type == TILE_EMPTY'));
has('src/types/types_registry.cpp', 'TILE_WEATHER_GRAPH', 'loadWeatherGraphFields',
  'saveWeatherGraphFields', 'render_weather_graph_wrapper');
// The panel's own hourly cache: past hours persist in LittleFS, fill the
// "Today" past on the panel, and are appended to the preview payload.
has('src/types/weather/graph_history.h', 'store_hour', 'lookup_hour', 'cached_temperature');
has('src/types/weather/graph_history.cpp', '_weather_hist.bin', 'kDayBuckets');
has('src/types/weather/graph_renderer.cpp', 'weather_graph_append_cache',
  'weather_history::store_hour', 'weather_history::lookup_hour');
has('src/web/server/handlers/web_admin_tiles.cpp', 'weather_graph_append_cache');
has('src/types/weather/graph_renderer.cpp', 'forecast_hourly', 'condition_color',
  'lv_chart_create');
// The flattened hourly keys the bridge may send, the 00-23 axis with a mark
// every three hours, the big value font for the readouts, and the history
// merge ("src":"hist" fills the past, forecast covers present and future).
has('src/types/weather/graph_renderer.cpp', 'json_string(object, "c", hour.condition)',
  'json_string(object, "i", hour.icon)', 'FONT_VALUE', 'kHourStep', 'line_crosses',
  'json_string(object, "src", src)', 'is_hist');
// The type label in all eight languages (a lossy conversion once replaced the
// accented and Cyrillic letters with question marks).
has('src/core/i18n/i18n.cpp', '"Weather graph"', '"Wetterdiagramm"',
  '"Graphique météo"', '"Wykres pogody"', '"Graf počasí"',
  '"Gráfico del tiempo"', '"Graf počasia"', '"График погоды"');
has('src/types/weather/graph_web_html.cpp', 'weather_graph_entity',
  'weather_graph_day', 'weather_graph_pair_id');
has('src/types/weather/graph_web_handler.cpp', 'weather_graph_entity',
  'weather_graph_pair_id', 'sensor_display_mode');
has('src/web/server/handlers/web_admin_tiles.cpp', 'syncWeatherGraphPair',
  'weather_graph_values');
has('src/web/admin/tiles/state.js', 'weatherGraphValues', 'weather_graph_values');
has('src/types/weather/admin-graph.js', 'loadWeatherGraphFields',
  'saveWeatherGraphFields', 'weather_graph_pair_id', 'applyWeatherGraphPreview',
  "createElement('div')", 'titleEl.textContent', 'isHist');
has('src/web/admin/tiles/live-preview.js', "previewKind === 'weather_graph'");
has('src/web/admin/tiles/grid-preview.js', "previewKind === 'weather_graph'");
// The default new-tile size is the wide strip: 4 x 1.5 on 4 x 3, 7 x 2 on 7 x 5.
const layout = read('src/web/admin/tiles/layout.js');
assert.match(layout, /Number\(type\) === 30/);
assert.match(layout, /placeCols\(\) >= 7 \? 2 : 1\.5/);
has('src/web/assets/admin.css', '.tile-weather-graph');
has('src/network/mqtt/mqtt_handlers.cpp', 'TILE_WEATHER_GRAPH');
has('src/ui/tabs/tiles/tab_tiles_unified.cpp', 'TILE_WEATHER_GRAPH');
has('src/network/bridge/entity_search.cpp', 'TILE_WEATHER_GRAPH');
// The pair is linked: day and the shared look come from the saved half, while
// the name, position and size stay per tile.
const sync = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.ok(!/other\.title = source\.title;/.test(sync), 'the name is not mirrored onto the partner');
assert.match(sync, /other\.sensor_display_mode == source\.sensor_display_mode/);
// The panel shows the tile's own name, the day only as the fallback
// (graph_renderer.cpp graph_title).
assert.match(read('src/types/weather/graph_renderer.cpp'),
  /if \(tile\.title\.length\(\)\) return tile\.title;/);
// Screensaver: type 30 is selectable, has its own widget bank, refreshes and
// resets with the other screensaver tiles; the pair sync runs on the saved
// grid, so it links halves on the screensaver too.
const policy = read('src/types/tile_type_policy.h');
assert.match(policy, /tileTypeAllowedInScreensaver[\s\S]*?TILE_WEATHER_GRAPH/);
has('src/tiles/runtime/tile_renderer.cpp', 'weather_graphs[4]',
  'g_screensaver_weather_graph', 'SCREENSAVER) return g_screensaver_weather_graph',
  'alias TAB0');
has('src/ui/screensaver/image_screensaver.cpp', 'TILE_WEATHER_GRAPH',
  'queue_weather_tile_update(GridType::SCREENSAVER',
  'reset_weather_widget(GridType::SCREENSAVER, slot)');
assert.ok(read('src/web/server/render/web_admin_html.cpp').includes('value=\\"30\\"'),
  'screensaver type list offers the weather graph');
// Screensaver tiles may use the whole screen: no reserved bottom rows, and
// the graph lays out from the live card size.
has('src/web/server/handlers/web_admin_tiles.cpp', 'only the grid bounds apply');
has('src/ui/screensaver/screensaver_places.cpp', 'no rows are reserved');
has('src/web/admin/folders/navigation.js', 'tiles may go in any row');
has('src/types/weather/graph_renderer.cpp', 'live_w');
// The preview branch itself: the panel's payload (with the hourly forecast
// kept in weather_graph_values) must draw the line and the condition band.
// A fixed clock (10:30 local) keeps the readouts and the hist/forecast
// priority deterministic: hours below 10 are past, hours above are future.
const fixedMs = new Date(2026, 10, 4, 10, 30, 0).getTime();
class FixedDate extends Date {
  constructor(...args) {
    if (args.length === 0) super(fixedMs);
    else super(...args);
  }
  static now() {
    return fixedMs;
  }
}
const sandbox = {WEATHER_I18N: {today: 'Сегодня', tomorrow: 'Завтра'}, console, Date: FixedDate};
vm.createContext(sandbox);
vm.runInContext(read('src/types/weather/admin-graph.js'), sandbox, {filename: 'admin-graph.js'});
const pad = value => String(value).padStart(2, '0');
const localDate = date => date.getFullYear() + '-' + pad(date.getMonth() + 1) + '-' + pad(date.getDate());
const now = new Date(fixedMs);
const today = localDate(now);
const tomorrow = localDate(new Date(fixedMs + 86400000));
const hourly = [];
for (let hour = 0; hour < 24; hour++) {
  // Hour 11 is cold and hour 12 jumps: the readout the line crosses must be
  // drawn below it, the clear ones above (graph_renderer.cpp).
  const temperature = hour === 11 ? 0 : hour === 12 ? 30 : hour / 2;
  // The rain episode uses the flattened keys ("c"/"i") the bridge may send:
  // precipitation must show for both spellings.
  if (hour >= 12 && hour < 15) {
    hourly.push({date_local: today, hour_local: hour, temperature,
      c: 'rain', i: 'weather-rainy'});
  } else {
    hourly.push({date_local: today, hour_local: hour, temperature,
      condition: 'clear'});
  }
  hourly.push({date_local: tomorrow, hour_local: hour, temperature: 10 + hour / 2,
    // Hour 6 counts millimetres only: precipitation must show by number too.
    ...(hour === 6 ? {p: 2} : {condition: 'cloudy'})});
}
const previewPayload = JSON.stringify({forecast_hourly: hourly, units: {temperature: '°C'}});
const draw = (day) => {
  const host = {innerHTML: '', clientWidth: 600, clientHeight: 160};
  const title = {textContent: ''};
  const el = {querySelector: selector => selector === '.tile-weather-graph' ? host
    : (selector === '.tile-title' ? title : null)};
  sandbox.applyWeatherGraphPreview(el, {
    sensor_entity: 'weather.test', sensor_display_mode: day, title: 'Дом'
  }, {weatherGraphValues: {'weather.test': previewPayload}, names: {}});
  return {host, title};
};
const shownToday = draw(0);
assert.match(shownToday.host.innerHTML, /<polyline /);
assert.match(shownToday.host.innerHTML, /<rect /);
// Today shows the whole day, 00 to 23, like tomorrow: the line starts at 00.
assert.match(shownToday.host.innerHTML, /points="0\.00,/);
// Hour marks every three hours, starting at 00.
assert.match(shownToday.host.innerHTML, />03</);
assert.match(shownToday.host.innerHTML, />21</);
// Temperature readouts every three hours.
assert.match(shownToday.host.innerHTML, /3°</);
// A readout the line crosses is drawn below it, a clear one above it. The
// curve is sampled four times per hour, so hour 12 is sample 48, hour 6 is 24.
const polyline = /points="([^"]+)"/.exec(shownToday.host.innerHTML)[1]
  .split(' ').map(pair => pair.split(',').map(Number));
assert.equal(polyline.length, 24 + 23 * 3, 'the curve is sampled four times per hour');
const crossed = /<text x="[\d.]+" y="([\d.]+)"[^>]*>30°<\/text>/.exec(shownToday.host.innerHTML);
assert.ok(crossed, 'the 30° readout exists');
assert.ok(Number(crossed[1]) > polyline[48][1],
  'the crossed readout sits below the line: ' + crossed[1] + ' vs ' + polyline[48][1]);
const clear = /<text x="[\d.]+" y="([\d.]+)"[^>]*>3°<\/text>/.exec(shownToday.host.innerHTML);
assert.ok(clear, 'the 3° readout exists');
assert.ok(Number(clear[1]) < polyline[24][1],
  'the clear readout sits above the line: ' + clear[1] + ' vs ' + polyline[24][1]);
// The named precipitation episode, from the flattened keys too.
assert.match(shownToday.host.innerHTML, /rainy</);
assert.match(shownToday.host.innerHTML, /#3d8bfd/);
// No min/max range on the left, no day word on the right.
assert.ok(!shownToday.host.innerHTML.includes('°C'), 'no range labels');
assert.ok(!shownToday.host.innerHTML.includes('Сегодня'), 'no day text in the graph');
// The name in the "Name" field titles the tile; the day is only the fallback.
assert.equal(shownToday.title.textContent, 'Дом');
const drawUnnamed = (day) => {
  const host = {innerHTML: '', clientWidth: 600, clientHeight: 160};
  const title = {textContent: ''};
  const el = {querySelector: selector => selector === '.tile-weather-graph' ? host
    : (selector === '.tile-title' ? title : null)};
  sandbox.applyWeatherGraphPreview(el, {
    sensor_entity: 'weather.test', sensor_display_mode: day, title: '   '
  }, {weatherGraphValues: {'weather.test': previewPayload}, names: {}});
  return title;
};
assert.equal(drawUnnamed(0).textContent, 'Сегодня', 'an unnamed half shows the day');
assert.equal(drawUnnamed(1).textContent, 'Завтра', 'an unnamed half shows the day');
const shownTomorrow = draw(1);
assert.match(shownTomorrow.host.innerHTML, /<polyline /);
// Millimetres without a condition word count as rain, in both directions.
assert.match(shownTomorrow.host.innerHTML, /#3d8bfd/);
assert.match(shownTomorrow.host.innerHTML, /rainy</);
assert.equal(shownTomorrow.title.textContent, 'Дом');

// The pair only draws today and tomorrow: the parser skips every other date
// (yesterday's history, a third forecast day), or those entries filled
// kMaxParsedHours = 48 before tomorrow was reached and the tomorrow half drew
// "--" while the Web Admin preview, which has no limit, showed it.
const renderer = read('src/types/weather/graph_renderer.cpp');
assert.match(renderer, /bool keep_pair_only/);
assert.match(renderer,
  /keep_pair_only && out\.has_time && date != out\.today &&\s*date != pair_tomorrow/);
// A payload whose dates do not match the clock still parses without the filter.
assert.match(renderer,
  /void parse_graph_data\(const String& payload, GraphData& out\) \{\s*parse_graph_data_impl\(payload, out, true\);/);
assert.match(renderer,
  /if \(!out\.count\) parse_graph_data_impl\(payload, out, false\);/);
// The tile paints from the cached payload while it is built: the retained
// state can arrive before the card exists and the next forecast message is
// hours away (user 2026-11-05: the tile stayed at "--" until a grid rebuild).
assert.match(renderer,
  /tiles_get_cached_entity_payload\(tile\.sensor_entity\.c_str\(\), payload\)/);
const renderEnd = renderer.indexOf('layout_graph(widgets, card, tile_w, tile_h, 0);');
const renderReturn = renderer.indexOf('return card;', renderEnd);
assert.ok(renderEnd >= 0 && renderReturn > renderEnd, 'the render ends with layout_graph');
const paintAt = renderer.indexOf('tiles_get_cached_entity_payload', renderEnd);
assert.ok(paintAt > renderEnd && paintAt < renderReturn,
  'the cached payload is read before the card is returned');
// The size the layout pass gives the card repaints the graph for that size.
assert.match(renderer, /lv_obj_add_event_cb\(card, on_graph_card_resized, LV_EVENT_SIZE_CHANGED, ref\);/);
assert.match(renderer, /target\[ref->index\]\.last_hash = 0;/);

// The current hour on the today half: a dashed vertical line with its dot on
// the curve, drawn as an LVGL chart cursor (the dot takes its place from the
// series, so it follows the curve), hidden on the tomorrow half.
const marker = read('src/types/weather/graph_renderer.cpp');
assert.match(marker, /lv_chart_add_cursor\(/);
assert.match(marker, /static_cast<lv_dir_t>\(LV_DIR_VER \| LV_DIR_TOP \| LV_DIR_BOTTOM\)/);
for (const style of ['line_dash_width', 'line_dash_gap', 'LV_PART_CURSOR']) {
  assert.ok(marker.includes(style), `the now marker styles ${style}`);
}
assert.match(marker, /lv_chart_set_cursor_point\(widgets\.chart, widgets\.now_cursor/);
assert.ok(marker.includes('LV_CHART_POINT_NONE'),
  'the marker hides without a current hour');
assert.match(marker,
  /data\.has_time && tile->sensor_display_mode != 1 &&\s*data\.now_pos >= 0\.0f/,
  'only the today half carries the marker');
// The current time reaches the marker as hours with minutes.
assert.match(marker, /float now_pos = -1\.0f;/);
assert.match(marker, /pos = static_cast<float>\(hour\) \+ local\.tm_min \/ 60\.0f;/);

// The current hour marker: dashed line plus its dot on the today half only.
assert.match(shownToday.host.innerHTML, /stroke-dasharray="5 4"/);
assert.match(shownToday.host.innerHTML, /<circle /);
assert.ok(!shownTomorrow.host.innerHTML.includes('stroke-dasharray'),
  'the tomorrow half has no current-hour marker');

// History merge: the past comes from "src":"hist", the future from forecast.
// Duplicates resolve by time however they are ordered: hist wins at hour 3
// (past, forecast listed second), forecast wins at hour 12 (future, hist
// listed second). Hour 5 arrives as hist rain, so the band stays colored.
const mergeHourly = [];
for (let hour = 0; hour < 24; hour++) {
  if (hour < 10) {
    mergeHourly.push({date_local: today, hour_local: hour, temperature: hour,
      ...(hour === 5 ? {c: 'rain', i: 'weather-rainy'} : {condition: 'clear'}),
      src: 'hist'});
  } else {
    mergeHourly.push({date_local: today, hour_local: hour,
      temperature: 10 + hour / 2, condition: 'clear'});
  }
}
mergeHourly.push({date_local: today, hour_local: 3, temperature: 99, condition: 'clear'});
mergeHourly.push({date_local: today, hour_local: 12, temperature: -40,
  condition: 'clear', src: 'hist'});
const mergePayload = JSON.stringify({forecast_hourly: mergeHourly});
const drawMerge = () => {
  const host = {innerHTML: '', clientWidth: 600, clientHeight: 160};
  const title = {textContent: ''};
  const el = {querySelector: selector => selector === '.tile-weather-graph' ? host
    : (selector === '.tile-title' ? title : null)};
  sandbox.applyWeatherGraphPreview(el, {
    sensor_entity: 'weather.test', sensor_display_mode: 0, title: 'Дом'
  }, {weatherGraphValues: {'weather.test': mergePayload}, names: {}});
  return {host, title};
};
const merged = drawMerge();
assert.match(merged.host.innerHTML, /points="0\.00,/, 'hist fills the past from 00');
assert.match(merged.host.innerHTML, />3°</, 'hist wins the past duplicate at hour 3');
assert.match(merged.host.innerHTML, />16°</, 'forecast wins the future duplicate at hour 12');
assert.match(merged.host.innerHTML, /#3d8bfd/, 'hist rain colors the band');

// A payload that only reaches the entity cache (the panel slept, or the grid
// did not exist yet) must be replayed: the Web Admin reads the cache and shows
// it, while the panel kept its old drawing until the grid was rebuilt (user
// 2026-11-06: "Завтра" showed "--" in the panel and the value in the Web
// Admin until the day toggle rebuilt the tile).
const tabs = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
assert.match(tabs, /static bool g_weather_replay_pending = false;/);
assert.match(tabs,
  /if \(powerManager\.isInSleep\(\) \|\| !tiles_is_loaded\(grid_type\)\) \{\s*g_weather_replay_pending = true;\s*return;\s*\}/,
  'the skipped update is remembered');
assert.match(tabs, /void tiles_replay_deferred_updates\(\) \{[\s\S]{0,400}apply_cached_states\(grid_type, getGridConfig\(grid_type\)\);/,
  'the replay runs from the cached states');
assert.match(read('src/tiles/runtime/tile_update_service.h'),
  /tiles_replay_deferred_updates\(\);\s*\/\/ First the states|tiles_replay_deferred_updates\(\);\s*process_sensor_update_queue/,
  'the main loop replays before it drains the queues');
// The graph consumes its payload hash only when the draw can run.
assert.match(renderer,
  /if \(widgets\.last_hash == hash\) return;\s*const Tile\* tile = tile_renderer_get_tile_config\(grid_type, grid_index\);[\s\S]{0,200}?widgets\.last_hash = hash;/,
  'a failed tile lookup does not swallow the payload');

console.log('Weather graph tile: type, pair, renderer, routes and Web Admin contract');
