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
struct Config { const char* language = "de"; bool tile_borders = true; };
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
${stripIncludes(read('src/ui/shared/ui_surface_style.h'))}
${stripIncludes(read('src/ui/shared/ui_surface_style.cpp'))}
${stripIncludes(read('src/ui/popups/popup_layout.h'))}
${stripIncludes(read('src/ui/popups/popup_open.h'))}
${stripIncludes(read('src/ui/popups/popup_shell.h'))}
${stripIncludes(read('src/ui/popups/popup_open.cpp'))}
${stripIncludes(read('src/ui/popups/popup_shell.cpp'))}
${stripIncludes(read('src/core/config/pin_access.h'))}
namespace pin_access {
${definition('src/core/config/pin_access.cpp', 'secureClear')}
}
${definition('src/tiles/runtime/tile_renderer_shared.h', 'disable_pressed_button_animation')}
void hide_light_popup() {} void hide_climate_popup() {} void hide_cover_popup() {}
void hide_sensor_popup() {} void hide_weather_popup() {} void hide_energy_popup() {}
void hide_media_popup() {} void hide_camera_popup() {}
namespace pin_test {
${stripIncludes(read('src/ui/popups/pin/pin_popup.h'))}
${stripIncludes(read('src/ui/popups/pin/pin_popup.cpp'))}
}
using namespace pin_test;
${definition('src/ui/ui_manager.cpp', 'make_unlock_title')}
bool verify_test(const char*, void*) { return false; }
void success_test(void*) {}

void check_title(const String& expected) {
  lv_obj_update_layout(shell.overlay);
  sync_popup_shell();
  assert(!strcmp(hometiles_title::text(g_ctx->title_label), expected.c_str()) &&
         "Reused PIN popup must replace the preloaded Settings title owner");
  assert(!strcmp(hometiles_title::text(shell.title), expected.c_str()) &&
         "Visible shared header must use the current protected tile title");
  assert(!strcmp(lv_label_get_text(shell.title), lv_label_get_text(g_ctx->title_label)));
}
int main() {
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
      init.title = make_unlock_title(tr.pin_popup_unlock_format, name);
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
        assert(!strcmp(lv_label_get_text(shell.title), "Radio entsperren"));
      hide_pin_popup();
      assert(!is_pin_popup_visible() && !shell.active && !g_ctx->verify && !g_ctx->callback_context);
    }
    lv_obj_delete(g_ctx->overlay);
    assert(!g_ctx && allocations == 0);
  }
  lv_deinit();
  std::cout << "PIN preload, cached Radio/Settings titles, two lines, ellipsis, DE/EN/FR, callback ownership and cleanup passed\n";
}
`);
for (const [name, width, height, define] of [
  ['s3', 480, 480, 'DEVICE_LAYOUT_480X480'],
  ['4b', 720, 720, ''],
  ['tab5', 1280, 720, ''],
]) {
  const binary = path.join(out, name + (process.platform === 'win32' ? '.exe' : ''));
  let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', `-DSCREEN_WIDTH=${width}`, `-DSCREEN_HEIGHT=${height}`,
    ...(define ? ['-D' + define] : []), source, host.archive, '-o', binary], {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stdout + result.stderr);
  result = spawnSync(binary, [], {encoding: 'utf8'});
  fs.writeFileSync(path.join(out, name + '.log'), result.stdout + result.stderr);
  assert.equal(result.status, 0, name + ': ' + result.stdout + result.stderr);
}
console.log('Cached PIN titles follow the protected tile with real LVGL on S3, 4B and Tab5.');
