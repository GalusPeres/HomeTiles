import {radiusPolicyHost, surfaceStyleHost} from '../../lib/surface-style-host.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const stripIncludes = text => text.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const definition = (file, name) => {
  const result = cppFunctionDefinitions(read(file)).find(fn => fn.name === name);
  assert(result, name);
  return result.source;
};
const translations = [...read('src/core/i18n/i18n.cpp').matchAll(/static const Strings kStrings(?:De|En|Fr) = \{[\s\S]*?\};/g)];
assert.equal(translations.length, 3);
const host = await lvglHost(root);
if (!host) { console.log('SKIP: PIN popup title reuse requires native LVGL'); process.exit(0); }
const out = path.join(root, 'build/tests/pin-popup-title-reuse');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');

// Execute the complete PIN popup and shared shell, including preload, reuse,
// real title owners and style/size events. Only hardware and config are adapted.
fs.writeFileSync(source, String.raw`
#include <lvgl.h>
#include <lvgl_private.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <new>
#include <iostream>
#include "src/ui/shared/title_label.h"
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_body.h"
class String : public std::string {
 public:
  using std::string::string; using std::string::operator=;
  String() = default;
  String(const std::string& value) : std::string(value) {}
  void replace(const char* from, const String& to) {
    size_t pos = 0;
    while ((pos = find(from, pos)) != npos) {
      std::string::replace(pos, strlen(from), to); pos += to.size();
    }
  }
};
${stripIncludes(read('src/core/i18n/i18n.h'))}
namespace i18n {
${translations.map(match => match[0]).join('\n')}
const Strings& strings(const char* language) {
  if (!strcmp(language, "de")) return kStringsDe;
  if (!strcmp(language, "fr")) return kStringsFr;
  return kStringsEn;
}
}
${radiusPolicyHost(root)}
#include "src/core/config/icon_glow.h"
struct Config { int tile_radius = tile_radius::kMinimum; const char* language = "de"; bool tile_borders = true; bool icon_discs = true; uint8_t icon_glow = icon_glow::kDefault; };
struct ConfigManager { Config config; const Config& getConfig() { return config; } } configManager;
extern "C" {
LV_FONT_DECLARE(ui_font_12); LV_FONT_DECLARE(ui_font_14); LV_FONT_DECLARE(ui_font_16);
LV_FONT_DECLARE(ui_font_20); LV_FONT_DECLARE(ui_font_24); LV_FONT_DECLARE(ui_font_28);
LV_FONT_DECLARE(ui_font_32); LV_FONT_DECLARE(ui_font_40); LV_FONT_DECLARE(mdi_icons_32);
LV_FONT_DECLARE(mdi_icons_48);
LV_FONT_DECLARE(ui_font_48); LV_FONT_DECLARE(ui_font_56); LV_FONT_DECLARE(ui_font_64);
LV_FONT_DECLARE(ui_font_72); LV_FONT_DECLARE(ui_font_80); LV_FONT_DECLARE(ui_font_96);
}
#if defined(DEVICE_LAYOUT_480X480)
#define FONT_MDI_ICONS (&mdi_icons_32)
#else
#define FONT_MDI_ICONS (&mdi_icons_48)
#endif
String getMdiChar(const String&) { return "\xF3\xB0\x96\xAD"; }
constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
int allocations = 0;
void* heap_caps_malloc(size_t size, int) { ++allocations; return malloc(size); }
void heap_caps_free(void* pointer) { if (pointer) { --allocations; free(pointer); } }
${surfaceStyleHost(root)}
${stripIncludes(read('src/ui/popups/popup_layout.h'))}
${stripIncludes(read('src/ui/popups/popup_open.h'))}
${stripIncludes(read('src/ui/popups/popup_shell.h'))}
${stripIncludes(read('src/ui/popups/popup_open.cpp'))}
${stripIncludes(read('src/ui/popups/popup_shell.cpp'))}
${stripIncludes(read('src/ui/popups/popup_nav_style.h'))}
${stripIncludes(read('src/core/config/pin_access.h'))}
namespace pin_access {
${definition('src/core/config/pin_access.cpp', 'secureClear')}
}
${definition('src/tiles/runtime/tile_renderer_shared.h', 'disable_pressed_button_animation')}
void hide_light_popup() {} void hide_climate_popup() {} void hide_cover_popup() {}
void hide_sensor_popup() {} void hide_weather_popup() {} void hide_energy_popup() {}
void hide_media_popup() {} void hide_camera_popup() {} void hide_device_popup() {}
namespace pin_test {
${stripIncludes(read('src/ui/popups/pin/pin_keypad_geometry.h'))}
${stripIncludes(read('src/ui/popups/pin/pin_popup.h'))}
${stripIncludes(read('src/ui/popups/pin/pin_popup.cpp'))}
}
using namespace pin_test;
bool verify_test(const char*, void*) { return false; }
void success_test(void*) {}

// The agreed Unlock layout on every screen: the prompt, the dots line and the
// keys sit below the header, inside the card, centered with the same margin
// above and below; keys are a bit wider than tall and large enough to hit.
void check_layout(const char* layout) {
  lv_obj_update_layout(shell.overlay);
  lv_area_t card; lv_obj_get_coords(g_ctx->card, &card);
  const int pad = lv_obj_get_style_pad_top(g_ctx->card, LV_PART_MAIN);
  const int header_bottom = card.y1 + popup_layout::kHeaderCenterY + popup_layout::kHeaderIconDiscSize / 2;
  lv_area_t prompt, dots, first, last;
  lv_obj_get_coords(g_ctx->prompt_label, &prompt);
  lv_obj_get_coords(g_ctx->dots_row, &dots);
  lv_obj_get_coords(g_ctx->key_buttons[0], &first);
  lv_obj_get_coords(g_ctx->key_buttons[kConfirmKey], &last);
  const int key_w = lv_obj_get_width(g_ctx->key_buttons[0]), key_h = lv_obj_get_height(g_ctx->key_buttons[0]);
  const int above = prompt.y1 - header_bottom, below = card.y2 - pad - last.y2;
  // Visible gaps: header to the prompt's capitals, prompt baseline to the
  // dots, dots to the keys.
  const lv_font_t* font = lv_obj_get_style_text_font(g_ctx->prompt_label, LV_PART_MAIN);
  const int baseline = prompt.y1 + lv_font_get_line_height(font) - font->base_line;
  lv_font_glyph_dsc_t cap; lv_font_get_glyph_dsc(font, &cap, 'E', 0);
  lv_area_t dot; lv_obj_get_coords(g_ctx->dots[0], &dot);
  const int to_prompt = baseline - cap.box_h - cap.ofs_y - header_bottom;
  const int to_dots = dot.y1 - baseline, to_keys = first.y1 - dot.y2 - 1;
  std::cout << layout << ": keys " << key_w << "x" << key_h << ", above " << above << ", below " << below << "\n";
  assert(above >= 0 && "The prompt sits below the header");
  assert(dots.y1 > prompt.y2 && first.y1 > dots.y2 && "Prompt, dots line and keys stack in order");
  assert(first.x1 >= card.x1 + pad && last.x2 <= card.x2 - pad && below >= 0 && "Keys stay inside the card");
  assert(key_w > key_h && key_h >= popup_layout::scale(56) && "Keys are wider than tall and easy to hit");
  assert(std::abs(to_prompt - to_dots) <= 2 && std::abs(to_dots - to_keys) <= 2 &&
         "Three equal gaps: header, prompt, dots, keys");
  if (popup_layout::kKeypadKeyMaxPermille < 1000) {
    assert(key_h <= popup_layout::kCardHeight * 125 / 1000 && "Large panels keep the keys at an eighth of the card");
  }
  for (int i = 0; i < kKeyCount; ++i) {
    lv_area_t a; lv_obj_get_coords(g_ctx->key_buttons[i], &a);
    for (int j = i + 1; j < kKeyCount; ++j) {
      lv_area_t b; lv_obj_get_coords(g_ctx->key_buttons[j], &b);
      assert((a.x2 < b.x1 || b.x2 < a.x1 || a.y2 < b.y1 || b.y2 < a.y1) && "Keys never overlap");
    }
  }
}

// "Enter PIN" always stays; below it one dot per typed digit, never a
// digit; the error replaces the prompt after a wrong PIN until the next key.
void check_prompt(const i18n::Strings& tr) {
  auto shown = [](lv_obj_t* obj) { return !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN); };
  auto click = [](int key) { lv_obj_send_event(g_ctx->key_buttons[key], LV_EVENT_CLICKED, nullptr); };
  // Filled circles: one per typed digit; empty ones fill up to at least the
  // shortest PIN.
  auto dots = []() {
    int count = 0;
    for (lv_obj_t* dot : g_ctx->dots)
      count += !lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN) && lv_obj_get_style_bg_opa(dot, LV_PART_MAIN) == LV_OPA_COVER;
    return count;
  };
  auto circles = []() {
    int count = 0;
    for (lv_obj_t* dot : g_ctx->dots) count += !lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN);
    return count;
  };
  assert(shown(g_ctx->prompt_label) && dots() == 0 && circles() == 4);
  assert(!strcmp(lv_label_get_text(g_ctx->prompt_label), tr.pin_popup_enter));
  click(0); click(1);
  assert(shown(g_ctx->prompt_label) && !strcmp(lv_label_get_text(g_ctx->prompt_label), tr.pin_popup_enter));
  assert(dots() == 2 && circles() == 4 && "One filled circle per digit right away, no digit shown");
  // No child of the dots line shows text.
  for (uint32_t i = 0; i < lv_obj_get_child_count(g_ctx->dots_row); ++i) {
    assert(!lv_obj_check_type(lv_obj_get_child(g_ctx->dots_row, static_cast<int32_t>(i)), &lv_label_class));
  }
  click(kBackspaceKey);
  assert(dots() == 1);
  for (int i = 0; i < 12; ++i) click(kZeroKey);
  assert(g_ctx->length == pin_access::kInputMaxDigits && dots() == static_cast<int>(pin_access::kInputMaxDigits) &&
         circles() == static_cast<int>(pin_access::kInputMaxDigits));
  click(kConfirmKey);
  assert(dots() == 0 && circles() == 4 && !strcmp(lv_label_get_text(g_ctx->prompt_label), tr.pin_popup_incorrect));
  click(3);
  assert(dots() == 1 && g_ctx->length == 1 && !strcmp(lv_label_get_text(g_ctx->prompt_label), tr.pin_popup_enter));
}

void check_title(const String& expected) {
  lv_obj_update_layout(shell.overlay);
  sync_popup_shell();
  assert(!strcmp(hometiles_title::text(g_ctx->title_label), expected.c_str()) &&
         "Reused PIN popup must replace the preloaded Settings title owner");
  assert(!strcmp(hometiles_title::text(shell.title), expected.c_str()) &&
         "Visible shared header must use the current protected tile title");
  // With the state line the header shows the title on one line (shortened
  // like the Sensor headers), so only the logical title is compared.
}
int main() {
  assert(!strcmp(i18n::strings("de").tile_radius,"Kachelradius"));
  assert(!strcmp(i18n::strings("en").tile_radius,"Tile radius"));
  assert(!strcmp(i18n::strings("fr").tile_radius,"Rayon des tuiles"));
  lv_init();
  auto* display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  std::vector<uint32_t> band(SCREEN_WIDTH * 16);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, band.data(), nullptr, band.size() * 4, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  for (const char* language : {"de", "en", "fr"}) {
    configManager.config.language = language;
    const auto& tr = i18n::strings(language);
    preload_pin_popup();
    assert(!is_pin_popup_visible());
    auto* context = g_ctx;
    auto* frame = shell.frame;
    auto* owner = hometiles_title::state_for(g_ctx->title_label);
    int callback_context = 7;
    for (const char* name : {"Radio", "Radio\nWohnzimmer", "A very long protected radio folder title", "Radio", tr.tile_type_settings}) {
      PinPopupInit init;
      // The header shows the protected tile's name and the state "Locked".
      init.title = name;
      init.icon_name = "radio";
      init.bg_color = 0x334455;
      init.hide_on_success = false;
      init.verify = verify_test; init.success = success_test; init.context = &callback_context;
      show_pin_popup(init);
      assert(g_ctx == context && shell.frame == frame && allocations == 1);
      assert(hometiles_title::state_for(g_ctx->title_label) == owner);
      assert(g_ctx->verify == verify_test && g_ctx->success == success_test &&
             g_ctx->callback_context == &callback_context && !g_ctx->hide_on_success);
      check_title(init.title);
      // Layout/style changes must not restore a stale full title.
      lv_obj_send_event(g_ctx->title_label, LV_EVENT_STYLE_CHANGED, nullptr);
      check_title(init.title);
      lv_refr_now(display);
      check_title(init.title);
      if (!strcmp(name, "Radio") && !strcmp(language, "de"))
        assert(!strcmp(lv_label_get_text(shell.title), "Radio"));
      assert(!strcmp(lv_label_get_text(shell.value), tr.pin_popup_locked) && "The header shows the state");
      check_layout(LAYOUT_NAME);
      check_prompt(tr);
      hide_pin_popup();
      assert(!is_pin_popup_visible() && !shell.active && !g_ctx->verify && !g_ctx->callback_context);
    }
    lv_obj_delete(g_ctx->overlay);
    assert(!g_ctx && allocations == 0);
  }
  lv_deinit();
  std::cout << "PIN preload, cached Radio/Settings titles, state header, prompt line, layout, DE/EN/FR, callback ownership and cleanup passed\n";
}
`);
// Every popup layout (as in test-editable-history-lvgl.mjs).
for (const [name, width, height, define] of [
  ['square', 480, 480, 'DEVICE_LAYOUT_480X480'], ['wide', 1024, 600, 'DEVICE_LAYOUT_1024X600'],
  ['ws8', 1280, 800, 'DEVICE_GUITION_JC8012P4A1_V2'], ['portrait', 720, 1280, ''], ['base', 720, 720, ''], ['landscape', 1280, 720, ''],
  ['compact-wide', 800, 480, 'DEVICE_LAYOUT_480X480'], ['tall', 480, 800, 'DEVICE_LAYOUT_480X480'],
]) {
  const binary = path.join(out, name + (process.platform === 'win32' ? '.exe' : ''));
  let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', `-DSCREEN_WIDTH=${width}`, `-DSCREEN_HEIGHT=${height}`, `-DLAYOUT_NAME="${name}"`,
    ...(define ? ['-D' + define] : []), source, host.archive, '-o', binary], {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stdout + result.stderr);
  result = spawnSync(binary, [], {encoding: 'utf8'});
  fs.writeFileSync(path.join(out, name + '.log'), result.stdout + result.stderr);
  assert.equal(result.status, 0, name + ': ' + result.stdout + result.stderr);
}
console.log('PIN popup: titles, state header, prompt line and keypad layout pass with real LVGL on every layout.');
