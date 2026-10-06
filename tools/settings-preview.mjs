// Renders the firmware's Settings screen (src/ui/tabs/settings: settings_screen,
// settings_parts, settings_style) with real LVGL and the firmware fonts on the
// host, for every panel layout, and lays it beside the approved mockup
// (build/design-mockups/settings/settings-menu.html) with a difference image.
// The mockup's example values stand in for the configuration
// (settings_model.h), so both show the same state.
//
// Usage: node tools/settings-preview.mjs [p8 p7 p1024 p43 p4b s3 p4880]
//        [--page=display|wifi|localization|system] [--open=<row>] [--press=<row>]
//        [--state=off|ap|eth|ethwifi] [--dialog=github|restart|unpair|password|pairing]
//        [--entry=manual|join] [--no-mockup]
// --open shows the option list of a Localization row (1 = time zone), like
// the mockup's dd=<row>; --press shows that row pressed. System: --state=off
// = not paired and the first-password window open (mockup enc=0&pw=win);
// --dialog opens a dialog (mockup dlg=git|restart|encOff|pwOff|code).
// WiFi: --state=ap = the hotspot on, eth / ethwifi = a panel that can use
// Ethernet in Ethernet or WiFi mode (mockup eth=eth|wifi); --entry opens the
// network entry (mockup sheet=manual|pass).
// Output: build/settings-preview/<panel>-<page>.png (firmware render) and
//         build/settings-preview/<panel>-<page>-compare.png (mockup | firmware | difference).
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

import {lvglHost} from './lib/lvgl-host.mjs';
import {decodePng} from './lib/png-decode.mjs';
import {radiusPolicyHost, surfaceStyleHost} from './lib/surface-style-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const strip = source => source.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const out = path.join(root, 'build/settings-preview');
fs.mkdirSync(out, {recursive: true});

// The seven panel layouts (src/devices/*, kProfile), as in the mockup's
// device menu: screen, Home grid, layout class, rotation steps.
const PANELS = {
  p8: {W: 1280, H: 800, cols: 7, rows: 5, cw: 168, ch: 145, gap: 16, pad: 4, cls: 'big', quarter: false, name: 'Guition JC8012P4A1 V2'},
  p7: {W: 1280, H: 720, cols: 7, rows: 4, cw: 168, ch: 166, gap: 16, pad: 4, cls: 'big', quarter: false, name: 'M5Stack Tab5'},
  p1024: {W: 1024, H: 600, cols: 6, rows: 4, cw: 156, ch: 136, gap: 16, pad: 4, cls: 'mid', quarter: false, name: 'Guition JC1060P470C'},
  p43: {W: 800, H: 480, cols: 5, rows: 4, cw: 150, ch: 111, gap: 10, pad: 3, cls: 'small', quarter: false, name: 'Waveshare Touch LCD 4.3'},
  p4b: {W: 720, H: 720, cols: 4, rows: 4, cw: 166, ch: 166, gap: 16, pad: 4, cls: 'big', quarter: true, name: 'Waveshare 4B'},
  s3: {W: 480, H: 480, cols: 4, rows: 4, cw: 111, ch: 111, gap: 10, pad: 3, cls: 'small', quarter: true, name: 'Guition ESP32-S3 4\\"'},
  p4880: {W: 480, H: 800, cols: 4, rows: 6, cw: 111, ch: 124, gap: 10, pad: 3, cls: 'small', quarter: false, name: 'Guition JC4880P443'},
};
const args = process.argv.slice(2);
const noMockup = args.includes('--no-mockup');
const selected = args.filter(a => PANELS[a]);
const option = name => (args.find(a => a.startsWith(`--${name}=`)) || '').split('=')[1];
const page = option('page') || 'display';
const open = option('open');
const press = option('press');
const state = option('state');
const dialog = option('dialog');
const entry = option('entry');
const dialogs = {github: 'git', restart: 'restart', unpair: 'encOff', password: 'pwOff', pairing: 'code'};
if (dialog !== undefined && !(dialog in dialogs)) throw new Error(`unknown dialog ${dialog}`);
const categories = {display: 0, wifi: 1, localization: 2, system: 3};
if (!(page in categories)) throw new Error(`unknown page ${page}`);
const shot = page + (open === undefined ? '' : `-open${open}`) + (press === undefined ? '' : `-press${press}`) +
  (state === undefined ? '' : `-${state}`) + (dialog === undefined ? '' : `-${dialog}`) +
  (entry === undefined ? '' : `-${entry}`);
// The mockup's hash for the same state.
const mockupState = (open === undefined ? '' : `&dd=${open}`) + (state === 'off' ? '&enc=0&pw=win' : '') +
  (state === 'eth' ? '&eth=eth' : state === 'ethwifi' ? '&eth=wifi' : '') +
  (dialog === undefined ? '' : `&dlg=${dialogs[dialog]}`) +
  (entry === undefined ? '' : `&sheet=${entry === 'manual' ? 'manual' : 'pass'}`);
const panels = selected.length ? selected : Object.keys(PANELS);

// The time the mockup shows (Berlin, en-US 12 h), so both lines read the same.
const time = new Intl.DateTimeFormat('en-US', {timeZone: 'Europe/Berlin', hour: 'numeric', minute: '2-digit', hour12: true})
  .format(new Date());

// MDI codepoints of the icons the screen uses, from the firmware's table.
const mdi = read('src/tiles/icons/mdi_icons.cpp');
const icons = ['cog', 'window-close', 'monitor', 'wifi', 'wifi-off', 'access-point', 'translate', 'chip',
  'brightness-6', 'power-sleep', 'screen-rotation', 'timer-outline', 'brightness-4', 'earth', 'clock-outline',
  'calendar-blank-outline', 'keyboard-outline', 'chevron-down', 'chevron-up', 'chevron-right', 'check', 'lan-connect',
  'link-variant', 'link-variant-off', 'form-textbox-password', 'rocket-launch-outline', 'restart', 'github', 'magnify',
  'download', 'wifi-strength-1-lock', 'wifi-strength-2', 'wifi-strength-2-lock', 'wifi-strength-3-lock',
  'wifi-strength-4', 'wifi-strength-lock-outline', 'plus', 'refresh', 'lan', 'ethernet', 'ip-network-outline',
  'pencil-outline', 'eye', 'eye-off', 'lock-outline', 'apple-keyboard-shift', 'apple-keyboard-caps',
  'backspace-outline', 'check-bold'];
const iconTable = icons.map(name => {
  const match = mdi.match(new RegExp(`\\{"${name}", (0x[0-9A-Fa-f]+)\\}`));
  if (!match) throw new Error(`MDI icon ${name} missing`);
  return `{"${name}", ${match[1]}}`;
}).join(', ');

// The English strings table of the firmware.
const i18nSource = read('src/core/i18n/i18n.cpp');
const enStart = i18nSource.indexOf('static const Strings kStringsEn = {');
const enTable = i18nSource.slice(enStart, i18nSource.indexOf('};', enStart) + 2);
// The English time zone names (LocaleProfile kLocaleEn timezone_labels).
const enLocale = i18nSource.slice(i18nSource.indexOf('static const LocaleProfile kLocaleEn = {'));
const zonesStart = enLocale.indexOf('{"UTC+0 - UTC"');
const zones = enLocale.slice(zonesStart, enLocale.indexOf('}', zonesStart) + 1);

function harness(p) {
  const fontPx = {big: 48, mid: 40, small: 32}[p.cls];
  return String.raw`
#include <lvgl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "src/ui/shared/title_label.h"
// Arduino stand-ins.
struct String {
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
};
extern "C" {
LV_FONT_DECLARE(ui_font_12); LV_FONT_DECLARE(ui_font_14); LV_FONT_DECLARE(ui_font_16); LV_FONT_DECLARE(ui_font_20);
LV_FONT_DECLARE(ui_font_24); LV_FONT_DECLARE(ui_font_28); LV_FONT_DECLARE(ui_font_32); LV_FONT_DECLARE(ui_font_40);
LV_FONT_DECLARE(ui_font_48); LV_FONT_DECLARE(ui_font_56); LV_FONT_DECLARE(ui_font_64); LV_FONT_DECLARE(ui_font_72);
LV_FONT_DECLARE(ui_font_80); LV_FONT_DECLARE(ui_font_96); LV_FONT_DECLARE(ui_symbols_20); LV_FONT_DECLARE(ui_symbols_24);
LV_FONT_DECLARE(mdi_icons_${fontPx});
}
#define FONT_MDI_ICONS (&mdi_icons_${fontPx})
String getMdiChar(const char* name) {
  static const struct { const char* name; uint32_t code; } kIcons[] = {${iconTable}};
  for (const auto& icon : kIcons) {
    if (strcmp(icon.name, name) == 0) {
      const uint32_t c = icon.code;
      char utf8[5] = {static_cast<char>(0xF0 | (c >> 18)), static_cast<char>(0x80 | ((c >> 12) & 0x3F)),
                      static_cast<char>(0x80 | ((c >> 6) & 0x3F)), static_cast<char>(0x80 | (c & 0x3F)), 0};
      return String(utf8);
    }
  }
  return String("?");
}
// Board and grid (src/devices kProfile, tile_config.h).
namespace Device {
constexpr int kGridCols = ${p.cols}, kGridRows = ${p.rows}, kGridCellW = ${p.cw}, kGridCellH = ${p.ch}, kGridGap = ${p.gap};
}
static constexpr uint8_t GRID_COLS = ${p.cols};
static constexpr uint8_t GRID_ROWS = ${p.rows};
static constexpr int GRID_GAP = ${p.gap};
static constexpr int GRID_PAD = ${p.pad};
static constexpr int GRID_CELL_W = ${p.cw};
static constexpr int GRID_CELL_H = ${p.ch};
static constexpr int GRID_EXTRA_X = SCREEN_WIDTH - (GRID_COLS * GRID_CELL_W + (GRID_COLS - 1) * GRID_GAP + 2 * GRID_PAD);
static constexpr int GRID_EXTRA_Y = SCREEN_HEIGHT - (GRID_ROWS * GRID_CELL_H + (GRID_ROWS - 1) * GRID_GAP + 2 * GRID_PAD);
static constexpr int GRID_PAD_LEFT = GRID_PAD + GRID_EXTRA_X / 2;
static constexpr int GRID_PAD_RIGHT = GRID_PAD + GRID_EXTRA_X - GRID_EXTRA_X / 2;
static constexpr int GRID_PAD_TOP = GRID_PAD + GRID_EXTRA_Y / 2;
static constexpr int GRID_PAD_BOTTOM = GRID_PAD + GRID_EXTRA_Y - GRID_EXTRA_Y / 2;
${radiusPolicyHost(root, String(p.ch), String(p.gap))}
#include "src/core/config/icon_glow.h"
// Default global options: borders on, circles on, default Circle strength, maximum radius.
struct Config { bool tile_borders = true; bool icon_discs = true; uint8_t icon_glow = icon_glow::kDefault; int tile_radius = tile_radius::kDefault; };
struct Manager { Config cfg; const Config& getConfig() { return cfg; } } configManager;
${surfaceStyleHost(root)}
enum : int { TILE_SENSOR = 1, TILE_BINARY_SENSOR, TILE_ENERGY, TILE_SCENE, TILE_FOLDER, TILE_BACK, TILE_CAMERA, TILE_SETTINGS,
  TILE_NUMBER, TILE_SELECT, TILE_DATETIME, TILE_LOCK, TILE_ALARM, TILE_FAN, TILE_CLOCK, TILE_SWITCH, TILE_COVER, TILE_CLIMATE };
${strip(read('src/tiles/config/tile_geometry.h'))}
${strip(read('src/ui/popups/popup_layout.h'))}
${strip(read('src/core/i18n/i18n.h'))}
namespace i18n {
${enTable}
}
// The card "From icon" gives a color, as tile_icon_source.cpp registers it.
static uint32_t host_from_icon_card(uint32_t icon, bool pressed, uint8_t percent) {
  const uint32_t card = tile_tint::background(0x1A1A1A, icon, percent ? percent : 20);
  if (!pressed) return card;
  uint32_t out = 0;
  for (int shift = 16; shift >= 0; shift -= 8) out |= std::min<uint32_t>(255, ((card >> shift) & 0xFF) + 0x10) << shift;
  return out;
}
${strip(read('src/ui/tabs/settings/settings_style.h'))}
${strip(read('src/ui/tabs/settings/settings_parts.h'))}
${strip(read('src/ui/tabs/settings/settings_model.h'))}
${strip(read('src/ui/tabs/settings/settings_screen.h'))}
${strip(read('src/ui/startup/hometiles_logo.h'))}
${strip(read('src/ui/startup/hometiles_logo.cpp'))}
${strip(read('src/ui/tabs/settings/settings_parts.cpp'))}
${strip(read('src/ui/tabs/settings/settings_keyboard.h'))}
${strip(read('src/ui/tabs/settings/settings_keyboard.cpp'))}
${strip(read('src/ui/tabs/settings/settings_screen.cpp'))}
// The mockup's example state.
namespace settings_model {
const i18n::Strings& text() { return i18n::kStringsEn; }
uint32_t card_color() { return 0x1A1A1A; }
DisplayValues display_values() { return {80, 1, 100, 4, 3, 20, 1, 100}; }
int saved_brightness() { return 80; }
int saved_sleep_index() { return 4; }
int sleep_steps() { return 8; }
unsigned sleep_step_seconds(int index) {
  static const unsigned kSteps[] = {5, 15, 30, 60, 300, 900, 1800, 3600};
  return kSteps[index < 0 ? 0 : index > 7 ? 7 : index];
}
bool quarter_turns() { return ${p.quarter ? 'true' : 'false'}; }
uint8_t rotation_index() { return 0; }
void brightness_changed(int, bool) {}
void sleep_changed(int, bool) {}
void saver_changed(int, bool) {}
void saver_brightness_changed(int, bool) {}
void rotation_selected(uint8_t) {}
bool access_point_on() { return false; }
bool network_connected() { return true; }
void network_name(char* buf, size_t len) { snprintf(buf, len, "HomeNet"); }
const char* language_name() { return "English"; }
bool time_text(char* buf, size_t len) { snprintf(buf, len, "%s", "${time}"); return true; }
bool update_available() { return false; }
// Localization: English, Berlin, the rest Auto (mockup st.loc).
uint8_t locale_option_count(LocaleList list) {
  static const uint8_t kCounts[] = {4, 27, 3, 4, 3};
  return kCounts[static_cast<int>(list)];
}
const char* locale_option(LocaleList list, uint8_t index) {
  static const char* const kLanguages[] = {"English", "Deutsch", "Fran\xC3\xA7" "ais", "Polski"};
  static const char* const kZones[] = ${zones};
  static const char* const kTimes[] = {"Auto (language)", "24-hour", "12-hour"};
  static const char* const kDates[] = {"Auto (language)", "DD.MM.YYYY", "MM/DD/YYYY", "YYYY/MM/DD"};
  static const char* const kKeyboards[] = {"Auto (language)", "Deutsch (QWERTZ)", "English (QWERTY)"};
  switch (list) {
    case LocaleList::Language: return kLanguages[index];
    case LocaleList::TimeZone: return kZones[index];
    case LocaleList::TimeFormat: return kTimes[index];
    case LocaleList::DateFormat: return kDates[index];
    case LocaleList::Keyboard: return kKeyboards[index];
  }
  return "";
}
uint8_t locale_selected(LocaleList list) { return list == LocaleList::TimeZone ? 2 : 0; }
void locale_selected_changed(LocaleList, uint8_t) {}
// System: paired, password on (mockup st.sec), or --state=off; --dialog=pairing
// shows the number.
SystemValues system_values() {
  const bool off = ${state === 'off' ? 'true' : 'false'};
  const PairState pairing = ${dialog === 'pairing' ? 'PairState::Compare' : "off ? PairState::NotPaired : PairState::Paired"};
  return {UpdateState::Idle, 0, !off, pairing, 119, !off, off, 119};
}
const char* latest_version() { return ""; }
const char* device_name() { return "${p.name}"; }
bool panel_address(char* buf, size_t len) { snprintf(buf, len, "${state === 'ap' ? '192.168.4.1' : '192.168.1.50'}"); return true; }
bool pairing_number(char* buf, size_t len) { snprintf(buf, len, "465 848"); return true; }
const char* pairing_note() { return nullptr; }
const char* repo_url() { return "https://github.com/GalusPeres/HomeTiles"; }
void update_pressed() {}
void restart() {}
void pair() {}
void confirm_pairing() {}
void cancel_pairing() {}
void unpair() {}
void allow_password() {}
void remove_password() {}
// WiFi: HomeNet connected, the mockup's other networks (st.nets).
WifiValues wifi_values() {
  WifiValues v = {};
  v.ethernet_panel = ${state === 'eth' || state === 'ethwifi' ? 'true' : 'false'};
  v.ethernet_selected = v.ethernet_active = ${state === 'eth' ? 'true' : 'false'};
  v.access_point = ${state === 'ap' ? 'true' : 'false'};
  v.connected = !v.access_point;
  v.bars = 4;
  v.ip_mode_offered = v.ethernet_active;
  return v;
}
bool ethernet_panel() { return ${state === 'eth' || state === 'ethwifi' ? 'true' : 'false'}; }
bool ethernet_active() { return ${state === 'eth' ? 'true' : 'false'}; }
uint8_t keyboard_layout() { return 0; }
static const WifiNetwork kNetworks[] = {{"HomeNet-Guest", 3, true}, {"FRITZ!Box 7590 XY", 2, true},
                                        {"Garden-Cam", 2, false}, {"DIRECT-42-HP OfficeJet", 1, true}};
uint8_t wifi_network_count() { return 4; }
const WifiNetwork& wifi_network(uint8_t index) { return kNetworks[index < 4 ? index : 0]; }
const char* wifi_saved_password(const char*) { return ""; }
void hotspot_details(char* ssid, size_t ssid_len, char* password, size_t password_len) {
  snprintf(ssid, ssid_len, "HomeTiles-3F2A");
  snprintf(password, password_len, "hometiles");
}
bool static_address(char*, size_t) { return false; }
void wifi_scan() {}
void wifi_connect(const char*, const char*) {}
void wifi_disconnect() {}
void hotspot_selected(bool) {}
void network_mode_selected(bool) {}
void ip_mode_selected(bool) {}
void wifi_connect_done() {}
const char* firmware_version() { return "v0.8.0"; }
void close_settings() {}
void open_category_popup(uint8_t, lv_event_t*) {}
}
int main(int argc, char** argv) {
  tone_color::g_from_icon_card = &host_from_icon_card;
  lv_init();
  lv_display_t* display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  std::vector<uint32_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT), band(SCREEN_WIDTH * 32);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, band.data(), nullptr, band.size() * 4, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_user_data(display, &pixels);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t* area, uint8_t* data) {
    auto& target = *static_cast<std::vector<uint32_t>*>(lv_display_get_user_data(d));
    auto* source = reinterpret_cast<uint32_t*>(data);
    for (int y = area->y1; y <= area->y2; ++y)
      for (int x = area->x1; x <= area->x2; ++x) target[y * SCREEN_WIDTH + x] = *source++;
    lv_display_flush_ready(d);
  });
  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  // The Settings panel as UIManager::createTabPanel and build_settings_tab set it up.
  lv_obj_t* panel = lv_obj_create(screen);
  lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_opa(panel, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_left(panel, GRID_PAD_LEFT, 0);
  lv_obj_set_style_pad_right(panel, GRID_PAD_RIGHT, 0);
  lv_obj_set_style_pad_top(panel, GRID_PAD_TOP, 0);
  lv_obj_set_style_pad_bottom(panel, GRID_PAD_BOTTOM, 0);
  settings_screen::build(panel);
  settings_screen::prepare_show();
  settings_screen::select_category(static_cast<settings_screen::Category>(${categories[page]}));
  ${open === undefined ? '' : `settings_screen::open_locale_list(${Number(open)});`}
  ${press === undefined ? '' : `lv_obj_add_state(settings_screen::g_locale_rows[${Number(press)}].row, LV_STATE_PRESSED);`}
  settings_screen::system_changed();
  ${entry === undefined ? '' : `settings_screen::open_entry(${entry === 'manual'}, "FRITZ!Box 7590 XY");`}
  ${dialog === undefined || dialog === 'pairing' ? '' : `settings_screen::open_dialog(settings_screen::Dialog::${{github: 'GitHub', restart: 'Restart', unpair: 'Unpair', password: 'RemovePassword'}[dialog]});`}
  ui_surface_style::process_pending_updates();
  lv_obj_update_layout(screen);
  lv_obj_invalidate(screen);
  lv_tick_inc(50);
  lv_refr_now(display);
  std::ofstream file(argv[1], std::ios::binary);
  file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size() * 4);
  return 0;
}
`;
}

// ---------- PNG ----------
function encodePng(width, height, rgb) {
  const raw = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (width * 3 + 1)] = 0;
    rgb.copy(raw, y * (width * 3 + 1) + 1, y * width * 3, (y + 1) * width * 3);
  }
  const chunk = (type, data) => {
    const length = Buffer.alloc(4); length.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(zlib.crc32(body) >>> 0);
    return Buffer.concat([length, body, crc]);
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0); header.writeUInt32BE(height, 4);
  header[8] = 8; header[9] = 2; header[10] = 0; header[11] = 0; header[12] = 0;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', header),
    chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

// The mockup state at the panel's resolution, 1:1 (headless Edge, the
// screen cropped out of the device frame; see mockup fit()).
function mockup(key, p) {
  const edge = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
  const html = path.join(root, 'build/design-mockups/settings/settings-menu.html');
  if (!fs.existsSync(edge) || !fs.existsSync(html)) return null;
  const bezel = {big: 14, mid: 12, small: 10}[p.cls];
  const winW = p.W + 2 * bezel + 34, winH = p.H + 2 * bezel + 18;
  const file = path.join(out, `${key}-mockup-full.png`);
  const url = 'file:///' + html.replace(/\\/g, '/') +
    `#dev=${key}&view=settings&cat=${page}&ico=0&bare=1&shot=1${mockupState}`;
  spawnSync(edge, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--allow-file-access-from-files',
    `--window-size=${winW},${winH}`, '--virtual-time-budget=6000', `--screenshot=${file}`, url], {stdio: 'ignore'});
  if (!fs.existsSync(file)) return null;
  const image = decodePng(fs.readFileSync(file));
  fs.unlinkSync(file);
  const x0 = Math.floor((winW - (p.W + 2 * bezel)) / 2) + bezel, y0 = bezel;
  const rgb = Buffer.alloc(p.W * p.H * 3);
  const channels = 4;
  for (let y = 0; y < p.H; y++) {
    for (let x = 0; x < p.W; x++) {
      const s = ((y + y0) * image.width + x + x0) * channels, d = (y * p.W + x) * 3;
      rgb[d] = image.pixels[s]; rgb[d + 1] = image.pixels[s + 1]; rgb[d + 2] = image.pixels[s + 2];
    }
  }
  return rgb;
}

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the Settings preview needs LVGL 9.6 and a host compiler');
  process.exit(0);
}
for (const key of panels) {
  const p = PANELS[key];
  const source = path.join(out, `${key}.cpp`), binary = path.join(out, `${key}.exe`), raw = path.join(out, `${key}.raw`);
  fs.writeFileSync(source, harness(p));
  const defines = [`-DSCREEN_WIDTH=${p.W}`, `-DSCREEN_HEIGHT=${p.H}`,
    ...(p.cls === 'mid' ? ['-DDEVICE_LAYOUT_1024X600'] : p.cls === 'small' ? ['-DDEVICE_LAYOUT_480X480'] : [])];
  const build = spawnSync(host.cxx, [...host.flags, '-std=c++17', ...defines, source, host.archive, '-o', binary], {encoding: 'utf8'});
  if (build.status !== 0) {
    console.error(build.stdout + build.stderr);
    process.exit(1);
  }
  const run = spawnSync(binary, [raw], {encoding: 'utf8'});
  if (run.status !== 0) {
    console.error(`${key}: render failed\n${run.stdout}${run.stderr}`);
    process.exit(1);
  }
  const xrgb = fs.readFileSync(raw);
  const firmware = Buffer.alloc(p.W * p.H * 3);
  for (let i = 0; i < p.W * p.H; i++) {
    firmware[i * 3] = xrgb[i * 4 + 2]; firmware[i * 3 + 1] = xrgb[i * 4 + 1]; firmware[i * 3 + 2] = xrgb[i * 4];
  }
  fs.writeFileSync(path.join(out, `${key}-${shot}.png`), encodePng(p.W, p.H, firmware));
  const reference = noMockup ? null : mockup(key, p);
  if (!reference) {
    console.log(`${key}: ${path.join(out, `${key}-${shot}.png`)}`);
    continue;
  }
  // mockup | firmware | difference (white = differs, grey = the firmware image faintly).
  const gap = 12, width = p.W * 3 + gap * 2;
  const sheet = Buffer.alloc(width * p.H * 3, 40);
  let differing = 0;
  for (let y = 0; y < p.H; y++) {
    for (let x = 0; x < p.W; x++) {
      const s = (y * p.W + x) * 3;
      const delta = Math.max(...[0, 1, 2].map(c => Math.abs(reference[s + c] - firmware[s + c])));
      if (delta > 48) differing++;
      for (let c = 0; c < 3; c++) {
        sheet[(y * width + x) * 3 + c] = reference[s + c];
        sheet[(y * width + x + p.W + gap) * 3 + c] = firmware[s + c];
        sheet[(y * width + x + 2 * (p.W + gap)) * 3 + c] = delta > 48 ? 255 : Math.round(firmware[s + c] * 0.25);
      }
    }
  }
  const file = path.join(out, `${key}-${shot}-compare.png`);
  fs.writeFileSync(file, encodePng(width, p.H, sheet));
  console.log(`${key}: ${(100 * differing / (p.W * p.H)).toFixed(2)} % of the pixels differ clearly -> ${file}`);
}
