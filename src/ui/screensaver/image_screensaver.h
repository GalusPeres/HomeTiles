#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "src/tiles/config/tile_config.h"

// Global screensaver mode. Configuration lives in screensaver_config,
// independently of clock tiles; tapping any clock tile opens this same
// mode.
void show_image_screensaver();
void hide_image_screensaver();
bool is_image_screensaver_visible();
// The visible screensaver's overlay (its tiles, clock and picture inside),
// else nullptr: the panel's swipe back to Home starts on it.
lv_obj_t* image_screensaver_overlay();
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

// A picture of an image or camera entity arrived from the Bridge (or was
// cleared; bridge_images.h): the screensaver showing that entity shows it,
// a hidden one decodes it ahead.
void image_screensaver_picture_arrived(const char* entity_id);

// A Bridge picture for a Camera tile (types/camera/picture.cpp), decoded with
// the screensaver's decoders so tiles share its hardware JPEG engine (P4) and
// its PSRAM budget: native RGB565 (LV_COLOR_FORMAT_RGB565) of the JPEG's own
// size, in a malloc buffer the caller frees; nullptr when it cannot be
// decoded. want_w x want_h is the size the picture should have.
uint16_t* image_screensaver_decode_picture(const uint8_t* data, size_t len, uint16_t want_w,
                                           uint16_t want_h, uint16_t& out_w, uint16_t& out_h);

// Separate live refresh for settings that restyle every tile (default
// color, icon glow) or a failed save. Rebuild only the small screensaver
// grid, preserving the image and clock.
void image_screensaver_tiles_changed();

// A Web Admin tile edit or move (loopTask, like LVGL): an open screensaver
// rebuilds only the changed slots and draws them at once, before the flash
// write. False when no screensaver is open.
bool image_screensaver_show_tiles_now();

// Scenes in the bottom row use the same existing publish function as
// normal tiles.
void image_screensaver_set_scene_callback(void (*callback)(const char*));

// The tile renderer needs the matching slot configuration for its
// separate screensaver widget context; no special MQTT handling.
const Tile* image_screensaver_get_slot_tile(uint8_t index);
