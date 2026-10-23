#pragma once

#include <stdint.h>
#include <type_traits>
#include <Arduino.h>
#include <lvgl.h>

struct MediaCoverRef {
  lv_image_dsc_t* dsc = nullptr;
  lv_image_dsc_t* popup_dsc = nullptr;
  String source_url;
  uint32_t url_hash = 0;
  // URL paired with the displayed MQTT pixels; their content hash is separate.
  uint32_t embedded_url_hash = 0;
  uint32_t requested_url_hash = 0;
  uint32_t failed_url_hash = 0;
  uint32_t failed_at_ms = 0;
  // The Bridge's picture of the current song (the state's "image_key",
  // bridge_images.h); empty without one.
  char image_key[17] = "";
};

struct MediaTileWidgets {
  lv_obj_t* cover_clip = nullptr;
  lv_obj_t* cover_image = nullptr;
  MediaCoverRef* cover_ref = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* previous_label = nullptr;
  lv_obj_t* play_pause_label = nullptr;
  lv_obj_t* next_label = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* media_title_label = nullptr;
  lv_obj_t* media_subtitle_label = nullptr;
  lv_obj_t* state_label = nullptr;
  // The playing bars over the title (playing_bars.h): shown while a song
  // plays or pauses, LV_STATE_CHECKED while it plays.
  lv_obj_t* bars = nullptr;
  uint32_t last_payload_hash = 0;
  uint32_t last_media_text_hash = 0;
  bool has_media_position = false;
  float media_position = 0.0f;
  float media_duration = 0.0f;
  uint32_t media_position_received_ms = 0;
  // The last report of the position (its value, Home Assistant's
  // media_position_updated_at in seconds since 1970, -1 without one, and
  // whether it played): a state that repeats it (a volume change) keeps the
  // running position (media_position.h).
  float media_position_reported = -1.0f;
  double media_position_updated = -1.0;
  bool media_position_playing = false;
  bool has_media_volume = false;
  float media_volume_level = 0.0f;
  bool media_is_muted = false;
  // False while Home Assistant reports the player unavailable: the controls
  // on the tile and in the popup are disabled, like in Home Assistant.
  bool available = true;
  bool dynamic_icon = true;
  // The state form the tile's icon shows (media_state_icon::Form in
  // state_icon.h, kept as a number so this header stays light); zero is
  // Plain, so a cleared cache entry is valid.
  uint8_t icon_form = 0;
};

// The page caches copy these with memcpy and clear them in place
// (TileWidgetCache): no String or other owning member (b523 put a String
// here, and clearing an 85 KB cache built a temporary on the 24 KB buildUI
// stack: a boot loop on V2).
static_assert(std::is_trivially_copyable<MediaTileWidgets>::value,
              "MediaTileWidgets is copied with memcpy by the page caches");
