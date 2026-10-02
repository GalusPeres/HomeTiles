// Half-height tiles (pills) must look like the panel in the Web Admin
// preview (user 2026-10-02: "Upstairs" with value font 28 - "irgendwie stimmt
// die Schrift nicht oder Abstände"). The real compact layout (LVGL on the
// host) and the real preview CSS place the same title and value; every text
// baseline, left edge and font size must match the scaled device.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {radiusPolicyHost, surfaceStyleHost} from '../../lib/surface-style-host.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {findBrowser} from '../../lib/headless-dom.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const strip = s => s.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const fn = (s, n) => { const f = cppFunctionDefinitions(s).find(f => f.name === n); assert(f, n); return f.source; };
const host = await lvglHost(root);
const browser = findBrowser();
if (!host || !browser) {
  console.log('SKIP: the half-height preview comparison needs LVGL, a host compiler and Chrome');
  process.exit(0);
}
const out = path.join(root, 'build/tests/compact-preview-device');
fs.mkdirSync(out, {recursive: true});
fs.writeFileSync(path.join(out, 'Arduino.h'), '#pragma once\n#include <cstdint>\n#include <cstddef>\n');
fs.writeFileSync(path.join(out, 'FS.h'), '#pragma once\nnamespace fs { class FS {}; }\n');

const cpp = String.raw`
#include <lvgl.h>
#include <lvgl_private.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include "src/devices/device_select.h"
#include "src/ui/shared/title_label.h"
extern "C" {LV_FONT_DECLARE(ui_font_12);LV_FONT_DECLARE(ui_font_14);LV_FONT_DECLARE(ui_font_16);LV_FONT_DECLARE(ui_font_20);LV_FONT_DECLARE(ui_font_24);LV_FONT_DECLARE(ui_font_28);LV_FONT_DECLARE(ui_font_32);LV_FONT_DECLARE(ui_font_40);LV_FONT_DECLARE(ui_font_48);LV_FONT_DECLARE(ui_font_56);LV_FONT_DECLARE(ui_font_64);LV_FONT_DECLARE(ui_font_72);LV_FONT_DECLARE(ui_font_80);LV_FONT_DECLARE(ui_font_96);LV_FONT_DECLARE(mdi_icons_32);LV_FONT_DECLARE(mdi_icons_40);LV_FONT_DECLARE(mdi_icons_48);}
#if defined(DEVICE_LAYOUT_480X480)
#define FONT_MDI_ICONS (&mdi_icons_32)
#elif defined(DEVICE_LAYOUT_1024X600)
#define FONT_MDI_ICONS (&mdi_icons_40)
#else
#define FONT_MDI_ICONS (&mdi_icons_48)
#endif
class String:public std::string{public:using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}String(int n):std::string(std::to_string(n)){}String(double v,int d){char b[64];snprintf(b,sizeof b,"%.*f",d,v);assign(b);}};
#include "src/devices/device.h"
#include "src/tiles/config/tile_geometry.h"
constexpr int SCREEN_WIDTH=Device::kScreenWidth,SCREEN_HEIGHT=Device::kScreenHeight;
${radiusPolicyHost(root, 'Device::kGridCellH', 'Device::kGridGap')}
#include "src/core/config/icon_glow.h"
struct TestConfig { int tile_radius = tile_radius::kMinimum; bool tile_borders = true; bool icon_discs = true; uint8_t icon_glow = icon_glow::kDefault; const char* language = "en"; };
struct TestConfigManager { TestConfig config; const TestConfig& getConfig() const { return config; } } configManager;
${surfaceStyleHost(root)}
${strip(read('src/tiles/runtime/tile_renderer_fonts.h'))}
constexpr int TILES_PER_GRID=1,GRID_CELL_W=Device::kGridCellW,GRID_CELL_H=Device::kGridCellH,GRID_GAP=Device::kGridGap;
struct Tile{String title,icon_name;float col=0,row=0,span_w=1,span_h=0.5f;uint8_t sensor_value_font=0;int type=1;};
${read('src/tiles/config/tile_config.h').match(/static constexpr uint8_t SENSOR_VALUE_FONT_MAX = \d+;/)[0]}
${fn(read('src/tiles/runtime/tile_renderer_shared.h'),'apply_fractional_tile_geometry')}
${strip(read('src/tiles/runtime/tile_icon_disc.h'))}
${strip(read('src/tiles/runtime/compact_sensor_layout.h'))}
uint32_t tileDefaultBgColor(){return 0x1A1A1A;}
constexpr int GRID_COLS=Device::kGridCols,GRID_ROWS=Device::kGridRows;
constexpr int GRID_PAD=Device::kGridPad;
${read('src/tiles/config/tile_config.h').match(/static constexpr int GRID_EXTRA_X =[^]*?GRID_PAD_BOTTOM = [^;]+;/)[0]}
#include "src/types/climate/layout.h"
${fn(read('src/fonts/ui_fonts.h'),'ui_font_for_size')}
${strip(read('src/ui/screensaver/screensaver_tile_shadow.h'))}
${strip(read('src/web/server/render/web_admin_styles.cpp').split('void appendAdminStyles(')[0])}
static void line(std::ostream&o,const char*name,lv_obj_t*label,lv_obj_t*card,bool&first){
 if(!label)return;
 lv_area_t a,c;lv_obj_get_coords(label,&a);lv_obj_get_coords(card,&c);
 const lv_font_t*f=lv_obj_get_style_text_font(label,LV_PART_MAIN);
 // LVGL puts the glyph baseline base_line above the bottom of the line.
 o<<(first?"":",")<<"\""<<name<<"\":{\"x\":"<<a.x1-c.x1<<",\"top\":"<<a.y1-c.y1<<",\"baseline\":"<<(a.y1-c.y1)+f->line_height-f->base_line
  <<",\"lineHeight\":"<<f->line_height<<",\"font\":"<<ui_font_size_of(f)<<"}";first=false;
}
int main(){
 lv_init();auto*d=lv_display_create(SCREEN_WIDTH,SCREEN_HEIGHT);(void)d;
 String css;appendPreviewScaleVars(css);
 std::cout<<"CSS "<<css.substr(css.find('{')+1,css.rfind('}')-css.find('{')-1)<<"\n";
 std::cout<<"GRID "<<GRID_CELL_W<<" "<<GRID_CELL_H<<" "<<GRID_GAP<<"\n";
 for(float span_w:{1.f,1.5f,2.f})for(int choice:{0,2,3,5})for(int with_value:{1,0}){
  if(!with_value&&choice)continue;
  Tile tile;tile.span_w=span_w;tile.sensor_value_font=static_cast<uint8_t>(choice);
  // The card as the Sensor renderer makes it: a button without border.
  lv_obj_t*card=lv_button_create(lv_screen_active());lv_obj_set_style_border_width(card,0,0);lv_obj_set_style_shadow_width(card,0,0);
  lv_obj_t*icon=lv_label_create(card);lv_obj_set_style_text_font(icon,FONT_MDI_ICONS,0);lv_label_set_text(icon,"\xF3\xB0\x94\x8F");
  lv_obj_t*title=lv_label_create(card);lv_label_set_text(title,"Upstairs");
  lv_obj_t*value=with_value?lv_label_create(card):nullptr;if(value)lv_label_set_text(value,"23.0 \xC2\xB0""C");
  compact_sensor_layout::apply(card,icon,title,value,tile);
  lv_obj_update_layout(card);
  std::ostringstream o;bool first=true;
  o<<"{\"spanW\":"<<span_w<<",\"choice\":"<<choice<<",\"value\":"<<with_value<<",\"cardW\":"<<lv_obj_get_width(card)<<",\"cardH\":"<<lv_obj_get_height(card)<<",\"lines\":{";
  line(o,"title",title,card,first);line(o,"value",value,card,first);
  o<<"}}";std::cout<<"TILE "<<o.str()<<"\n";
  lv_obj_delete(card);
 }
 lv_deinit();
}
`;
// The point size of a UI font, from the font tables the firmware links.
const sizes = [12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 96];
const sizeOf = `static int ui_font_size_of(const lv_font_t*f){${sizes.map(s => `if(f==&ui_font_${s})return ${s};`).join('')}return 0;}`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp.replace('static void line(', sizeOf + '\nstatic void line('));

const fontUrl = pathToFileURL(path.join(root, 'docs/assets/fonts/inter-4.1-regular.woff2')).href;
const css = readRepoFile('src/web/assets/admin.css').replace(/\r\n/g, '\n')
  .replaceAll("url('/assets/inter-4.1-regular.woff2')", `url('${fontUrl}')`);
const compactValueSize = choice => choice === 2 ? 24 : [3, 4, 5].includes(choice) ? 28 : 20;

const failures = [];
const report = [];
let checked = 0;
for (const profile of ['guition_jc8012p4a1_v2', 'guition_esp32_4848s040', 'waveshare_7']) {
  const define = JSON.parse(read('tools/device-profiles.json')).profiles.find(p => p.buildProfile === profile).define;
  const binary = path.join(out, profile + (process.platform === 'win32' ? '.exe' : ''));
  let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-I', out, '-DHOMETILES_CI_TARGET', `-D${define}`,
    source, host.archive, '-o', binary], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stdout + run.stderr);
  run = spawnSync(binary, [], {encoding: 'utf8'});
  assert.equal(run.status, 0, profile + ': ' + run.stdout + run.stderr);
  const lines = run.stdout.split(/\r?\n/);
  const vars = lines.find(l => l.startsWith('CSS ')).slice(4);
  const [cellW, cellH, gap] = lines.find(l => l.startsWith('GRID ')).slice(5).split(' ').map(Number);
  const tiles = lines.filter(l => l.startsWith('TILE ')).map(l => JSON.parse(l.slice(5)));

  const html = `<!doctype html><html><head><meta charset="utf-8"><style>${css}
:root{${vars}}
body{margin:0;background:#000} #host .tile{position:absolute;left:20px;top:20px}
</style></head><body><div id="host"></div><pre id="result"></pre><script>
${readRepoFile('src/web/admin/tiles/text-baseline.js')}
document.fonts.load('400 20px "HomeTiles Inter"').then(() => {
  // The page's own baseline measurement, as the Web Admin runs it.
  calibratePreviewBaselines();
  const scale = parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--radius-preview-scale'));
  // The grid as the server emits it: exactly the device proportions (user
  // 2026-10-02), so the half-height row is the scaled device row.
  const rootStyle = getComputedStyle(document.documentElement);
  const geometry = ['--preview-cell-w', '--preview-cell-h', '--preview-gap'].map(name => parseFloat(rootStyle.getPropertyValue(name)));
  const results = [];
  for (const device of ${JSON.stringify(tiles)}) {
    const el = document.createElement('div');
    const size = (${compactValueSize.toString()})(device.choice);
    el.className = 'tile sensor sensor-compact sensor-half' + (device.value ? '' : ' compact-title-only') +
      (size === 24 ? ' compact-value-24' : size === 28 ? ' compact-value-28' : '');
    el.innerHTML = '<i class="mdi mdi-thermometer tile-icon"></i><div class="tile-title"><span class="tile-title-lines"><span class="tile-title-line">Upstairs</span></span></div>' +
      (device.value ? '<div class="tile-value">23.0<span class="tile-unit">°C</span></div>' : '');
    el.style.width = device.cardW * scale + 'px';
    el.style.height = device.cardH * scale + 'px';
    document.getElementById('host').replaceChildren(el);
    const card = el.getBoundingClientRect();
    const lines = {};
    for (const [name, selector] of [['title', '.tile-title'], ['value', '.tile-value']]) {
      const node = el.querySelector(selector);
      if (!node) continue;
      const mark = document.createElement('span');
      mark.style.cssText = 'display:inline-block;width:0;height:0;vertical-align:baseline';
      // On the last text line: the title's lines are a block of their own.
      (node.querySelector('.tile-title-line:last-child') || node).appendChild(mark);
      const r = node.getBoundingClientRect();
      lines[name] = {x: r.left - card.left, top: r.top - card.top,
                     baseline: mark.getBoundingClientRect().top - card.top,
                     font: parseFloat(getComputedStyle(node).fontSize)};
    }
    results.push({lines, cardH: card.height});
  }
  document.getElementById('result').textContent = JSON.stringify({scale, geometry, results});
});
</script></body></html>`;
  const page = path.join(out, profile + '.html');
  fs.writeFileSync(page, html);
  run = spawnSync(browser, ['--headless=new', '--disable-gpu', '--no-first-run', '--allow-file-access-from-files',
    '--virtual-time-budget=5000', '--dump-dom', pathToFileURL(page).href], {encoding: 'utf8', timeout: 60000});
  assert.equal(run.status, 0, run.stderr);
  const match = /<pre id="result">([^<]*)<\/pre>/.exec(run.stdout);
  assert(match && match[1], profile + ': the preview harness did not finish\n' + run.stdout.slice(-2000));
  const {scale, geometry, results} = JSON.parse(match[1].replaceAll('&quot;', '"').replaceAll('&amp;', '&'));
  // Cell width and gap follow the device within a hundredth of a pixel.
  for (const [i, device] of [[0, cellW], [1, cellH], [2, gap]].map(([i, v]) => [i, v])) {
    if (Math.abs(geometry[i] - device * scale) > 0.01) failures.push(profile + ' grid ' + ['cell width', 'cell height', 'gap'][i] + ' ' + geometry[i] + ' vs ' + device * scale);
  }
  tiles.forEach((device, index) => {
    const shown = results[index];
    const label = `${profile} ${device.spanW}x0.5 font choice ${device.choice}${device.value ? '' : ' title only'}`;
    for (const [name, line] of Object.entries(device.lines)) {
      const preview = shown.lines[name];
      assert(preview, `${label} ${name} missing in the preview`);
      const dBase = preview.baseline - line.baseline * scale;
      const dX = preview.x - line.x * scale;
      const dFont = preview.font - line.font * scale;
      report.push(`${label} ${name}: baseline ${dBase.toFixed(2)} x ${dX.toFixed(2)} font ${dFont.toFixed(2)}`);
      // The page measures the browser baseline (text-baseline.js) and takes the
      // device line tops, so every line sits on the device baseline.
      if (Math.abs(dBase) > 0.1) failures.push(`${label} ${name} baseline ${preview.baseline.toFixed(2)} vs device ${(line.baseline * scale).toFixed(2)}`);
      if (Math.abs(dX) > 0.1) failures.push(`${label} ${name} x ${preview.x.toFixed(2)} vs device ${(line.x * scale).toFixed(2)}`);
      if (Math.abs(dFont) > 0.25) failures.push(`${label} ${name} font ${preview.font.toFixed(2)} vs device ${(line.font * scale).toFixed(2)}`);
      ++checked;
    }
  });
}
fs.writeFileSync(path.join(out, 'report.log'), report.join('\n') + '\n');
assert.deepEqual(failures, [], 'half-height texts away from their device positions');
console.log(`Half-height tiles match the device: ${checked} text lines (baseline, left edge, size) on V2, 480x480 and 1024x600`);
