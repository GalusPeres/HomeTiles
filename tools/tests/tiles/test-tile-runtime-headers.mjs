import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const headers = [
  'src/types/sensor/widgets.h', 'src/types/switch/state.h',
  'src/types/cover/state.h', 'src/types/binary_sensor/state.h',
  'src/types/climate/state.h', 'src/types/weather/widgets.h',
  'src/types/media/widgets.h', 'src/tiles/runtime/tile_renderer.h',
];
const compiler = [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version']).status === 0);
if (!compiler) {
  console.log('SKIP: tile runtime header checks require a C++ compiler');
  process.exit(0);
}

// Only platform headers are replaced. In particular, Climate uses the actual
// TileConfig geometry definition and Weather uses actual device selection.
const source = `
#include "src/tiles/runtime/tile_renderer.h"
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <string>

static std::string expected_csv(unsigned mask,
                               std::initializer_list<const char*> names) {
  std::string value;
  unsigned index = 0;
  for (const char* name : names) {
    if (mask & (1U << index)) {
      if (!value.empty()) value += ',';
      value += name;
    }
    ++index;
  }
  return value;
}

int main() {
  static_assert(sizeof(CoverState) == 32);
  static_assert(BINARY_SENSOR_PAYLOAD_MAX == 512);
  static_assert(ClimateTileWidgets::kMaxSlots == 6);
  static_assert(WEATHER_FORECAST_MAX == 8);
  static_assert(WEATHER_FORECAST_COL_W == EXPECTED_FORECAST_WIDTH);
  SensorTileWidgets sensor;
  assert(!sensor.value_label && !sensor.series && sensor.gauge_min == 0 &&
         sensor.gauge_max == 100);
  SwitchState light;
  assert(light.available && !light.has_state && !light.is_on);
  assert(light.brightness_pct == 100 && light.color_temp_kelvin == 4000 &&
         light.min_color_temp_kelvin == 2000 && light.max_color_temp_kelvin == 6535);
  CoverState cover;
  assert(!cover.valid && !cover.available && !cover.has_position &&
         !cover.has_tilt_position && cover.state[0] == 0);
  BinarySensorState binary;
  assert(binary.value == BinarySensorValue::Missing && !binary.has_available &&
         !binary.has_last_changed && binary.last_changed == 0);
  ClimateState climate;
  assert(!climate.valid && climate.available && climate.target_temperature == 20 &&
         climate.target_humidity == 50 && climate.preset_mode_id == 0xFF);
  ClimateTileWidgets climate_widgets;
  for (const auto& geometry : climate_widgets.slot_geometry) {
    assert(geometry.col == 0 && geometry.row == 0 &&
           geometry.span_w == 1 && geometry.span_h == 1);
  }
  MediaCoverRef artwork;
  assert(!artwork.dsc && !artwork.popup_dsc && artwork.source_url.empty() &&
         artwork.embedded_url_hash == 0 && artwork.requested_url_hash == 0 && artwork.failed_at_ms == 0);
  MediaTileWidgets media;
  assert(!media.cover_ref && !media.has_media_position &&
         !media.has_media_volume && media.dynamic_icon);
  WeatherTileWidgets weather;
  for (const auto& day : weather.forecast) assert(!day.day_label && !day.icon_label);
  TileWidgetCache cache;
  assert(cache.switch_states[0].available &&
         cache.binary_sensor_states[0].value == BinarySensorValue::Missing);

  for (unsigned mask = 0; mask < 256; ++mask) {
    assert(climateHvacModesCsv(mask) == expected_csv(mask,
      {"off", "heat", "cool", "heat_cool", "auto", "dry", "fan_only"}));
    assert(climatePresetModesCsv(mask) == expected_csv(mask,
      {"none", "eco", "away", "boost", "comfort", "home", "sleep", "activity"}));
    assert(climateSwingModesCsv(mask) == expected_csv(mask,
      {"off", "on", "vertical", "horizontal", "both"}));
    assert(climateHorizontalSwingModesCsv(mask) == expected_csv(mask,
      {"off", "on", "left", "center", "right", "swing", "wide"}));
    const unsigned forecast[] = {0, 1, 2, 4, 5, 6};
    assert(weather_forecast_count(mask) == (mask < 6 ? forecast[mask] : 8));
    if (mask >= 8) assert(std::strcmp(climatePresetName(mask), "") == 0);
  }
  for (unsigned mask = 0; mask < 65536; ++mask) {
    assert(climateFanModesCsv(mask) == expected_csv(mask,
      {"auto", "low", "medium", "high", "on", "off", "top", "middle", "focus", "diffuse"}));
  }
}
`;

const buildRoot = path.join(repoRoot, 'build', 'tests');
fs.mkdirSync(buildRoot, {recursive: true});
const tempRoot = fs.mkdtempSync(path.join(buildRoot, 'tile-runtime-headers-'));
try {
  const write = (relative, text) => {
    const filename = path.join(tempRoot, relative);
    fs.mkdirSync(path.dirname(filename), {recursive: true});
    fs.writeFileSync(filename, text);
  };
  write('Arduino.h', `#pragma once
#include <stdint.h>
#include <string>
class String : public std::string {
 public:
  using std::string::string;
  bool startsWith(const char* prefix) const { return rfind(prefix, 0) == 0; }
};
`);
  write('lvgl.h', `#pragma once
struct lv_obj_t;
struct lv_chart_series_t;
struct lv_image_dsc_t;
using lv_coord_t = int;
`);
  write('src/devices/device.h', `#pragma once
#include <stdint.h>
#include "src/devices/device_select.h"
namespace Device {
constexpr uint8_t kGridCols = 4, kGridRows = 4;
constexpr int kGridGap = 8, kGridPad = 8, kGridCellW = 108, kGridCellH = 108;
}
`);
  const flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-DHOMETILES_CI_TARGET', '-I', tempRoot, '-I', repoRoot];
  for (const header of headers) {
    write('standalone.cpp', `#include "${header}"\n`);
    const result = spawnSync(compiler, [...flags,
      '-DDEVICE_WAVESHARE_TOUCH_LCD_8', '-fsyntax-only',
      path.join(tempRoot, 'standalone.cpp')], {encoding: 'utf8'});
    assert.equal(result.status, 0,
      `${header} must include its own dependencies:\n${result.stdout}${result.stderr}`);
  }
  write('test.cpp', source);
  for (const [device, width] of [
    ['DEVICE_WAVESHARE_TOUCH_LCD_8', 150],
    ['DEVICE_GUITION_ESP32_4848S040', 100],
    ['DEVICE_LAYOUT_TEST_1024X600', 125],
  ]) {
    const output = path.join(tempRoot, process.platform === 'win32' ? 'test.exe' : 'test');
    const result = spawnSync(compiler, [...flags, '-D' + device,
      '-DEXPECTED_FORECAST_WIDTH=' + width, path.join(tempRoot, 'test.cpp'),
      '-o', output], {encoding: 'utf8'});
    assert.equal(result.status, 0, `${device}:\n${result.stdout}${result.stderr}`);
    const run = spawnSync(output, [], {encoding: 'utf8'});
    assert.equal(run.status, 0, `${device}:\n${run.stdout}${run.stderr}`);
  }
  console.log('Standalone tile runtime headers, defaults, and inline behavior passed');
} finally {
  assert.ok(path.resolve(tempRoot).startsWith(path.resolve(buildRoot) + path.sep));
  fs.rmSync(tempRoot, {recursive: true, force: true});
}
