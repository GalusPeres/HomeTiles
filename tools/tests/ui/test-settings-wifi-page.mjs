// The Settings WiFi page of the approved mockup
// (build/design-mockups/settings/settings-menu.html, pageWifi, sheet() and
// keyboard()): the connection and hotspot group, Networks with Search and a
// list that scrolls in itself, the entry inside the card with the new
// keyboard, and Connection WiFi | Ethernet on panels that can use Ethernet.
// Every action keeps the former WiFi popup's path, including its scan
// guards; nothing the former keyboard could type is lost.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const header = readRepoFile('src/core/i18n/i18n.h');
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
const rawScreen = readRepoFile('src/ui/tabs/settings/settings_screen.cpp');
const screen = maskCpp(rawScreen);
const rawKeyboard = readRepoFile('src/ui/tabs/settings/settings_keyboard.cpp');
const rawEntry = readRepoFile('src/ui/tabs/settings/settings_entry.cpp');
const entrySource = maskCpp(rawEntry);
const style = readRepoFile('src/ui/tabs/settings/settings_style.h');
const model = maskCpp(readRepoFile('src/ui/tabs/settings/tab_settings.cpp'));

const between = (source, from, to) => {
  const start = source.indexOf(from);
  const end = source.indexOf(to, start);
  assert.ok(start >= 0 && end > start, `${from} .. ${to}`);
  return source.slice(start, end);
};

// --- Translations ------------------------------------------------------------------------------
const keys = ['settings_choose_network', 'settings_hotspot', 'settings_hotspot_off_sub', 'settings_hotspot_on_sub',
  'settings_network', 'settings_address', 'settings_networks', 'settings_search', 'settings_searching',
  'settings_add_network', 'settings_join_network', 'settings_network_name', 'settings_connecting',
  'settings_connect_failed', 'settings_ethernet', 'settings_restart_to_switch', 'settings_ip_address',
  'settings_automatic', 'settings_static', 'settings_static_address', 'settings_set_in_web_admin'];
const english = ['Choose a network below', 'Hotspot', 'For setup without a network',
  'Other devices can connect to the panel', 'Network', 'Address', 'Networks', 'Search', 'Searching...',
  'Add network', 'Join network', 'Network name', 'Connecting...', 'Could not connect. Check the password.',
  'Ethernet', 'Restart to switch', 'IP address', 'Automatic', 'Static', 'Static address', 'Set in Web Admin'];
const struct = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
const fields = [...struct.replace(/\/\/[^\n]*/g, '').matchAll(/const char\*\s+(\w+);/g)].map(match => match[1]);
const first = fields.indexOf(keys[0]);
assert.deepEqual(fields.slice(first, first + keys.length), keys, 'the WiFi strings sit together');
for (const table of ['kStringsEn', 'kStringsDe', 'kStringsFr', 'kStringsPl']) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  const values = [...i18n.slice(start, i18n.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)]
    .map(match => match[1]);
  assert.equal(values.length, fields.length, `${table} has one value per field`);
  const texts = values.slice(first, first + keys.length);
  for (const [i, text] of texts.entries()) {
    assert.ok(text.length > 0, `${table}.${keys[i]} is set`);
    assert.doesNotMatch(text, /:\s*$/, `${table}.${keys[i]} has no colon`);
  }
  if (table === 'kStringsEn') assert.deepEqual(texts, english, 'English WiFi texts');
}

// --- Page ---------------------------------------------------------------------------------------------
const page = between(screen, 'void build_wifi_page(lv_obj_t* page) {', 'void show_page_content(bool shown) {');
const rawPage = between(rawScreen, '// ---------- WiFi page ----------', '// ---------- Frame ----------');
assert.doesNotMatch(rawPage.replace(/\/\/[^\n]*/g, ''), /"[A-Z][a-z]+/, 'no display text in the WiFi code');
assert.doesNotMatch(rawEntry.replace(/\/\/[^\n]*/g, ''), /"[A-Z][a-z]+/, 'no display text in the entry code');
for (const key of ['wifi_disconnect_btn', 'settings_not_connected', 'settings_connecting', 'settings_choose_network',
  'settings_hotspot', 'settings_hotspot_on_sub', 'settings_hotspot_off_sub', 'settings_add_network']) {
  assert.match(page, new RegExp(`s\\.${key}\\b`), `the page shows ${key}`);
}
// The hotspot is a toggle right under the status; with it on, its QR code.
assert.match(page, /settings_parts::toggle\(hotspot\.row, v\.access_point, settings_style::kWifiColor/);
assert.match(page, /if \(v\.access_point\) hotspot_row\(group\);/);
assert.match(rawPage, /"WIFI:T:WPA;S:%s;P:%s;;"/, 'the QR code joins the hotspot');
// The list scrolls in its own box down to the card's edge.
assert.match(page, /lv_obj_set_height\(list, room\);[\s\S]*?LV_OBJ_FLAG_SCROLLABLE/);
// Ethernet panels: WiFi | Ethernet and Restart while a mode waits for it.
const mode = between(screen, 'void build_network_mode(', 'void ip_mode_row(');
assert.match(mode, /settings_parts::segment\(mode\.row, options, 2, v\.ethernet_selected \? 1 : 0/);
assert.match(mode, /if \(v\.restart_needed\)/);
// The way back to DHCP stays while a static address is in use.
assert.match(page, /if \(v\.ip_mode_offered\) ip_mode_row\(group, v, palette\);/);
// Opening the page searches; its own rebuilds do not.
const build = between(screen, 'void build_page() {', 'void clear_refs() {');
assert.match(build, /if \(!g_wifi_quiet_rebuild\) settings_model::wifi_scan\(\);/);
assert.match(build, /g_page_timer = lv_timer_create\(on_page_timer, 500, nullptr\);/);
// The category: "Network" on Ethernet panels.
assert.match(between(screen, 'const char* category_title(', 'const char* category_icon('),
  /settings_model::ethernet_panel\(\) \? s\.settings_network : s\.wifi_label/);

// --- Entry (settings_entry.cpp, shared with the setup) --------------------------------------------------
const entry = between(entrySource, 'void open(const Spec& spec, bool manual, const char* ssid) {', 'void close(bool from_event) {');
assert.match(entry, /manual \? s\.settings_add_network : g\.stacked \? s\.settings_join_network : g_entry\.ssid/);
assert.match(entry, /settings_model::wifi_saved_password\(g_entry\.ssid\)/, 'the saved password is prefilled');
assert.match(entry, /settings_keyboard::create\(g_entry\.root, keys, keyboard_layout\(\)/);
const ok = between(entrySource, 'void on_key_ok() {', 'void on_close(lv_event_t*) {');
assert.match(ok, /if \(!g_entry\.manual && \(!password \|\| !password\[0\]\)\)/, 'a locked network needs its password');
assert.match(ok, /settings_model::wifi_connect\(ssid, password\);/);
assert.match(between(entrySource, 'void on_close(lv_event_t*) {', 'settings_keyboard::Layout keyboard_layout()'),
  /if \(g_entry\.busy\) return;/, 'the entry stays while connecting');
const entryTick = entrySource.slice(entrySource.indexOf('bool tick(const settings_model::WifiValues& v) {'));
assert.match(entryTick, /if \(v\.connect_failed\)[\s\S]*?settings_connect_failed/);
// The page opens the entry in its card and follows it while it connects.
assert.match(between(screen, 'void open_entry(bool manual, const char* ssid) {', 'void close_entry() {'),
  /settings_entry::open\(\{g_card, g_built_card, nullptr, on_entry_closed\}, manual, ssid\);/);
const tick = between(screen, 'void wifi_tick() {', 'void refresh_wifi_async(');
assert.match(tick, /if \(settings_entry::tick\(v\)\)/);
for (const [name, big, small] of [['kEntryFieldHeight', 'popup_layout::scale(72)', '44'],
  ['kKeyHeight', 'popup_layout::scale(84)', '56'], ['kKeyStep', 'popup_layout::scale(94)', '62']]) {
  assert.ok(style.includes(`constexpr int ${name} = ${big};`) && style.includes(`constexpr int ${name} = ${small};`), name);
}

// --- Keyboard: the mockup's rows, and every character of the former one --------------------------------
assert.match(rawKeyboard, /const char\* const kQwertz1\[\] = \{"q", "w", "e", "r", "t", "z"/);
assert.match(rawKeyboard, /const char\* const kAzerty1\[\] = \{"a", "z", "e", "r", "t", "y"/);
const glyphs = new Set();
for (const match of rawKeyboard.matchAll(/"((?:\\.|[^"\\])*)"/g)) {
  const text = match[1].replace(/\\x([0-9A-Fa-f]{2})/g, (_, h) => String.fromCharCode(parseInt(h, 16)))
    .replace(/\\(.)/g, '$1');
  const decoded = Buffer.from([...text].map(c => c.charCodeAt(0))).toString('utf8');
  if ([...decoded].length === 1) glyphs.add(decoded);
}
for (let code = 0x21; code <= 0x7e; ++code) {
  const ch = String.fromCharCode(code);
  if (/[A-Z]/.test(ch)) continue;  // Shift makes the capitals
  assert.ok(glyphs.has(ch), `the keyboard types ${ch}`);
}
for (const ch of 'äöüßąćęłńóśźżàâæçéèêëîïôœùûÿ') assert.ok(glyphs.has(ch), `holding a letter offers ${ch}`);
assert.match(rawKeyboard, /LV_EVENT_LONG_PRESSED_REPEAT/, 'Backspace repeats while held');
// The accent key shows the language's accented letters in the letter rows,
// as the former keyboard's language key did (user 2026-10-06: holding a
// letter alone was not to be found).
assert.match(rawKeyboard, /g_kb\.accent_key = special_key\(kAccentKey, colors\.group, colors\.button, 3, 1\.5f, 1\);/);
assert.match(rawKeyboard, /special_key\(kSpace, colors\.button, colors\.pressed, 3, 2\.5f, 5\);/);
const accentPages = rawKeyboard.slice(rawKeyboard.indexOf('struct AccentPage {'), rawKeyboard.indexOf('enum Special'));
const pageGlyphs = name => {
  const start = accentPages.indexOf(`const char* const ${name}[] = {`);
  const text = accentPages.slice(start, accentPages.indexOf('};', start));
  return [...text.matchAll(/"((?:\\.|[^"\\])*)"/g)].map(match => Buffer.from(match[1]
    .replace(/\\x([0-9A-Fa-f]{2})/g, (_, h) => String.fromCharCode(parseInt(h, 16))), 'latin1').toString('utf8'));
};
const french = ['kFrenchLower1', 'kFrenchLower2', 'kFrenchLower3'].flatMap(pageGlyphs).join('');
for (const ch of 'àâæéèêëîïôçœùûüÿ€') assert.ok(french.includes(ch), `the French page has ${ch}`);
const polish = ['kPolishLower1', 'kPolishLower2', 'kPolishLower3'].flatMap(pageGlyphs).join('');
for (const ch of 'ąćęłńóśźż') assert.ok(polish.includes(ch), `the Polish page has ${ch}`);
const european = ['kEuropeanLower1', 'kEuropeanLower2', 'kEuropeanLower3'].flatMap(pageGlyphs).join('');
for (const ch of 'äöüß') assert.ok(european.includes(ch), `the German page has ${ch}`);
const rawModel = readRepoFile('src/ui/tabs/settings/tab_settings.cpp');
assert.match(rawModel, /if \(lang\[0\] == 'f' && lang\[1\] == 'r'\) return 1;\s*if \(lang\[0\] == 'p' && lang\[1\] == 'l'\) return 2;/,
  'the accent page follows the language');
// French AZERTY can be chosen in Localization > Keyboard (value 3).
assert.match(rawModel, /"Fran\\xC3\\xA7" "ais \(AZERTY\)"/);
assert.match(model, /if \(cfg\.keyboard_layout == 3\) return 2;/);
assert.match(readRepoFile('src/core/config/config_manager.cpp'), /if \(normalized\.keyboard_layout > 3\) normalized\.keyboard_layout = 0;/);

// --- Model: the former WiFi popup's paths --------------------------------------------------------------
const scan = between(model, 'static void wifi_try_scan() {', 'static void wifi_stop_scan() {');
assert.match(scan, /networkManager\.probeWifiDriverHealth\(/);
assert.match(scan, /WiFi\.scanNetworks\([^)]*true\)/, 'the scan runs in the background');
const blocked = between(model, 'static bool wifi_scan_blocked() {', 'static void wifi_try_scan() {');
assert.match(blocked, /ap_mode_active/);
assert.match(blocked, /networkTransport\.isWifiDriverActive\(\)/);
assert.match(blocked, /wifi_scan_block_until/);
const connect = between(model, 'void wifi_connect(const char* ssid, const char* password) {', 'void wifi_disconnect() {');
assert.match(connect, /cfg\.wifi_static_enabled = false;/, 'a new network starts on DHCP');
assert.match(connect, /wifi_scan_block_until = millis\(\) \+ 10000UL;/);
assert.match(connect, /g_hotspot_callback\(false\)/);
assert.match(connect, /g_wifi_reconnect_callback\(\)/);
assert.match(between(model, 'void wifi_disconnect() {', 'void hotspot_selected('), /g_wifi_disconnect_callback\(\);/);
assert.match(between(model, 'void hotspot_selected(bool on) {', 'void network_mode_selected('), /g_hotspot_callback\(on\);/);
assert.match(between(model, 'void network_mode_selected(bool ethernet) {', 'void ip_mode_selected('),
  /configManager\.saveEthernetEnabled\(ethernet\)/);
assert.match(between(model, 'void ip_mode_selected(bool static_ip) {', 'void wifi_connect_done()'),
  /configManager\.saveStaticAddressingEnabled\(static_ip\)/);

// The hotspot loop reports the network on every pass (S3 2026-10-06: the
// clock on Localization flickered with the hotspot on): the category lines
// follow at most once a second and touch their styles only on a change.
const reports = between(model, 'static void refresh_lines_now_and_then() {', 'void settings_update_power_status()');
assert.match(reports, /if \(last_ms != 0 && now - last_ms < 1000\) return;/);
assert.match(reports, /void settings_update_wifi_status_ap\(const char\*, const char\*\) \{ refresh_lines_now_and_then\(\); \}/);
assert.match(reports, /void settings_update_ap_mode\(bool running\) \{\s*if \(running == ap_mode_active\) return;/);
const lines = between(screen, 'void refresh_lines() {', 'void sync_rotation()');
assert.match(lines, /if \(!lv_color_eq\(lv_obj_get_style_text_color\(view\.line, LV_PART_MAIN\), color\)\)/);
assert.match(lines, /if \(lv_obj_get_style_text_opa\(view\.line, LV_PART_MAIN\) != opa\)/);

console.log('Settings WiFi page: list, hotspot, entry with the new keyboard, Ethernet, former paths');
