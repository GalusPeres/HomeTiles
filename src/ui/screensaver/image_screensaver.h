#pragma once

#include <Arduino.h>

#include "src/tiles/config/tile_config.h"

// Global screensaver mode. Configuration lives in screensaver_config,
// independently of clock tiles; tapping any clock tile opens this same
// mode.
void show_image_screensaver();
void hide_image_screensaver();
bool is_image_screensaver_visible();
// True from the start of show_image_screensaver() (its overlay exists before
// the setup finishes) until hide_image_screensaver().
bool image_screensaver_covers_ui();
void image_screensaver_brightness_changed();

// Load the first configured image into the PSRAM cache after a delay.
void preload_image_screensaver();

// Call from the main loop. Start the screensaver after the inactivity
// period configured in display settings.
void service_image_screensaver_auto(uint32_t last_activity_ms);

// After an Admin save, an open view applies image/clock changes on the
// next LVGL tick without closing its overlay. preview_wallpaper is only
// the currently selected editor preview and is not persisted.
void image_screensaver_config_changed(
    const String& preview_wallpaper = String());

// Separate live refresh for tile saves/drag-and-drop. Rebuild only the
// small screensaver grid, preserving the image and clock.
void image_screensaver_tiles_changed();

// Scenes in the bottom row use the same existing publish function as
// normal tiles.
void image_screensaver_set_scene_callback(void (*callback)(const char*));

// The tile renderer needs the matching slot configuration for its
// separate screensaver widget context; no special MQTT handling.
const Tile* image_screensaver_get_slot_tile(uint8_t index);
