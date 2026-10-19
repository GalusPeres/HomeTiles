#pragma once

#include <Arduino.h>
#include <lvgl.h>

struct MediaPopupInit {
  String entity_id;
  String title;
  String icon_name;
  String icon_char;
  String media_title;
  String media_subtitle;
  bool is_playing = false;
  bool has_media_position = false;
  float media_position = 0.0f;
  float media_duration = 0.0f;
  uint32_t media_position_received_ms = 0;
  bool has_volume = false;
  float volume_level = 0.0f;
  bool is_muted = false;
  // False while the player is unavailable: the controls are disabled.
  bool available = true;
  uint32_t bg_color = 0;
  // The tile icon's current color: the header icon and its disc glow take it.
  uint32_t icon_color = 0xFFFFFF;
  const lv_image_dsc_t* cover_dsc = nullptr;
  uint32_t cover_hash = 0;
  // The tile's popup layout (mediaPopupCoverCentered): the cover centred
  // instead of left beside the title.
  bool cover_centered = false;
};

void show_media_popup(const MediaPopupInit& init);
void preload_media_popup();
// True while the Media popup is open for this entity.
bool media_popup_showing(const String& entity_id);
void update_media_popup(const MediaPopupInit& init);
void update_media_popup_cover(const char* entity_id, const lv_image_dsc_t* cover_dsc, uint32_t cover_hash);
void hide_media_popup();
// Remembers the player fields only the popup shows (feature bits, shuffle,
// repeat, sources and sound modes, the group, artist and album) from a media
// state payload (tile_renderer.cpp update_media_tile_state), also while the
// popup is closed. Bounded to a few players.
void media_popup_note_state(const String& entity_id, const char* payload);
// A library thumbnail answer on {base}/stat/media/thumb (mqtt_handlers.cpp).
void media_popup_handle_browse_thumbnail(const char* payload, size_t length);
