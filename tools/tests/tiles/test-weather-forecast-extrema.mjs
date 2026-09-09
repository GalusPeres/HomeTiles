import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const compiler = ['clang++', 'g++'].find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: Weather extrema require a native C++ compiler'); process.exit(0); }
const source = fs.readFileSync(path.join(root, 'src/ui/popups/weather/weather_popup.cpp'), 'utf8');
const fn = cppFunctionDefinitions(source).find(f => f.name === 'update_forecast_graph');
assert(fn);
const start = fn.source.indexOf('  bool has_temp = false;');
const end = fn.source.indexOf('\n  if (has_temp) {', start);
assert(start >= 0 && end > start);
// Execute the actual daily/hourly graph preparation; GUI placement is independent.
const preparation = fn.source.slice(start, end);
const out = path.join(root, 'build/tests/weather-forecast-extrema');
fs.mkdirSync(out, {recursive: true});
const cpp = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(cpp, `
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
constexpr int kCols=7, kHourlyForecastMax=168, kForecastHoursPerDay=24;
constexpr int kForecastTempPointCount=168, LV_CHART_POINT_NONE=-32768;
struct ForecastData { bool has_precipitation=false,has_low=false,has_high=false; float precipitation=0,low=0,high=0; };
struct HourlyForecastData { bool active=false,has_temp=false; int hour_local=0,date_local=0; float temp=0; };
struct Chart { std::array<int,168> values{}; };
struct WeatherPopupContext { ForecastData forecast_data[7]; HourlyForecastData hourly[168]; Chart* forecast_temp_chart; int* forecast_temp_series; };
void lv_chart_set_point_count(Chart*,int) {}
void lv_chart_set_all_value(Chart* chart,int*,int value) { chart->values.fill(value); }
void lv_chart_set_value_by_id(Chart* chart,int*,int index,int value) { chart->values.at(index)=value; }
int scale_forecast_temp(float value) { return std::lround(value*10); }
int find_active_day_index(WeatherPopupContext*,int day) { return day; }
struct Result { std::array<float,7> low{},high{}; std::array<bool,7> has_low{},has_high{}; };
Result prepare(WeatherPopupContext* ctx) {
${preparation}
  Result result;
  for(int i=0;i<7;i++) {
    result.low[i]=day_low_label_temp[i]; result.high[i]=day_high_label_temp[i];
    result.has_low[i]=ctx->forecast_data[i].has_low || day_has_low_anchor[i];
    result.has_high[i]=ctx->forecast_data[i].has_high || day_has_high_anchor[i];
  }
  return result;
}
int main() {
 Chart chart; int series=0; WeatherPopupContext ctx{};
 ctx.forecast_temp_chart=&chart;ctx.forecast_temp_series=&series;
 for(int d=0;d<7;d++) {ctx.forecast_data[d].has_low=true;ctx.forecast_data[d].low=8+d;ctx.forecast_data[d].has_high=true;ctx.forecast_data[d].high=23+d;}
 int temps[]={10,9,9,8,9,10,12,14,16,18,20,21,22,23,22,21,20,19,18,17,16,14,13,12};
 for(int h=0;h<30;h++) ctx.hourly[h]={true,true,h%24,h/24,float(temps[h%24]+h/24)};
 auto result=prepare(&ctx);
 for(int d=0;d<7;d++) {assert(result.low[d]==8+d);assert(result.high[d]==23+d);}
 assert(chart.values[24]==110);assert(chart.values[29]==110);assert(chart.values[30]==LV_CHART_POINT_NONE);
 ctx.forecast_data[1].has_high=false;
 result=prepare(&ctx);assert(result.high[1]==11);assert(result.low[1]==9);
 ctx.forecast_data[1].has_low=false;
 result=prepare(&ctx);assert(result.high[1]==11);assert(result.low[1]==9);
 ctx.forecast_data[0].low=-5;ctx.forecast_data[0].high=0;
 result=prepare(&ctx);assert(result.low[0]==-5);assert(result.high[0]==0);
 for(auto& hour:ctx.hourly) hour.active=false;
 result=prepare(&ctx);assert(!result.has_high[1]);assert(!result.has_low[1]);assert(result.high[6]==29);
 std::cout<<"Daily highs/lows preserved with partial hourly coverage; fallback, missing/zero/negative values and hourly chart retained\\n";
}
`);
let result = spawnSync(compiler, ['-std=c++17', cpp, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log(result.stdout.trim());
