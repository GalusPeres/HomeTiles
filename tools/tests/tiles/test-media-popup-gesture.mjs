import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const popup = fs.readFileSync(path.join(repoRoot, 'src/ui/popups/media/media_popup.cpp'), 'utf8');
const header = fs.readFileSync(path.join(repoRoot, 'src/ui/popups/media/media_popup.h'), 'utf8');
const definitions = cppFunctionDefinitions(popup);
function productionFunction(name) {
  const matches = definitions.filter(definition => definition.name === name);
  assert.equal(matches.length, 1, `Expected exactly one production definition of ${name}`);
  return matches[0].source;
}
function productionStruct(source, name) {
  const masked = maskCpp(source);
  const start = masked.indexOf(`struct ${name} {`);
  assert.ok(start >= 0, `Missing production struct ${name}`);
  const end = masked.indexOf('};', start);
  assert.ok(end > start, `Unclosed production struct ${name}`);
  return source.slice(start, end + 2);
}
const registrations = productionFunction('show_media_popup').match(
  /lv_obj_add_event_cb\(ctx->(?:volume|seek)_slider,\s*on_(?:volume|seek)_slider_event,\s*LV_EVENT_\w+,\s*ctx\);/g);
assert.ok(registrations?.length, 'Missing production slider event registrations');

// Execute the actual state records, render/update helpers, event handlers, and
// callback registrations. Only LVGL, the clock and MQTT output are substituted.
// This models delivered events, not the hardware conditions that deliver them.
const source = `
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
using String = std::string;
enum lv_event_code_t {
  LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST,
  LV_EVENT_CLICKED, LV_EVENT_PRESSED, LV_EVENT_ALL
};
struct lv_event_t { lv_event_code_t code; void* user_data; };
using Callback = void (*)(lv_event_t*);
struct Registration { Callback callback; lv_event_code_t code; void* user_data; };
struct lv_obj_t {
  int32_t value = 0;
  unsigned flags = 0, states = 0;
  std::string text;
  std::vector<Registration> callbacks;
};
void hide_popup_shell(lv_obj_t*){} void cancel_popup_open(lv_obj_t*){}
struct lv_image_dsc_t {};
struct lv_timer_t { void* user_data; };
constexpr unsigned LV_OBJ_FLAG_HIDDEN = 1, LV_OBJ_FLAG_CLICKABLE = 2;
constexpr unsigned LV_STATE_DISABLED = 1;
constexpr int LV_ANIM_OFF = 0;
static uint32_t now_ms = 10000;
static uint32_t millis() { return now_ms; }
static lv_event_code_t lv_event_get_code(lv_event_t* event) { return event->code; }
static void* lv_event_get_user_data(lv_event_t* event) { return event->user_data; }
static void* lv_timer_get_user_data(lv_timer_t* timer) { return timer->user_data; }
static int32_t lv_slider_get_value(lv_obj_t* obj) { return obj->value; }
static void lv_slider_set_value(lv_obj_t* obj, int32_t value, int) { obj->value = value; }
static void lv_obj_add_flag(lv_obj_t* obj, unsigned flag) { obj->flags |= flag; }
static void lv_obj_clear_flag(lv_obj_t* obj, unsigned flag) { obj->flags &= ~flag; }
static bool lv_obj_has_flag(lv_obj_t* obj, unsigned flag) { return (obj->flags & flag) != 0; }
static void lv_obj_add_state(lv_obj_t* obj, unsigned state) { obj->states |= state; }
static void lv_obj_clear_state(lv_obj_t* obj, unsigned state) { obj->states &= ~state; }
static void lv_label_set_text(lv_obj_t* obj, const char* text) { obj->text = text; }
static String getMdiChar(const char* icon) { return icon; }
static void lv_obj_add_event_cb(lv_obj_t* obj, Callback cb, lv_event_code_t code, void* data) {
  obj->callbacks.push_back({cb, code, data});
}
static void dispatch(lv_obj_t& obj, lv_event_code_t code) {
  for (const auto& registration : obj.callbacks) {
    if (registration.code == code || registration.code == LV_EVENT_ALL) {
      lv_event_t event{code, registration.user_data};
      registration.callback(&event);
    }
  }
}
struct Command { std::string entity; float value; };
static std::vector<Command> volumes, seeks;
static void mqttPublishMediaVolume(const char* entity, float value) { volumes.push_back({entity, value}); }
static void mqttPublishMediaSeek(const char* entity, float value) { seeks.push_back({entity, value}); }
${productionStruct(header, 'MediaPopupInit')}
${productionStruct(popup, 'MediaPopupContext')}
static MediaPopupContext* g_media_popup_ctx = nullptr;
void hide_media_popup();
${[
  'volume_icon_bucket_for_percent', 'volume_icon_for_bucket', 'set_volume_widgets',
  'update_volume', 'format_media_time', 'current_media_position', 'set_seek_widgets',
  'update_seek', 'media_progress_timer_cb', 'on_close_click',
  'on_volume_slider_event', 'on_seek_slider_event', 'hide_media_popup',
].map(productionFunction).join('\n\n')}

struct Fixture {
  MediaPopupContext context;
  MediaPopupInit init;
  lv_obj_t card, overlay, seek, current, duration, volume, volume_label, volume_icon;
  lv_timer_t timer{&context};
  Fixture() {
    now_ms = 10000;
    volumes.clear(); seeks.clear();
    context.entity_id = "media_player.test";
    context.card = &card; context.overlay = &overlay;
    context.seek_slider = &seek; context.seek_current_label = &current;
    context.seek_duration_label = &duration;
    context.volume_slider = &volume; context.volume_label = &volume_label;
    context.volume_icon_label = &volume_icon;
    overlay.flags = LV_OBJ_FLAG_CLICKABLE;
    init.is_playing = true;
    init.has_media_position = true; init.media_position = 10; init.media_duration = 200;
    init.media_position_received_ms = now_ms;
    init.has_volume = true; init.volume_level = 0.35f;
    update_seek(&context, init); update_volume(&context, init);
    auto* ctx = &context;
    ${registrations.join('\n    ')}
    g_media_popup_ctx = &context;
  }
  ~Fixture() { g_media_popup_ctx = nullptr; }
};
static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "FAIL line %d: %s\\n", __LINE__, #condition); ++failures; \
} } while (false)
static bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main() {
  // Release and lost-press must both finish a gesture. Changes only preview;
  // retained state must not overwrite a seek while the gesture is active.
  for (const auto terminal : {LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
    Fixture f;
    f.seek.value = 600; dispatch(f.seek, LV_EVENT_VALUE_CHANGED);
    CHECK(f.context.seek_dragging && f.current.text == "2:00" && seeks.empty());
    f.init.media_position = 20; update_seek(&f.context, f.init);
    CHECK(f.seek.value == 600 && near(f.context.media_position, 10));
    now_ms += 1000; media_progress_timer_cb(&f.timer);
    CHECK(f.seek.value == 600);
    dispatch(f.seek, terminal);
    CHECK(!f.context.seek_dragging);
    CHECK(seeks.size() == 1);
    if (seeks.size() == 1) CHECK(seeks[0].entity == "media_player.test" && near(seeks[0].value, 120));
    CHECK(near(f.context.media_position, 120) && f.context.media_position_received_ms == now_ms);
    now_ms += 1000; media_progress_timer_cb(&f.timer);
    CHECK(f.seek.value == 605 && f.current.text == "2:01");
    f.init.media_position = 30; f.init.media_position_received_ms = now_ms;
    update_seek(&f.context, f.init);
    CHECK(f.seek.value == 150 && near(f.context.media_position, 30));
    CHECK(seeks.size() == 1);

    f.volume.value = 64; dispatch(f.volume, LV_EVENT_VALUE_CHANGED);
    CHECK(volumes.empty() && f.volume_label.text == "64%" && !f.context.is_muted);
    dispatch(f.volume, terminal);
    CHECK(volumes.size() == 1);
    if (volumes.size() == 1) CHECK(volumes[0].entity == "media_player.test" && near(volumes[0].value, 0.64f));
  }

  // Hiding from navigation/sleep and closing the card cancel the drag silently.
  // Reusing the context must then accept the next entity's playback state.
  for (int close_path = 0; close_path < 3; ++close_path) {
    Fixture f;
    f.seek.value = 800; dispatch(f.seek, LV_EVENT_VALUE_CHANGED);
    if (close_path == 0) hide_media_popup();
    else {
      lv_event_t event{close_path == 1 ? LV_EVENT_CLICKED : LV_EVENT_RELEASED, &f.context};
      on_close_click(&event);
    }
    CHECK(!f.context.seek_dragging && seeks.empty());
    CHECK(lv_obj_has_flag(&f.card, LV_OBJ_FLAG_HIDDEN));
    CHECK(!lv_obj_has_flag(&f.overlay, LV_OBJ_FLAG_CLICKABLE));
    const int32_t hidden_value = f.seek.value;
    now_ms += 1000; media_progress_timer_cb(&f.timer);
    CHECK(f.seek.value == hidden_value);
    f.context.entity_id = "media_player.next";
    f.init.media_position = 45; f.init.media_position_received_ms = now_ms;
    update_seek(&f.context, f.init);
    CHECK(f.seek.value == 225 && near(f.context.media_position, 45));
    CHECK(seeks.empty());
  }

  // Losing availability during a seek still ends its lifecycle, but must not
  // turn an absent position or volume into a zero-valued outgoing command.
  for (const auto terminal : {LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
    Fixture f;
    f.seek.value = 800; dispatch(f.seek, LV_EVENT_VALUE_CHANGED);
    f.init.has_media_position = false; f.init.has_volume = false;
    update_seek(&f.context, f.init); update_volume(&f.context, f.init);
    dispatch(f.seek, terminal); dispatch(f.volume, terminal);
    CHECK(!f.context.seek_dragging && seeks.empty() && volumes.empty());
    media_progress_timer_cb(&f.timer);
    CHECK(lv_obj_has_flag(&f.seek, LV_OBJ_FLAG_HIDDEN));
    CHECK((f.seek.states & LV_STATE_DISABLED) != 0);
    f.init.has_media_position = true; f.init.media_position = 25;
    update_seek(&f.context, f.init);
    CHECK(!lv_obj_has_flag(&f.seek, LV_OBJ_FLAG_HIDDEN) && f.seek.value == 125);
  }

  // Both terminal paths retain the existing bounds and final-only publishing.
  for (const auto terminal : {LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
    for (const int raw : {-100, 1500}) {
      Fixture f;
      f.seek.value = raw; dispatch(f.seek, LV_EVENT_VALUE_CHANGED); dispatch(f.seek, terminal);
      f.volume.value = raw; dispatch(f.volume, LV_EVENT_VALUE_CHANGED); dispatch(f.volume, terminal);
      CHECK(seeks.size() == 1 && volumes.size() == 1);
      if (seeks.size() == 1) CHECK(near(seeks[0].value, raw < 0 ? 0 : 200));
      if (volumes.size() == 1) CHECK(near(volumes[0].value, raw < 0 ? 0 : 1));
      CHECK(f.context.is_muted == (raw < 0));
    }
  }

  {
    Fixture f;
    // Programmatic seek updates and unrelated events never start/publish a drag.
    f.context.updating_seek = true;
    dispatch(f.seek, LV_EVENT_VALUE_CHANGED); dispatch(f.seek, LV_EVENT_RELEASED);
    dispatch(f.seek, LV_EVENT_PRESS_LOST);
    CHECK(!f.context.seek_dragging && seeks.empty());
    f.context.updating_seek = false;
    dispatch(f.seek, LV_EVENT_PRESSED); dispatch(f.volume, LV_EVENT_PRESSED);
    CHECK(!f.context.seek_dragging && seeks.empty() && volumes.empty());
    for (auto callback : {on_seek_slider_event, on_volume_slider_event, on_close_click}) {
      lv_event_t event{LV_EVENT_RELEASED, nullptr}; callback(&event);
      event.user_data = &f.context;
      f.context.seek_slider = nullptr; f.context.volume_slider = nullptr;
      callback(&event);
    }
    CHECK(seeks.empty() && volumes.empty());
  }
  hide_media_popup(); media_progress_timer_cb(nullptr);
  return failures ? 1 : 0;
}
`;

const compiler = [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version'], {encoding: 'utf8'}).status === 0);
if (!compiler) {
  console.log('SKIP: Media popup gesture regression requires a C++17 host compiler');
  process.exit(0);
}
const buildDir = path.join(repoRoot, 'build/tests/media-popup-gesture');
fs.mkdirSync(buildDir, {recursive: true});
const sourcePath = path.join(buildDir, 'media-popup-gesture.cpp');
const outputPath = path.join(buildDir, process.platform === 'win32' ? 'media-popup-gesture.exe' : 'media-popup-gesture');
fs.writeFileSync(sourcePath, source);
const compiled = spawnSync(compiler,
  ['-std=c++17', '-Wall', '-Wextra', '-Werror', sourcePath, '-o', outputPath], {encoding: 'utf8'});
assert.equal(compiled.status, 0, `${compiled.stdout}${compiled.stderr}`);
const run = spawnSync(outputPath, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `${run.stdout}${run.stderr}`);
console.log('Media popup gesture completion, interrupted hide, unavailable state and final publishing: PASS');
