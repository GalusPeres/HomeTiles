// The Web Admin media preview must look like the panel (user 2026-10-02:
// "dann ist bei media es anders"). The real media renderer and its text
// layout (LVGL on the host) and the real preview script (headless Chrome) draw
// the same tile with and without subtitle and artwork; title, subtitle,
// artwork and the three controls must sit at the device positions.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {radiusPolicyHost, surfaceStyleHost} from '../../lib/surface-style-host.mjs';
import {extractFunction, inlineScriptSafe, readRepoFile} from '../../lib/admin-source.mjs';
import {findBrowser} from '../../lib/headless-dom.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const strip = s => s.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const fn = (s, n) => { const f = cppFunctionDefinitions(s).find(f => f.name === n); assert(f, n); return f.source; };
const host = await lvglHost(root);
const browser = findBrowser();
if (!host || !browser) {
  console.log('SKIP: the media preview comparison needs LVGL, a host compiler and Chrome');
  process.exit(0);
}
const out = path.join(root, 'build/tests/media-preview-device');
fs.mkdirSync(out, {recursive: true});
fs.writeFileSync(path.join(out, 'Arduino.h'), '#pragma once\n#include <cstdint>\n#include <cstddef>\n');
fs.writeFileSync(path.join(out, 'FS.h'), '#pragma once\nnamespace fs { class FS {}; }\n');

const renderer = read('src/types/media/renderer.cpp');
const control = fn(renderer, 'create_media_control_button');
const TITLE = 'A long song title that scrolls within the space beside its artwork';
const SUBTITLE = 'Artist with a long name';
const cpp = String.raw`
#include <lvgl.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include "src/devices/device_select.h"
#include "src/ui/shared/title_label.h"
extern "C" {LV_FONT_DECLARE(ui_font_12);LV_FONT_DECLARE(ui_font_14);LV_FONT_DECLARE(ui_font_16);LV_FONT_DECLARE(ui_font_20);LV_FONT_DECLARE(ui_font_24);LV_FONT_DECLARE(ui_font_28);LV_FONT_DECLARE(ui_font_32);LV_FONT_DECLARE(ui_font_40);LV_FONT_DECLARE(mdi_icons_32);LV_FONT_DECLARE(mdi_icons_40);LV_FONT_DECLARE(mdi_icons_48);}
#if defined(DEVICE_LAYOUT_480X480)
#define FONT_MDI_ICONS (&mdi_icons_32)
#elif defined(DEVICE_LAYOUT_1024X600)
#define FONT_MDI_ICONS (&mdi_icons_40)
#else
#define FONT_MDI_ICONS (&mdi_icons_48)
#endif
class String:public std::string{public:using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}
 void trim(){auto a=find_first_not_of(" \r\n");if(a==npos){clear();return;}*this=substr(a,find_last_not_of(" \r\n")-a+1);}};
#include "src/devices/device.h"
#include "src/tiles/config/tile_geometry.h"
${strip(read('src/tiles/runtime/tile_renderer_fonts.h'))}
${strip(read('src/types/media/tile_layout.h'))}
${read('src/types/media/widgets.h').replace(/^#.*$/gm, '')}
${strip(read('src/types/media/cover_geometry.h'))}
${strip(read('src/types/media/content_layout.cpp'))}
${renderer.slice(renderer.indexOf('static constexpr lv_coord_t kMediaControlButtonSize'), renderer.indexOf('struct MediaEventData'))}
enum class GridType{TAB0,SCREENSAVER};constexpr int TILES_PER_GRID=1,GRID_CELL_W=Device::kGridCellW,GRID_CELL_H=Device::kGridCellH,GRID_GAP=Device::kGridGap;
struct Tile{String title,icon_name,sensor_entity;float col=0,row=0,span_w=2,span_h=2;};
MediaTileWidgets widgets[1];
MediaTileWidgets* tile_renderer_get_media_widgets(GridType){return widgets;}
struct Logger{void println(const char*){}}Serial;
struct Bridge{String findEntityIcon(const String&){return "speaker";}String findSensorName(const String&){return "Player";}String findSensorInitialValue(const String&){return "";}}haBridgeConfig;
${radiusPolicyHost(root, 'Device::kGridCellH', 'Device::kGridGap')}
#include "src/core/config/icon_glow.h"
struct Config{bool tile_borders=true;bool icon_discs=true;uint8_t icon_glow=icon_glow::kDefault;int tile_radius=tile_radius::kMinimum;const char*language="en";};struct Manager{Config cfg;const Config&getConfig(){return cfg;}}configManager;
${surfaceStyleHost(root)}
${strip(read('src/tiles/runtime/tile_icon_disc.h'))}
namespace i18n{struct Strings{const char*media_state_playing="Playing";const char*media_state_paused="Paused";const char*media_state_idle="Idle";const char*media_state_standby="Standby";const char*media_state_off="Off";const char*media_no_playback="No playback";};
 inline const Strings&strings(const char*){static Strings s;return s;}inline String entity_state_label(const char*,const char*s){return std::string(s)=="unavailable"?"Unavailable":"Unknown";}}
uint32_t tileBgColorOrDefault(const Tile&,uint32_t d){return d;}uint32_t tileDefaultBgColor(){return 0x1A1A1A;}
uint32_t brighten_rgb_color(uint32_t c,uint32_t){return c;}
String normalizeMdiIconName(const String&s){return s;}
String getMdiChar(const String&s){return s=="play"?"\xF3\xB0\x90\x8A":s=="skip-next"?"\xF3\xB0\x92\xAD":s=="skip-previous"?"\xF3\xB0\x92\xAE":"\xF3\xB0\x93\x83";}
String media_friendly_name_from_entity(const String&){return "Player";}
void set_label_style(lv_obj_t*l,lv_color_t c,const lv_font_t*f){lv_obj_set_style_text_color(l,c,0);lv_obj_set_style_text_font(l,f,0);}
void enable_event_bubble(lv_obj_t*){}void apply_media_text_scroll_style(lv_obj_t*){}
${fn(read('src/tiles/runtime/tile_renderer_shared.h'), 'disable_pressed_button_animation')}
void place_tile_card(lv_obj_t*c,int,int,const Tile&t){lv_obj_set_size(c,tile_geometry::extent(t.col,t.span_w,GRID_CELL_W,GRID_GAP),tile_geometry::extent(t.row,t.span_h,GRID_CELL_H,GRID_GAP));lv_obj_set_pos(c,0,0);}
void cover_ref_delete_cb(lv_event_t*e){delete static_cast<MediaCoverRef*>(lv_event_get_user_data(e));}
${renderer.match(/struct MediaPopupEventData \{[\s\S]*?\n};/)[0]}
void show_media_popup_event_cb(lv_event_t*){}
void media_popup_event_data_delete_cb(lv_event_t*e){delete static_cast<MediaPopupEventData*>(lv_event_get_user_data(e));}
void update_media_tile_state(GridType,uint8_t,const char*){}
namespace tile_icon_source{void refresh_controls(lv_obj_t*){}}
${control.slice(0, control.indexOf('  MediaEventData* data'))}return label;}
${fn(renderer, 'render_media_tile')}
${strip(read('src/types/media/web_scripts.cpp'))}
static void box(std::ostream&o,const char*name,lv_obj_t*obj,lv_obj_t*card,bool&first){
 if(!obj||lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))return;
 lv_area_t a,c;lv_obj_get_coords(obj,&a);lv_obj_get_coords(card,&c);
 o<<(first?"":",")<<"\""<<name<<"\":{\"x\":"<<a.x1-c.x1<<",\"y\":"<<a.y1-c.y1<<",\"w\":"<<lv_area_get_width(&a)<<",\"h\":"<<lv_area_get_height(&a)<<"}";first=false;
}
int main(){
 lv_init();lv_display_create(Device::kScreenWidth,Device::kScreenHeight);
 String script;append_media_scripts(script);
 std::string js=script;js=js.substr(js.find("<script>")+8);js=js.substr(0,js.rfind("</script>"));
 for(char&c:js)if(c=='\n')c=' ';
 std::cout<<"SCRIPT "<<js<<"\n";
 std::cout<<"GRID "<<static_cast<int>(Device::kGridCols)<<" "<<static_cast<int>(Device::kGridRows)<<"\n";
 for(float span_w:{2.f,2.5f,3.f})for(float span_h:{2.f,3.f})for(bool subtitle:{false,true})for(bool cover:{false,true}){
  if(span_w>Device::kGridCols||span_h>Device::kGridRows)continue;
  Tile t;t.span_w=span_w;t.span_h=span_h;t.title="Player";t.sensor_entity="media_player.test";
  auto*card=render_media_tile(lv_screen_active(),0,0,t,0,GridType::TAB0);auto&m=widgets[0];
  lv_label_set_text(m.media_title_label,"${TITLE}");lv_label_set_text(m.media_subtitle_label,"${SUBTITLE}");
  if(subtitle)lv_obj_remove_flag(m.media_subtitle_label,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(m.media_subtitle_label,LV_OBJ_FLAG_HIDDEN);
  if(cover)lv_obj_remove_flag(m.cover_clip,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(m.cover_clip,LV_OBJ_FLAG_HIDDEN);
  set_media_cover_text_layout(m,cover);lv_obj_update_layout(card);
  std::ostringstream o;bool first=true;
  o<<"{\"spanW\":"<<span_w<<",\"spanH\":"<<span_h<<",\"subtitle\":"<<subtitle<<",\"cover\":"<<cover
   <<",\"cardW\":"<<lv_obj_get_width(card)<<",\"cardH\":"<<lv_obj_get_height(card)<<",\"boxes\":{";
  box(o,"title",m.media_title_label,card,first);box(o,"subtitle",m.media_subtitle_label,card,first);box(o,"cover",m.cover_clip,card,first);
  const char* names[3]={"previous","play","next"};lv_obj_t* labels[3]={m.previous_label,m.play_pause_label,m.next_label};
  for(int i=0;i<3;++i){box(o,names[i],lv_obj_get_parent(labels[i]),card,first);box(o,(std::string(names[i])+"Icon").c_str(),labels[i],card,first);}
  o<<"}}";std::cout<<"TILE "<<o.str()<<"\n";
  lv_obj_delete(card);widgets[0]={};
 }
 lv_deinit();
}
`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);

const preview = [
  read('src/web/admin/tiles/text-baseline.js'),
  read('src/types/media/admin.js'),
  ...['escapeHtml'].map(name => extractFunction(name, read('src/web/admin/tiles/state.js'))),
  `function tileTitleHtml(text) { return escapeHtml(text); }`,
].join('\n');
const fontUrl = pathToFileURL(path.join(root, 'docs/assets/fonts/inter-4.1-regular.woff2')).href;
const css = readRepoFile('src/web/assets/admin.css').replace(/\r\n/g, '\n')
  .replaceAll("url('/assets/inter-4.1-regular.woff2')", `url('${fontUrl}')`);
const pixel = 'data:image/gif;base64,R0lGODlhAQABAIAAAP///wAAACH5BAEAAAAALAAAAAABAAEAAAICRAEAOw==';

let checked = 0;
const failures = [];
for (const profile of ['guition_jc8012p4a1_v2', 'guition_esp32_4848s040', 'waveshare_s3_touch_lcd_4b', 'waveshare_7']) {
  const define = JSON.parse(read('tools/device-profiles.json')).profiles.find(p => p.buildProfile === profile).define;
  const binary = path.join(out, profile + (process.platform === 'win32' ? '.exe' : ''));
  let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-I', out, '-DHOMETILES_CI_TARGET', `-D${define}`,
    source, host.archive, '-o', binary], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stdout + run.stderr);
  run = spawnSync(binary, [], {encoding: 'utf8'});
  assert.equal(run.status, 0, profile + ': ' + run.stdout + run.stderr);
  const lines = run.stdout.split(/\r?\n/);
  const script = lines.find(line => line.startsWith('SCRIPT ')).slice(7);
  const tiles = lines.filter(line => line.startsWith('TILE ')).map(line => JSON.parse(line.slice(5)));
  const [cols, rows] = lines.find(line => line.startsWith('GRID ')).slice(5).split(' ').map(Number);
  // The preview scale of this panel (web_admin_styles.cpp preview_cell_h_px).
  const target = cols === 7 && rows === 4 ? 390 : 430;
  const cellPreview = Math.max(40, Math.trunc((target - 24 - 10 * (rows - 1)) / rows));
  const html = `<!doctype html><html><head><meta charset="utf-8"><style>${css}
body{margin:0;background:#000} .tile.media{position:absolute;left:20px;top:20px}
</style></head><body><div id="host"></div><pre id="result"></pre><script>
${inlineScriptSafe(script)}
${inlineScriptSafe(preview)}
const layout = MEDIA_TILE_LAYOUT;
const scale = ${cellPreview} / layout.cellH;
document.documentElement.style.setProperty('--radius-preview-scale', String(scale));
document.documentElement.style.setProperty('--icon-size', Math.max(6, Math.round(layout.iconPx * scale)) + 'px');
// The preview grid at exactly the device proportions.
document.documentElement.style.setProperty('--preview-cell-w', layout.cellW * scale + 'px');
document.documentElement.style.setProperty('--preview-cell-h', layout.cellH * scale + 'px');
document.documentElement.style.setProperty('--preview-gap', layout.gap * scale + 'px');
document.fonts.load('400 20px "HomeTiles Inter"').then(() => {
  const results = [];
  for (const device of ${JSON.stringify(tiles)}) {
    const el = document.createElement('div');
    el.className = 'tile media';
    // Filled before the grid places it (a tile at a half position), then
    // given its grid size.
    el.style.width = '120px';
    el.style.height = '60px';
    el.style.background = '#1A1A1A';
    el.innerHTML = '<i class="mdi mdi-speaker tile-icon"></i><div class="tile-title">Player</div>';
    document.getElementById('host').replaceChildren(el);
    const state = {state: 'paused', title: ${JSON.stringify(TITLE)}, artist: device.subtitle ? ${JSON.stringify(SUBTITLE)} : '',
                   album: '', app: '', source: '', channel: '', cover: device.cover ? ${JSON.stringify(pixel)} : ''};
    applyMediaPreview(el, state, {sensor_entity: 'media_player.test', span_w: device.spanW, span_h: device.spanH},
                      'speaker', 'Player');
    el.style.width = device.cardW * scale + 'px';
    el.style.height = device.cardH * scale + 'px';
    // The artwork's place does not depend on its pixels: show it as loaded.
    const image = el.querySelector('.media-preview-cover img');
    if (image) mediaPreviewCoverLoaded(image);
    const card = el.getBoundingClientRect();
    const boxes = {};
    const rect = (name, node) => {
      if (!node || node.hidden) return;
      const r = node.getBoundingClientRect();
      boxes[name] = {x: r.left - card.left, y: r.top - card.top, w: r.width, h: r.height};
    };
    rect('title', el.querySelector('.media-preview-title'));
    rect('subtitle', el.querySelector('.media-preview-subtitle'));
    rect('cover', el.querySelector('.media-preview-cover'));
    el.querySelectorAll('.media-preview-control').forEach((node, i) => {
      const name = ['previous', 'play', 'next'][i];
      rect(name, node);
      // The em box of the MDI glyph, as tall as its font size.
      const icon = node.querySelector('.mdi').getBoundingClientRect();
      const size = parseFloat(getComputedStyle(node.querySelector('.mdi')).fontSize);
      boxes[name + 'Icon'] = {x: icon.left - card.left + (icon.width - size) / 2, y: icon.top - card.top, w: size, h: size};
    });
    results.push(boxes);
  }
  document.getElementById('result').textContent = JSON.stringify({scale, results});
});
</script></body></html>`;
  const page = path.join(out, profile + '.html');
  fs.writeFileSync(page, html);
  run = spawnSync(browser, ['--headless=new', '--disable-gpu', '--no-first-run', '--allow-file-access-from-files',
    '--virtual-time-budget=5000', '--dump-dom', pathToFileURL(page).href], {encoding: 'utf8', timeout: 60000});
  assert.equal(run.status, 0, run.stderr);
  const match = /<pre id="result">([^<]*)<\/pre>/.exec(run.stdout);
  assert(match && match[1], profile + ': the preview harness did not finish\n' + run.stdout.slice(-2000));
  const {scale, results} = JSON.parse(match[1].replaceAll('&quot;', '"').replaceAll('&amp;', '&'));
  const report = [];
  tiles.forEach((device, index) => {
    const shownTile = results[index];
    const label = `${profile} ${device.spanW}x${device.spanH}${device.subtitle ? ' subtitle' : ''}${device.cover ? ' cover' : ''}`;
    assert.deepEqual(Object.keys(shownTile).sort(), Object.keys(device.boxes).sort(), `${label} parts`);
    for (const [name, deviceBox] of Object.entries(device.boxes)) {
      const shown = shownTile[name];
      // Icon labels: the device label is the glyph advance by the line height,
      // its em box starts below the label top; compare the horizontal center.
      const iconPart = name.endsWith('Icon');
      const expectX = iconPart ? (deviceBox.x + deviceBox.w / 2) * scale : deviceBox.x * scale;
      const shownX = iconPart ? shown.x + shown.w / 2 : shown.x;
      const dx = shownX - expectX;
      const dy = iconPart ? 0 : shown.y - deviceBox.y * scale;
      const dw = /title|subtitle|Icon/.test(name) ? 0 : shown.w - deviceBox.w * scale;
      report.push(`${label} ${name}: dx=${dx.toFixed(1)} dy=${dy.toFixed(1)} dw=${dw.toFixed(1)}`);
      if (Math.abs(dx) > 1 || Math.abs(dy) > 1 || Math.abs(dw) > 1) {
        failures.push(`${label} ${name}: dx=${dx.toFixed(2)} dy=${dy.toFixed(2)} dw=${dw.toFixed(2)}`);
      }
      ++checked;
    }
  });
  fs.writeFileSync(path.join(out, profile + '.log'), report.join('\n') + '\n');
}
assert.deepEqual(failures, [], 'media preview parts away from their device positions');
console.log(`Media preview matches the device: ${checked} parts within 1 preview px on V2, 480x480, 4B and 1024x600`);
