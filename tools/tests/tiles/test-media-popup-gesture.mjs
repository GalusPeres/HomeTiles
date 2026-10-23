// The Media popup's time and volume bars work like the v0.9.3 popup again
// (user 2026-10-22: the b487-b494 bars that drew their own knob and held
// the chosen value for up to 5 s jittered; "wieder wie vorher"): LVGL's
// lv_slider draws the knob and owns the drag, a drag publishes only its
// final value, release and lost press both end it, the reported state never
// moves a bar under the finger and shows at once otherwise, the volume's
// number stays in its place, closing cancels silently, an unavailable player
// sends no zero and the time bar follows the SEEK feature.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const popup = fs.readFileSync(path.join(repoRoot, 'src/ui/popups/media/media_popup.cpp'), 'utf8').replace(/\r\n/g, '\n');
const header = fs.readFileSync(path.join(repoRoot, 'src/ui/popups/media/media_popup.h'), 'utf8').replace(/\r\n/g, '\n');
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
  const end = masked.indexOf('\n};', start);
  assert.ok(end > start, `Unclosed production struct ${name}`);
  return source.slice(start, end + 3);
}
function productionLine(pattern, what) {
  const match = popup.match(pattern);
  assert.ok(match, `Missing ${what}`);
  return match[0];
}

// The library's status type (the popup keeps the last one it showed).
const browseStatus = fs.readFileSync(path.join(repoRoot, 'src/ui/popups/media/media_browse.h'), 'utf8')
  .replace(/\r\n/g, '\n').match(/enum class MediaBrowseStatus : uint8_t \{[\s\S]*?\n\};/)[0];

// The touch and the knob are an lv_slider with the old popup's steps and
// touch pads; the popup has no finger logic and draws no knob of its own.
const slider = productionFunction('make_slider');
assert.match(slider, /slider\.input = lv_slider_create\(parent\);/);
assert.match(slider, /lv_obj_set_style_bg_color\(slider\.input, lv_color_hex\(c\.control_icon\), LV_PART_KNOB\);/);
assert.match(slider, /lv_obj_set_style_pad_all\(slider\.input, \(knob - height\) \/ 2, LV_PART_KNOB\);/,
  'LVGL 9.6 sizes the knob from the bar and its padding');
assert.ok(!/kRemoteHoldMs|seek_hold_until|volume_target_pct|volume_bubble/.test(popup), 'no hold and no floating number');
assert.match(slider, /lv_slider_set_range\(slider\.input, 0, steps\);/);
assert.match(slider, /lv_obj_set_ext_click_area\(slider\.input, click_pad\);/);
assert.match(slider,
  /for \(lv_event_code_t code : \{LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST\}\) \{\s*lv_obj_add_event_cb\(slider\.input, handler, code, ctx\);/);
assert.ok(!/lv_indev_get_point|LV_INDEV_DEF_SCROLL_LIMIT/.test(popup), 'no own reading of the finger');
const player = productionFunction('build_player');
assert.match(player, /make_slider\(ctx->seek_slider, [^;]*, 1000,\s*popup_layout::scale\(18\), on_seek_slider_event, ctx\);/);
assert.match(player, /make_slider\(ctx->volume_slider, [^;]*, 100, popup_layout::scale\(20\), on_volume_slider_event, ctx\);/);

// Execute the actual state records, update helpers and event handlers. Only
// LVGL, the clock, the shell and MQTT output are substituted. This models
// delivered events, not the hardware conditions that deliver them.
const source = `
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
struct String : std::string {
  String() = default;
  String(const char* text) : std::string(text ? text : "") {}
  String(const std::string& text) : std::string(text) {}
  bool equalsIgnoreCase(const String& other) const {
    if (size() != other.size()) return false;
    for (size_t i = 0; i < size(); ++i)
      if (std::tolower(static_cast<unsigned char>((*this)[i])) != std::tolower(static_cast<unsigned char>(other[i]))) return false;
    return true;
  }
};
enum lv_event_code_t { LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST, LV_EVENT_CLICKED, LV_EVENT_PRESSED };
struct lv_event_t { lv_event_code_t code; void* user_data; };
using lv_event_cb_t = void (*)(lv_event_t*);
struct Registration { lv_event_cb_t callback; lv_event_code_t code; void* user_data; };
struct lv_obj_t {
  int x = 0, w = 0;
  int32_t value = 0;
  bool dragged = false;
  unsigned flags = 0, states = 0;
  std::string text;
  std::vector<Registration> callbacks;
};
struct lv_timer_t { void* user_data; bool paused = true; int resets = 0; };
struct lv_image_dsc_t {};
struct lv_draw_buf_t {};
struct lv_font_t {};
struct lv_color_t { uint32_t value; };
using lv_opa_t = uint8_t;
struct lv_point_t { int32_t x, y; };
constexpr unsigned LV_OBJ_FLAG_HIDDEN = 1, LV_OBJ_FLAG_CLICKABLE = 2;
constexpr unsigned LV_STATE_DISABLED = 1;
constexpr int LV_ANIM_OFF = 0;
constexpr int32_t LV_COORD_MAX = 1 << 29;
constexpr int LV_TEXT_FLAG_NONE = 0;
static uint32_t now_ms = 10000;
static uint32_t millis() { return now_ms; }
static lv_event_code_t lv_event_get_code(lv_event_t* event) { return event->code; }
static void* lv_event_get_user_data(lv_event_t* event) { return event->user_data; }
static void* lv_timer_get_user_data(lv_timer_t* timer) { return timer->user_data; }
static void lv_timer_pause(lv_timer_t* timer) { timer->paused = true; }
static void lv_timer_resume(lv_timer_t* timer) { timer->paused = false; }
static void lv_timer_reset(lv_timer_t* timer) { ++timer->resets; }
static void lv_obj_add_flag(lv_obj_t* obj, unsigned flag) { obj->flags |= flag; }
static void lv_obj_remove_flag(lv_obj_t* obj, unsigned flag) { obj->flags &= ~flag; }
static void lv_obj_clear_flag(lv_obj_t* obj, unsigned flag) { obj->flags &= ~flag; }
static bool lv_obj_has_flag(lv_obj_t* obj, unsigned flag) { return (obj->flags & flag) != 0; }
static bool lv_obj_has_state(lv_obj_t* obj, unsigned state) { return (obj->states & state) != 0; }
static void lv_obj_add_state(lv_obj_t* obj, unsigned state) { obj->states |= state; }
static void lv_obj_remove_state(lv_obj_t* obj, unsigned state) { obj->states &= ~state; }
static void lv_obj_set_width(lv_obj_t* obj, int w) { obj->w = w; }
static void lv_obj_set_x(lv_obj_t* obj, int x) { obj->x = x; }
static int lv_obj_get_x(lv_obj_t* obj) { return obj->x; }
static void lv_obj_delete(lv_obj_t*) {}
static int async_calls = 0;
static void lv_async_call(void (*)(void*), void*) { ++async_calls; }
static void lv_label_set_text(lv_obj_t* obj, const char* text) { obj->text = text; }
static const char* lv_label_get_text(lv_obj_t* obj) { return obj->text.c_str(); }
// lv_slider: a value LVGL keeps within its range; set_value never fires an event.
static int32_t lv_slider_get_value(const lv_obj_t* obj) { return obj->value; }
static void lv_slider_set_value(lv_obj_t* obj, int32_t value, int) { obj->value = value; }
static bool lv_slider_is_dragged(const lv_obj_t* obj) { return obj->dragged; }
static void lv_text_get_size(lv_point_t* size, const char* text, const lv_font_t*, int, int, int32_t, int) {
  size->x = 10 * static_cast<int32_t>(std::strlen(text)); size->y = 20;
}
namespace popup_layout {
constexpr int scale(int value) { return value; }
inline const lv_font_t* font20() { static lv_font_t font; return &font; }
}
static String getMdiChar(const char* icon) { return icon; }
static void lv_obj_add_event_cb(lv_obj_t* obj, lv_event_cb_t cb, lv_event_code_t code, void* data) {
  obj->callbacks.push_back({cb, code, data});
}
static void dispatch(lv_obj_t& obj, lv_event_code_t code) {
  for (const auto& registration : obj.callbacks) {
    if (registration.code != code) continue;
    lv_event_t event{code, registration.user_data};
    registration.callback(&event);
  }
}
// The finger moves the slider to value (LVGL's drag), then lifts.
static void drag(lv_obj_t& input, int32_t value) {
  input.dragged = true; input.value = value; dispatch(input, LV_EVENT_VALUE_CHANGED);
}
static void lift(lv_obj_t& input, lv_event_code_t code) { input.dragged = false; dispatch(input, code); }
static int shell_hides = 0, opens_cancelled = 0, browse_resets = 0;
void hide_popup_shell(lv_obj_t*) { ++shell_hides; }
void cancel_popup_open(lv_obj_t*) { ++opens_cancelled; }
void media_browse_reset() { ++browse_resets; }
struct Command { std::string entity; float value; };
static std::vector<Command> volumes, seeks, mutes;
static void mqttPublishMediaVolume(const char* entity, float value) { volumes.push_back({entity, value}); }
static void mqttPublishMediaSeek(const char* entity, float value) { seeks.push_back({entity, value}); }
static void mqttPublishMediaMute(const char* entity, bool muted) { mutes.push_back({entity, muted ? 1.0f : 0.0f}); }
${fs.readFileSync(path.join(repoRoot, 'src/ui/shared/ui_theme.h'), 'utf8')}
int8_t popup_drawn_surface(lv_obj_t*) { return ui_theme::kThemeSurface; }
${productionStruct(header, 'MediaPopupInit')}
${[...popup.matchAll(/^constexpr uint32_t kFeature\w+ = \d+;$/gm)].map(match => match[0]).join('\n')}
${productionStruct(popup, 'MediaGroupCandidate')}
${productionStruct(popup, 'MediaExtras')}
std::vector<MediaExtras> g_extras;
${productionStruct(popup, 'MediaColors')}
${productionLine(/^enum class MediaView : uint8_t \{[^}]*\};$/m, 'MediaView')}
${productionStruct(popup, 'MediaSlider')}
${productionStruct(popup, 'MarqueeLine')}
${browseStatus}
${productionStruct(popup, 'MediaPopupContext')}
static MediaPopupContext* g_media_popup_ctx = nullptr;
// The menu (test-media-popup-menu.mjs): closing hides it.
static void rebuild_list(MediaPopupContext* ctx, bool) {
  if (ctx->view == MediaView::Player && ctx->list_box) lv_obj_add_flag(ctx->list_box, LV_OBJ_FLAG_HIDDEN);
}
static int shadows_freed = 0;
static void free_menu_shadow(MediaPopupContext*, bool) { ++shadows_freed; }
${[
  'find_extras', 'extras_of', 'set_shown', 'body_scope', 'slider_show', 'slider_set_shown',
  'slider_set_enabled', 'slider_input_fraction', 'volume_icon_bucket_for_percent', 'volume_icon_for_bucket',
  'set_volume_widgets', 'update_volume', 'format_media_time', 'current_media_position',
  'set_seek_widgets', 'update_seek', 'media_progress_timer_cb', 'seek_allowed', 'apply_volume_drag',
  'on_volume_slider_event', 'apply_seek_drag', 'on_seek_slider_event', 'delete_dead_lists', 'drop_later', 'delete_list',
  'close_media_popup', 'on_close_click', 'hide_media_popup',
].map(productionFunction).join('\n\n')}

struct Fixture {
  MediaPopupContext context;
  MediaPopupInit init;
  lv_obj_t card, overlay, player, seek_track, seek_cap, seek_input;
  lv_obj_t volume_track, volume_cap, volume_input;
  lv_obj_t current, duration, volume_value, volume_icon;
  lv_timer_t progress{&context};
  Fixture() {
    now_ms = 10000;
    volumes.clear(); seeks.clear(); mutes.clear(); g_extras.clear();
    context.entity_id = "media_player.test";
    context.card = &card; context.overlay = &overlay; context.player = &player;
    context.seek_slider = {&seek_track, &seek_cap, &seek_input, 0.0f, 1000};
    context.volume_slider = {&volume_track, &volume_cap, &volume_input, 0.0f, 100};
    context.seek_current_label = &current; context.seek_duration_label = &duration;
    context.volume_value_label = &volume_value; context.volume_icon_label = &volume_icon;
    context.progress_timer = &progress;
    overlay.flags = LV_OBJ_FLAG_CLICKABLE;
    init.entity_id = context.entity_id;
    init.is_playing = true;
    init.has_media_position = true; init.media_position = 10; init.media_duration = 200;
    init.media_position_received_ms = now_ms;
    init.has_volume = true; init.volume_level = 0.35f;
    update_seek(&context, init); update_volume(&context, init);
    for (lv_event_code_t code : {LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
      lv_obj_add_event_cb(&seek_input, on_seek_slider_event, code, &context);
      lv_obj_add_event_cb(&volume_input, on_volume_slider_event, code, &context);
    }
    g_media_popup_ctx = &context;
  }
  ~Fixture() { g_media_popup_ctx = nullptr; }
};
static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \\
  std::fprintf(stderr, "FAIL line %d: %s\\n", __LINE__, #condition); ++failures; \\
} } while (false)
static bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main() {
  // A value from outside sets the slider; release and lost press both finish
  // a gesture; the drag only previews (LVGL holds the finger's value);
  // retained state must not move the bar while the finger is on it.
  for (const auto terminal : {LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
    Fixture f;
    CHECK(f.seek_input.value == 50 && f.volume_input.value == 35 && f.volume_value.text == "35%");
    drag(f.seek_input, 600);
    CHECK(f.context.seek_dragging && f.current.text == "2:00" && seeks.empty());
    f.init.media_position = 20; update_seek(&f.context, f.init);
    CHECK(f.seek_input.value == 600 && near(f.context.media_position, 10));
    now_ms += 1000; media_progress_timer_cb(&f.progress);
    CHECK(f.seek_input.value == 600 && f.current.text == "2:00");
    lift(f.seek_input, terminal);
    CHECK(!f.context.seek_dragging);
    CHECK(seeks.size() == 1);
    if (seeks.size() == 1) CHECK(seeks[0].entity == "media_player.test" && near(seeks[0].value, 120));
    CHECK(near(f.context.media_position, 120) && f.context.media_position_received_ms == now_ms);
    now_ms += 1000; media_progress_timer_cb(&f.progress);
    CHECK(near(f.context.seek_slider.value, 0.605f) && f.current.text == "2:01" && f.seek_input.value == 605);
    // As in v0.9.3 a reported position shows at once.
    f.init.media_position = 30; f.init.media_position_received_ms = now_ms;
    update_seek(&f.context, f.init);
    CHECK(near(f.context.seek_slider.value, 0.15f) && near(f.context.media_position, 30) && f.seek_input.value == 150);
    CHECK(seeks.size() == 1);

    // Volume: the number follows the finger in its fixed place, one command
    // at the end; a report during the drag never moves the knob, after it it
    // shows at once.
    drag(f.volume_input, 64);
    CHECK(volumes.empty() && f.volume_value.text == "64%" && !f.context.is_muted && f.volume_input.value == 64);
    f.init.volume_level = 0.2f; update_volume(&f.context, f.init);
    CHECK(f.volume_input.value == 64 && f.volume_value.text == "64%");
    f.init.volume_level = 0.35f;
    lift(f.volume_input, terminal);
    CHECK(volumes.size() == 1);
    if (volumes.size() == 1) CHECK(volumes[0].entity == "media_player.test" && near(volumes[0].value, 0.64f));
    update_volume(&f.context, f.init);
    CHECK(near(f.context.volume_slider.value, 0.35f) && f.volume_value.text == "35%" && f.volume_input.value == 35);
  }

  // Hiding from navigation/sleep and closing the card cancel the drag
  // silently. Reusing the context must then accept the next entity's state.
  for (int close_path = 0; close_path < 3; ++close_path) {
    Fixture f;
    drag(f.seek_input, 800);
    drag(f.volume_input, 50);
    if (close_path == 0) hide_media_popup();
    else {
      lv_event_t event{close_path == 1 ? LV_EVENT_CLICKED : LV_EVENT_RELEASED, &f.context};
      on_close_click(&event);
    }
    CHECK(!f.context.seek_dragging && !f.context.volume_dragging && seeks.empty() && volumes.empty());
    CHECK(lv_obj_has_flag(&f.card, LV_OBJ_FLAG_HIDDEN));
    CHECK(!lv_obj_has_flag(&f.overlay, LV_OBJ_FLAG_CLICKABLE));
    f.seek_input.dragged = false; f.volume_input.dragged = false;
    const float hidden_value = f.context.seek_slider.value;
    now_ms += 1000; media_progress_timer_cb(&f.progress);
    CHECK(near(f.context.seek_slider.value, hidden_value));
    f.context.entity_id = "media_player.next";
    f.init.media_position = 45; f.init.media_position_received_ms = now_ms;
    update_seek(&f.context, f.init);
    CHECK(near(f.context.seek_slider.value, 0.225f) && near(f.context.media_position, 45) && f.seek_input.value == 225);
    CHECK(seeks.empty());
  }

  // Losing availability during a drag still ends its lifecycle, but must not
  // turn an absent position or volume into a zero-valued outgoing command.
  for (const auto terminal : {LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
    Fixture f;
    drag(f.seek_input, 800);
    drag(f.volume_input, 80);
    f.init.has_media_position = false; f.init.has_volume = false;
    update_seek(&f.context, f.init); update_volume(&f.context, f.init);
    f.seek_input.value = 0; f.volume_input.value = 0;
    lift(f.seek_input, terminal); lift(f.volume_input, terminal);
    CHECK(!f.context.seek_dragging && !f.context.volume_dragging && seeks.empty() && volumes.empty());
    media_progress_timer_cb(&f.progress);
    CHECK(lv_obj_has_flag(&f.seek_track, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(&f.seek_input, LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_obj_has_state(&f.seek_input, LV_STATE_DISABLED) && lv_obj_has_state(&f.volume_input, LV_STATE_DISABLED));
    f.init.has_media_position = true; f.init.media_position = 25;
    update_seek(&f.context, f.init);
    CHECK(!lv_obj_has_flag(&f.seek_input, LV_OBJ_FLAG_HIDDEN) && near(f.context.seek_slider.value, 0.125f));
    CHECK(!lv_obj_has_state(&f.seek_input, LV_STATE_DISABLED));
  }

  // Both terminal paths keep the bounds and publish only the final value.
  for (const auto terminal : {LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
    for (const int32_t raw : {-100, 1500}) {
      Fixture f;
      drag(f.seek_input, raw); lift(f.seek_input, terminal);
      drag(f.volume_input, raw); lift(f.volume_input, terminal);
      CHECK(seeks.size() == 1 && volumes.size() == 1);
      if (seeks.size() == 1) CHECK(near(seeks[0].value, raw < 0 ? 0 : 200));
      if (volumes.size() == 1) CHECK(near(volumes[0].value, raw < 0 ? 0 : 1));
      CHECK(f.context.is_muted == (raw < 0));
    }
  }

  // A player that reports its features but not SEEK keeps the bar still.
  {
    Fixture f;
    MediaExtras extras;
    extras.entity_id = f.context.entity_id;
    extras.has_features = true;
    extras.features = kFeatureVolumeSet;
    g_extras.push_back(extras);
    drag(f.seek_input, 500); lift(f.seek_input, LV_EVENT_RELEASED);
    CHECK(!f.context.seek_dragging && seeks.empty());
    g_extras[0].features |= kFeatureSeek;
    drag(f.seek_input, 500); lift(f.seek_input, LV_EVENT_RELEASED);
    CHECK(seeks.size() == 1 && near(seeks[0].value, 100));
  }

  // Unrelated events and missing parts never start or publish a drag.
  {
    Fixture f;
    dispatch(f.seek_input, LV_EVENT_CLICKED); dispatch(f.volume_input, LV_EVENT_CLICKED);
    dispatch(f.seek_input, LV_EVENT_PRESSED); dispatch(f.volume_input, LV_EVENT_PRESSED);
    CHECK(!f.context.seek_dragging && seeks.empty() && volumes.empty());
    for (auto callback : {on_seek_slider_event, on_volume_slider_event, on_close_click}) {
      lv_event_t event{LV_EVENT_RELEASED, nullptr}; callback(&event);
    }
    f.context.seek_slider.input = nullptr; f.context.volume_slider.input = nullptr;
    for (auto callback : {on_seek_slider_event, on_volume_slider_event}) {
      lv_event_t event{LV_EVENT_RELEASED, &f.context}; callback(&event);
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
  ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-const-variable',
    '-Wno-deprecated-declarations', sourcePath, '-o', outputPath], {encoding: 'utf8'});
assert.equal(compiled.status, 0, `${compiled.stdout}${compiled.stderr}`);
const run = spawnSync(outputPath, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `${run.stdout}${run.stderr}`);
console.log('Media popup bars like v0.9.3: lv_slider knob and touch, final-only publishing, reports at once, interrupted hide, unavailable state, SEEK feature: PASS');
