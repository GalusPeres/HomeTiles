#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

#include "src/core/i18n/i18n.h"

// What the Settings screen shows and does, apart from its look. The firmware
// reads and saves the real configuration and state (tab_settings.cpp); the
// host preview (tools/settings-preview.mjs) fills in the mockup's example
// values, so it renders exactly the screen code the panel runs.
// settings_screen.cpp only draws and reports touches through these.
namespace settings_model {

const i18n::Strings& text();
// The global tile color: the card and every surface on it.
uint32_t card_color();

// ---------- Display page ----------
struct DisplayValues {
  int brightness;  // the current backlight, percent
  int brightness_min;
  int brightness_max;
  int sleep_index;  // sleep_steps() = never
  int saver_index;
  int saver_brightness;
  int saver_brightness_min;
  int saver_brightness_max;
};
DisplayValues display_values();
// The stored brightness and sleep step (the Display category line).
int saved_brightness();
int saved_sleep_index();
// The step index that means "never"; the steps before it are timeouts.
int sleep_steps();
unsigned sleep_step_seconds(int index);
// Quarter turns (0, 90, 180, 270) or normal and flipped.
bool quarter_turns();
uint8_t rotation_index();
// Touches: `final` once the finger lifts (saved then).
void brightness_changed(int percent, bool final);
void sleep_changed(int index, bool final);
void saver_changed(int index, bool final);
void saver_brightness_changed(int percent, bool final);
void rotation_selected(uint8_t index);

// ---------- Category lines ----------
bool access_point_on();
bool network_connected();
void network_name(char* buf, size_t len);
const char* language_name();
// The time in the selected format; false without a valid time.
bool time_text(char* buf, size_t len);
bool update_available();
const char* firmware_version();

// ---------- Navigation ----------
void close_settings();
// WiFi (1), Localization (2) and System (3) still open their popups until
// their pages follow.
void open_category_popup(uint8_t category, lv_event_t* e);

}  // namespace settings_model
