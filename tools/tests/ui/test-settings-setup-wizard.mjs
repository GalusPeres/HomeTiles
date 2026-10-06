// The first-start setup of the approved mockup
// (build/design-mockups/settings/settings-menu.html, wizard A): four steps in
// the popup card, with every text translated, and the traps of the earlier
// attempt closed: a new panel starts its network without a restart (the WiFi
// search and the Bridge link work), a restart resumes the setup, the pairing
// prompt opens the setup's Home Assistant step instead of System, and the
// setup shows only on a new panel or through System > Setup.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const header = readRepoFile('src/core/i18n/i18n.h');
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
const rawSetup = readRepoFile('src/ui/tabs/settings/setup_screen.cpp');
const setup = maskCpp(rawSetup);
const screen = maskCpp(readRepoFile('src/ui/tabs/settings/settings_screen.cpp'));
const rawModel = readRepoFile('src/ui/tabs/settings/tab_settings.cpp');
const model = maskCpp(rawModel);
const entry = maskCpp(readRepoFile('src/ui/tabs/settings/settings_entry.cpp'));
const sketch = maskCpp(readRepoFile('HomeTiles.ino'));

const between = (source, from, to) => {
  const start = source.indexOf(from);
  const end = source.indexOf(to, start);
  assert.ok(start >= 0 && end > start, `${from} .. ${to}`);
  return source.slice(start, end);
};

// --- Translations ------------------------------------------------------------------------------
const keys = ['setup_step_fmt', 'setup_language_title', 'setup_wifi_title', 'setup_ha_title', 'setup_tiles_title',
  'setup_next', 'setup_finish', 'setup_later', 'setup_other_network', 'setup_with_phone', 'setup_open_ha',
  'setup_open_ha_where', 'setup_add_hometiles', 'setup_discovered_fmt', 'setup_continues', 'setup_tap_retry',
  'setup_no_bridge', 'setup_guide', 'setup_back_to_pairing', 'setup_install_bridge', 'setup_pairing_code',
  'setup_paired', 'setup_wifi_first', 'setup_scan_tiles', 'setup_password_hint', 'setup_password_hint_long',
  'setup_leave_question', 'setup_leave_text_fmt', 'setup_leave'];
const english = ['Step %d of %d', 'Choose your language', 'Connect to WiFi', 'Add to Home Assistant', 'Add your tiles',
  'Next', 'Finish', 'Later', 'Other network', 'Set up with your phone', 'Open Home Assistant',
  'Settings › Devices & services', 'Add HomeTiles', 'Under Discovered · %s', 'Continues by itself', 'Tap to try again',
  'No HomeTiles Bridge yet?', 'Setup guide', 'Back to the pairing', 'Install the Bridge first', 'Pairing code',
  'Paired with Home Assistant', 'Connect WiFi first', 'Scan it to add your tiles.', 'Set a Web Admin password there.',
  'Set a Web Admin password there, so only you can change the panel.', 'Leave the setup?',
  'You can finish it later in %s › System › %s.', 'Leave'];
const struct = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
const fields = [...struct.replace(/\/\/[^\n]*/g, '').matchAll(/const char\*\s+(\w+);/g)].map(match => match[1]);
const first = fields.indexOf(keys[0]);
assert.deepEqual(fields.slice(first, first + keys.length), keys, 'the setup strings sit together');
const placeholders = text => (text.match(/%[ds]/g) || []).join('');
for (const table of ['kStringsEn', 'kStringsDe', 'kStringsFr', 'kStringsPl']) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  const values = [...i18n.slice(start, i18n.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)]
    .map(match => match[1]);
  assert.equal(values.length, fields.length, `${table} has one value per field`);
  const texts = values.slice(first, first + keys.length);
  for (const [i, text] of texts.entries()) {
    assert.ok(text.length > 0, `${table}.${keys[i]} is set`);
    assert.equal(placeholders(text), placeholders(english[i]), `${table}.${keys[i]} keeps its placeholders`);
    // The arrow U+2192 is not in the firmware fonts; U+203A is.
    assert.doesNotMatch(text, /→/, `${table}.${keys[i]} uses a character the fonts have`);
  }
  if (table === 'kStringsEn') assert.deepEqual(texts, english, 'English setup texts');
}

// --- Screen: the popup card, step head, X, body, Back / dots / Next ---------------------------------------
assert.doesNotMatch(rawSetup.replace(/\/\/[^\n]*/g, ''), /"[A-Z][a-z]+/, 'no display text in the setup code');
const card = between(setup, 'void build_card() {', 'void tick() {');
assert.match(card, /lv_obj_set_size\(g_card, card_width\(\), card_height\(\)\);/);
assert.match(setup, /int card_width\(\) \{ return popup_layout::kCardWidth; \}/);
assert.match(setup, /int card_height\(\) \{ return popup_layout::kCardHeight; \}/);
assert.match(card, /settings_parts::step_head\(g_content, card_width\(\), kSteps\[g_step\]\.icon/);
assert.match(card, /settings_parts::close_button\(g_content, settings_parts::popup_close_x\(card_width\(\)\)/);
// No full-screen veil: the card sits on the Settings panel, dialogs use the
// popup shadow over a transparent layer (settings_parts::dialog).
assert.doesNotMatch(setup, /LV_OPA_(?:50|60|70)|veil/i);
const foot = between(setup, 'void build_foot(const Colors& palette) {', 'uint32_t body_key() {');
assert.match(foot, /if \(g_step > 0\)/, 'no Back on the first step');
assert.match(rawSetup, /s\.wifi_back_btn, "chevron-left"/);
assert.match(foot, /else if \(g_step == 1 && !step_done\(1\)\) \{\s*label = nullptr;/, 'Next once WiFi is connected');
assert.match(foot, /else if \(g_step == 2 && !step_done\(2\)\) \{\s*label = s\.setup_later;/, 'Later until paired');
assert.match(foot, /label = s\.setup_finish;/);
// The four steps.
const body = between(setup, 'void build_body() {', 'void build_card() {');
for (const step of ['build_language', 'build_wifi', 'build_home_assistant', 'build_tiles']) {
  assert.match(body, new RegExp(`${step}\\(palette\\);`), step);
}
const language = between(setup, 'void build_language(const Colors& palette) {', 'void signal_icon(');
assert.match(language, /settings_screen::rotation_segment\(rotation\.row, palette\.card\)/, 'rotation from the Display page');
assert.match(setup, /LocaleList locale_list\(uint8_t index\) \{ return index == 0 \? LocaleList::Language : LocaleList::TimeZone; \}/,
  'language and time zone (a new panel starts on Berlin)');
const wifi = between(setup, 'void build_wifi(const Colors& palette) {', 'lv_obj_t* column_text(');
assert.match(wifi, /s\.setup_other_network/);
// Networks with Search, as on the WiFi page (user 2026-10-06).
assert.match(wifi, /settings_screen::networks_heading\(g_body, v\.scanning, true, on_action, data\(Action::Search\)\);/);
assert.match(setup, /case Action::Search:\s*settings_model::wifi_scan\(\);/);
assert.match(wifi, /phone_row\(more, v, palette\);/, 'the phone setup stays visible under the list');
assert.match(rawSetup, /"WIFI:T:WPA;S:%s;P:%s;;"/, 'the QR code joins the hotspot');
assert.match(wifi, /lv_obj_set_height\(list, room\);[\s\S]*?LV_OBJ_FLAG_SCROLLABLE/, 'the list scrolls in itself');
const ha = between(setup, 'void build_home_assistant(const Colors& palette) {', 'void build_tiles(');
assert.match(rawSetup, /settings_parts::row\(group, "shield-check", s\.setup_paired\)/, 'paired shows the green shield');
assert.match(ha, /color_text\(row\.icon, settings_style::kGoodColor\);/);
assert.match(ha, /number_row\(steps, 1, s\.setup_open_ha, s\.setup_open_ha_where\);/);
assert.match(ha, /settings_model::device_name\(\)/, 'the name Home Assistant lists');
assert.match(between(setup, 'void build_code(', 'void build_home_assistant('), /settings_model::pairing_number\(/);
const tiles = between(setup, 'void build_tiles(const Colors& palette) {', 'void build_foot(');
assert.match(tiles, /settings_model::web_admin_url\(url, sizeof\(url\)\)/);
assert.match(tiles, /settings_parts::qr_code\(holder, qr, url\);/);

// --- Behaviour ---------------------------------------------------------------------------------------------
const tick = between(setup, 'void tick() {', 'void on_timer(lv_timer_t*)');
// Step by step: the setup never moves on by itself (user 2026-10-06); Next
// appears once WiFi is connected or the panel paired.
assert.doesNotMatch(tick, /go_to\(/, 'no jump to the next step');
assert.doesNotMatch(setup, /kNextDelayMs|g_next_at/);
// The Home Assistant step keeps the panel findable, at most every few seconds.
assert.match(tick, /pairing == PairState::NotPaired &&\s*\(g_pair_tried_at == 0 \|\| now - g_pair_tried_at >= kPairRetryMs\)/);
assert.match(tick, /settings_model::pair\(\);/);
// The WiFi entry is the Settings entry with the step head.
assert.match(setup, /settings_entry::open\(\{g_card, g_built_card, line, on_entry_closed\}, manual, ssid\);/);
assert.match(between(entry, 'Geometry geometry(int card_w, int card_h, bool setup) {', 'void focus_field('),
  /g\.close_x = settings_parts::popup_close_x\(card_w\);/);
// A touch never builds its own card anew inside its event.
assert.match(between(setup, 'void on_action(lv_event_t* e) {', 'void close_dialog(bool from_event) {'),
  /case Action::Next:\s*go_later\(g_step \+ 1\);/);
const finish = between(setup, 'void finish() {', 'void refresh_async(');
assert.match(finish, /settings_model::setup_end\(\);\s*lv_async_call\(go_home_async, nullptr\);/);
// X on the last step finishes without asking (user 2026-10-06).
assert.match(setup, /case Action::Close:\s*if \(g_step == kStepCount - 1\) \{\s*finish\(\);\s*\} else \{\s*open_leave_dialog\(\);/);
// Back and Next in the popups' navigation size (user 2026-10-06).
assert.match(readRepoFile('src/ui/tabs/settings/settings_style.h'), /constexpr int kSetupNavHeight = popup_layout::kNavHeight;/);
assert.match(between(setup, 'lv_obj_t* nav_button(', 'void build_foot('), /settings_style::kSetupNavHeight/);
// Connecting shows moving dots, so it never looks stuck (user 2026-10-06).
assert.match(entry, /show_progress\(settings_model::text\(\)\.settings_connecting\);/);
assert.match(entry, /lv_timer_create\(on_dots, 400, nullptr\)/);
// Leaving asks first; it can be finished later in System > Setup.
const leave = between(setup, 'void open_leave_dialog() {', 'void build_language(');
assert.match(leave, /settings_parts::dialog\(g_panel, g_built_card, s\.setup_leave_question/);
assert.match(leave, /s\.setup_leave_text_fmt, s\.tile_type_settings, s\.settings_setup/);

// --- Settings hosts it -------------------------------------------------------------------------------------
const show = between(screen, 'void show_setup() {', 'void build(lv_obj_t* panel) {');
assert.match(show, /clear_refs\(\);\s*lv_obj_clean\(g_panel\);/, 'the frame goes while the setup runs');
assert.match(show, /setup_screen::show\(g_panel\);/);
const prepare = between(screen, 'void prepare_show() {', 'void did_hide() {');
assert.match(prepare, /if \(settings_model::setup_step\(\) >= 0\) \{\s*show_setup\(\);/);
assert.match(between(screen, 'void did_hide() {', 'void refresh_lines() {'), /setup_screen::hide\(\);/);
// System > Setup starts it (the button is no longer disabled).
assert.match(screen, /case SystemAction::Setup:[\s\S]*?settings_model::setup_start\(\);\s*lv_async_call\(setup_async, nullptr\);/);
assert.doesNotMatch(screen, /SystemAction::Setup\) settings_parts::button_set_enabled\(b, false\)/);

// --- Model: kept in flash, new panel, resume, pairing prompt -----------------------------------------------
assert.match(rawModel, /kSetupNamespace = "tab5_config";/);
assert.match(rawModel, /kSetupKey = "setup_step";/);
const store = between(model, 'static void store_setup_step(int step) {', 'int setup_step() {');
assert.match(store, /Device::ScopedStorageWrite storage_write\(BatchedNvsWrite::kNeedsDisplayGuard\);/);
assert.match(store, /prefs\.remove\(kSetupKey\);/);
const begin = between(model, 'bool settings_begin_new_panel() {', 'void settings_resume_setup() {');
assert.match(rawModel, /const bool stored = prefs\.isKey\("configured"\);/, 'only a panel that never stored a configuration');
assert.match(begin, /if \(stored\) return false;/);
assert.match(begin, /settings_model::setup_start\(\);\s*const bool saved = configManager\.save\(configManager\.getConfig\(\)\);/);
assert.match(between(model, 'void settings_resume_setup() {', '\n}\n'), /uiManager\.switchToTab\(3\);/);
const prompt = between(model, 'void settings_show_pairing() {', 'namespace settings_model {');
assert.match(prompt, /if \(settings_model::setup_step\(\) >= 0\) \{\s*settings_model::setup_step_changed\(2\);/,
  'the pairing prompt opens the setup step, not System');
// The sketch: defaults before the network starts, the setup after the UI is built.
const load = sketch.indexOf('bool has_config = configManager.load();');
const begins = sketch.indexOf('if (!has_config) has_config = settings_begin_new_panel();');
const network = sketch.indexOf('networkManager.init();');
assert.ok(load >= 0 && begins > load && network > begins, 'a new panel stores its defaults before the network starts');
const rawSketch = readRepoFile('HomeTiles.ino');
const built = rawSketch.indexOf('Serial.println("[Setup] UI built");');
const resume = rawSketch.indexOf('settings_resume_setup();');
assert.ok(built > 0 && resume > built, 'the setup resumes after the UI is built');

console.log('First-start setup: four steps in the popup card, translated, resumed, no restart for a new panel');
