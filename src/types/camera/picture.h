#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "src/tiles/config/tile_config.h"
#include "src/network/bridge/bridge_images_core.h"
#include "src/types/camera/picture_layout.h"

// A Camera tile can show the Bridge's picture of its entity instead of the
// icon (#50): a camera's still at an interval, an image entity's picture as
// it changes. The options live in fields the Camera tile does not use
// otherwise, so Tile and PackedTileV7 stay unchanged:
//   sensor_display_mode  1 = the picture, 0 = the icon (as before)
//   sensor_decimals      its placement (bridge_images::Fit; 0xFF = fill)
//   image_slideshow_sec  a camera's interval, 3..60 s
//   key_code             transparency of the circle and the title pill over
//                        the picture in percent (0 opaque .. 100 none)
//   key_modifier         bit 0: a tap opens the camera straight in full
//                        screen, and a tap there closes it back to the tiles
//                        (icon look too); bits 1-2: circle and title
//                        follow the design (0), light (1) or dark (2); bit 3:
//                        with several entries the tile shows the one last
//                        chosen in the popup (RAM only: the first after a
//                        restart)
// Over the direct link only; without it, at half height and until the first
// picture arrives the tile shows its icon and title as before.
namespace camera_picture {

constexpr uint8_t kShowIcon = 0;
constexpr uint8_t kShowPicture = 1;
constexpr uint8_t kTransparencyMax = 100;
constexpr uint8_t kTapFullScreen = 0x01;
constexpr uint8_t kHeadLight = 0x02;
constexpr uint8_t kHeadDark = 0x04;
constexpr uint8_t kLastChosen = 0x08;
// The title's distance to the visible end of its pill (480 scale).
constexpr int kTitlePad = 12;

inline bool is_image(const String& entity) { return entity.startsWith("image."); }
inline bool is_camera(const String& entity) { return entity.startsWith("camera."); }

inline bool wanted(const Tile& tile) {
  return tile.type == TILE_CAMERA && tile.sensor_display_mode == kShowPicture &&
         (is_camera(tile.sensor_entity) || is_image(tile.sensor_entity));
}

inline uint8_t fit(const Tile& tile) {
  return tile.sensor_decimals <= bridge_images::kOriginal ? tile.sensor_decimals : bridge_images::kFill;
}

inline uint8_t every(const Tile& tile) {
  const uint16_t seconds = tile.image_slideshow_sec;
  if (seconds < bridge_images::kEveryMin) return bridge_images::kEveryMin;
  if (seconds > bridge_images::kEveryMax) return bridge_images::kEveryMax;
  return static_cast<uint8_t>(seconds);
}

inline uint8_t transparency(const Tile& tile) {
  return tile.key_code > kTransparencyMax ? kTransparencyMax : tile.key_code;
}

// A tap opens the entity straight in full screen.
inline bool tap_full_screen(const Tile& tile) {
  return tile.type == TILE_CAMERA && (tile.key_modifier & kTapFullScreen) &&
         (is_camera(tile.sensor_entity) || is_image(tile.sensor_entity));
}

inline bool last_chosen(const Tile& tile) { return (tile.key_modifier & kLastChosen) != 0; }

// The head over the picture (user 2026-10-11: "Automatisch / Dunkel / Hell",
// no switch at a transparency, Automatisch by default): Automatisch follows
// the design like a tile in the global color, also when the design changes;
// Dunkel and Hell draw on that surface in either design (tile_renderer.cpp
// tile_surface_of). Its card takes the design's tile color, never an own
// color. At full height, where the picture can show.
bool head_over_picture(const Tile& tile);
inline bool light_head(const Tile& tile) { return (tile.key_modifier & kHeadLight) != 0; }
inline bool dark_head(const Tile& tile) { return !light_head(tile) && (tile.key_modifier & kHeadDark) != 0; }

// The title's width for the head rule in `font` (camera_picture_layout::
// title_metric with the font's whole-pixel advances).
int title_metric(const lv_font_t* font, const char* text);

// The Camera renderer hands a picture tile over once its card, icon and
// title exist. A rebuilt tile (a Web Admin change) shows its last pixels at
// once, a picture that waits is decoded right away; otherwise the icon look
// stays until the first picture. The tile keeps its own decoded pixels;
// everything is released with the card.
// `entity` is the entry the tile shows, the tile's own or one chosen in the popup.
void attach(lv_obj_t* card, lv_obj_t* icon, lv_obj_t* title, const Tile& tile, const String& entity);

// Another entry on a picture tile (Zuletzt gewaehlt): its title is set already.
// The old picture stays until the new one is decoded.
void show_entity(lv_obj_t* card, const String& entity);

// A picture of `entity_id` arrived or was cleared (bridge_images.cpp).
void arrived(const char* entity_id);

// Loop task: keeps the picture subscriptions to the tiles that show them (a
// camera only while its tile is on screen and the display is awake) and
// decodes at most one arrived picture per call.
void service();

}  // namespace camera_picture
