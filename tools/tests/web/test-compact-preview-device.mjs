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
import {decodePng} from '../../lib/png-decode.mjs';

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
#include "src/types/climate/layout.h"
${strip(read('src/tiles/runtime/tile_icon_disc.h'))}
${strip(read('src/tiles/runtime/compact_sensor_layout.h'))}
uint32_t tileDefaultBgColor(){return 0x1A1A1A;}
constexpr int GRID_COLS=Device::kGridCols,GRID_ROWS=Device::kGridRows;
constexpr int GRID_PAD=Device::kGridPad;
#include "src/tiles/config/grid_layout.h"
${read('src/tiles/config/tile_config.h').match(/static const int& GRID_PAD_LEFT =[^]*?GRID_SHOWN_ROWS = [^;]+;/)[0]}
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
 // The corner disc of taller tiles (tile_icon_disc::add_round): at the inset
 // from the card corner, header.disc wide.
 const auto header=tile_icon_disc::corner_header(tile_layout::scale_480(24),tile_layout::scale_480(20),tile_layout::scale_480(-8),
   lv_font_get_glyph_width(FONT_MDI_ICONS,tile_icon_disc::kMdiReferenceGlyph,0),lv_font_get_line_height(FONT_MDI_ICONS));
 // Drawn smaller around its middle (shrink_corner_disc: the bar pill's
 // distance to the card edge); the box stays.
 std::cout<<"DISC "<<tile_icon_disc::inset()+tile_icon_disc::corner_shrink()<<" "<<header.disc-2*tile_icon_disc::corner_shrink()<<"\n";
 // The tile radius slider ends and what the preview gets for them; the card
 // and the disc take the radius with the half-height rule (radius()).
 // (unrounded, like emit_scaled).
 std::cout<<"RADII "<<tile_radius::kMinimum<<" "<<tile_radius::kMaximum<<" "<<tile_radius::kMinimum*preview_cell_h_px()/double(GRID_CELL_H)
  <<" "<<tile_radius::kMaximum*preview_cell_h_px()/double(GRID_CELL_H)<<" "<<tile_radius::kMinimum-tile_layout::scale_480(22)<<"\n";
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
let discsChecked = 0;

// The corner disc of every taller tile type and size as the browser paints
// it (user 2026-10-02: from 1x1 up the card corner cut the disc on V2, S3
// and 4B, the pills were right). Chrome renders each tile's top-left corner
// at 4x; along the disc's middle row and column and along the corner
// diagonal the gap from the card edge to the disc must be the device's:
// the inset straight, and concentric with the card corner diagonally. A
// disc too close to the corner or cut by the card edge fails.
function checkCornerDiscPixels(profile, vars, scale, cellW, cellH, gap, inset, disc, radii) {
  const [minRadius, maxRadius, minPreview, maxPreview, radiusDistance] = radii;
  const types = ['sensor', 'binary_sensor', 'number', 'select', 'datetime', 'cover', 'device', 'energy',
                 'weather', 'media', 'climate', 'switch switch-bar', 'cover switch-bar', 'device switch-bar'];
  const cases = [];
  for (const [radius, previewRadius] of [[minRadius, minPreview], [maxRadius, maxPreview]]) {
    for (const [w, h] of [[1, 1], [1.5, 1], [2, 1], [2, 2], [3, 2]]) cases.push({type: 'sensor', w, h, radius, previewRadius});
    for (const type of types.slice(1)) cases.push({type, w: 2, h: 2, radius, previewRadius});
  }
  const slot = 80, origin = 8, dpr = 4, perRow = 8;
  const tilesHtml = cases.map((c, i) => {
    const width = (c.w * cellW + (Math.ceil(c.w) - 1) * gap) * scale;
    const height = (c.h * cellH + (Math.ceil(c.h) - 1) * gap) * scale;
    // Pure colors: the card blue, the disc red, no glyph or text.
    return `<div style="position:absolute;left:${(i % perRow) * slot}px;top:${Math.floor(i / perRow) * slot}px;width:${slot}px;height:${slot}px;overflow:hidden">` +
      `<div class="tile ${c.type}" style="position:absolute;left:${origin}px;top:${origin}px;width:${width}px;height:${height}px;` +
      `background:rgb(0,0,255);--tile-radius:${c.previewRadius}px;--icon-disc-bg:rgb(255,0,0)">` +
      `<i class="mdi tile-icon" style="color:transparent"></i></div></div>`;
  }).join('');
  const page = path.join(out, profile + '-discs.html');
  // The icon box as the MDI webfont makes it: one em square (the page has no
  // network for the font itself).
  fs.writeFileSync(page, `<!doctype html><html><head><meta charset="utf-8"><style>${css}
:root{${vars}} body{margin:0;background:#000} .tile-icon::before{content:"";display:block;width:1em;height:1em}</style></head><body>${tilesHtml}</body></html>`);
  const shot = path.join(out, profile + '-discs.png');
  fs.rmSync(shot, {force: true});
  const run = spawnSync(browser, ['--headless=new', '--disable-gpu', '--no-first-run', '--hide-scrollbars',
    `--force-device-scale-factor=${dpr}`, `--window-size=${perRow * slot},${Math.ceil(cases.length / perRow) * slot}`,
    '--virtual-time-budget=3000', `--screenshot=${shot}`, pathToFileURL(page).href], {encoding: 'utf8', timeout: 60000});
  assert(fs.existsSync(shot), profile + ': no disc screenshot\n' + run.stderr);
  const {width, pixels} = decodePng(fs.readFileSync(shot));
  const at = (x, y, channel) => pixels[(y * width + x) * 4 + channel] / 255;
  // Where a coverage ramp crosses one half, in device pixels from the scan
  // start (pixel centers; box filtering makes this the exact edge).
  const crossing = samples => {
    for (let k = 1; k < samples.length; ++k) {
      if (samples[k - 1] < 0.5 && samples[k] >= 0.5) return k - 1 + (0.5 - samples[k - 1]) / (samples[k] - samples[k - 1]) + 0.5;
    }
    return NaN;
  };
  const insetPx = inset * scale, discPx = disc * scale;
  cases.forEach((c, i) => {
    const x0 = ((i % perRow) * slot + origin) * dpr, y0 = (Math.floor(i / perRow) * slot + origin) * dpr;
    const span = Math.ceil((insetPx + discPx + 2) * dpr);
    const middle = Math.floor((insetPx + discPx / 2) * dpr);
    const scan = (dx, dy, fx, fy) => {
      const card = [], red = [];
      for (let k = -4; k < span; ++k) {
        const x = x0 + fx * k + dx, y = y0 + fy * k + dy;
        card.push(Math.max(at(x, y, 0), at(x, y, 2)));
        red.push(at(x, y, 0));
      }
      return [crossing(card) - 4, crossing(red) - 4];
    };
    const [rowCard, rowDisc] = scan(0, middle, 1, 0);
    const [colCard, colDisc] = scan(middle, 0, 0, 1);
    const [diagCard, diagDisc] = scan(0, 0, 1, 1);
    // The device geometry at the preview scale: card radius, disc radius
    // (radius() with the half-height rule, at most half the disc).
    const cardRadius = (c.radius - radiusDistance) * scale;
    const discRadius = Math.min(Math.max(0, cardRadius - insetPx), discPx / 2);
    const diagonal = (insetPx + discRadius - discRadius / Math.SQRT2 - (cardRadius - cardRadius / Math.SQRT2)) * Math.SQRT2;
    const label = `${profile} ${c.type} ${c.w}x${c.h} radius ${c.radius}`;
    for (const [name, shown, device, tolerance] of [
      ['left gap', (rowDisc - rowCard) / dpr, insetPx, 0.15],
      ['top gap', (colDisc - colCard) / dpr, insetPx, 0.15],
      // Chrome rasterizes the card's rounded clip and the disc's curve with
      // up to 0.23 px between them on the diagonal; a cut or shifted disc
      // is off by far more (b198: 0.73 instead of 2.17 px on the 4B).
      ['corner gap', (diagDisc - diagCard) * Math.SQRT2 / dpr, diagonal, 0.3]]) {
      report.push(`${label} disc ${name}: ${shown.toFixed(2)} (device ${device.toFixed(2)})`);
      if (!(Math.abs(shown - device) <= tolerance)) failures.push(`${label} disc ${name} ${shown.toFixed(2)} vs device ${device.toFixed(2)}`);
    }
    ++discsChecked;
  });
}
const report = [];
let checked = 0;
// PREVIEW_DEVICE_PROFILES=all checks every device profile (a sweep before a
// release or after a layout change); the suite runs one per layout.
const previewProfiles = process.env.PREVIEW_DEVICE_PROFILES === 'all'
  ? JSON.parse(read('tools/device-profiles.json')).profiles.map(p => p.buildProfile)
  : ['guition_jc8012p4a1_v2', 'guition_esp32_4848s040', 'waveshare_7'];
for (const profile of previewProfiles) {
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
  // A 1x1 Sensor tile's corner disc (the ::after of its icon), in the card's
  // border box.
  const sensor = document.createElement('div');
  sensor.className = 'tile sensor';
  sensor.style.width = ${cellW} * scale + 'px';
  sensor.style.height = ${cellH} * scale + 'px';
  sensor.innerHTML = '<i class="mdi mdi-thermometer tile-icon"></i><div class="tile-title">Upstairs</div>';
  document.getElementById('host').replaceChildren(sensor);
  const sensorBox = sensor.getBoundingClientRect();
  const icon = sensor.querySelector('.tile-icon');
  const iconBox = icon.getBoundingClientRect();
  const after = getComputedStyle(icon, '::after');
  const disc = {left: iconBox.left - sensorBox.left + parseFloat(after.left),
                top: iconBox.top - sensorBox.top + parseFloat(after.top),
                width: parseFloat(after.width), height: parseFloat(after.height)};
  document.getElementById('result').textContent = JSON.stringify({scale, geometry, results, disc});
});
</script></body></html>`;
  const page = path.join(out, profile + '.html');
  fs.writeFileSync(page, html);
  run = spawnSync(browser, ['--headless=new', '--disable-gpu', '--no-first-run', '--allow-file-access-from-files',
    '--virtual-time-budget=5000', '--dump-dom', pathToFileURL(page).href], {encoding: 'utf8', timeout: 60000});
  assert.equal(run.status, 0, run.stderr);
  const match = /<pre id="result">([^<]*)<\/pre>/.exec(run.stdout);
  assert(match && match[1], profile + ': the preview harness did not finish\n' + run.stdout.slice(-2000));
  const {scale, geometry, results, disc} = JSON.parse(match[1].replaceAll('&quot;', '"').replaceAll('&amp;', '&'));
  // The corner disc of a taller tile at the device's inset and size (user
  // 2026-10-02: cut by the card corner on the S3 and 4B).
  const [discInset, discSize] = lines.find(l => l.startsWith('DISC ')).slice(5).split(' ').map(Number);
  for (const [name, shown, device] of [['left', disc.left, discInset], ['top', disc.top, discInset],
                                       ['width', disc.width, discSize], ['height', disc.height, discSize]]) {
    report.push(`${profile} corner disc ${name}: ${(shown - device * scale).toFixed(2)}`);
    if (Math.abs(shown - device * scale) > 0.1) failures.push(`${profile} corner disc ${name} ${shown.toFixed(2)} vs device ${(device * scale).toFixed(2)}`);
  }
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
  checkCornerDiscPixels(profile, vars, scale, cellW, cellH, gap, discInset, discSize,
                        lines.find(l => l.startsWith('RADII ')).slice(6).split(' ').map(Number));
}
fs.writeFileSync(path.join(out, 'report.log'), report.join('\n') + '\n');
assert.deepEqual(failures, [], 'half-height texts away from their device positions');
console.log(`Half-height tiles match the device: ${checked} text lines (baseline, left edge, size) and ${discsChecked} corner discs ` +
            `(gap left, top and in the corner, as painted) on ${previewProfiles.length} device profiles`);
