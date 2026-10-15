#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Scene tile pictures: JPEG files on the panel's storage (/icons, uploaded by
// the Web Admin through /api/upload_icon). Without a title the picture fills
// the tile; with a title it takes the icon's place.
//
// Decoded pictures are cached (bounded, PSRAM) and shared by every card that
// shows them, so page turns and grid rebuilds do not decode again. A picture
// is reduced by an area average to about its tile's size (LVGL's TJpgDec has
// no decode-time scaling), so a large upload keeps only what its tile needs.
namespace scene_picture {

// Directory and the only accepted file names: "/icons/<name>.jpg|.jpeg",
// no further slashes, no "..", short enough for every storage.
constexpr const char* kDir = "/icons";
constexpr size_t kMaxPathLength = 63;
bool valid_path(const String& path);

// A decoded picture of at least min_w x min_h pixels when the source is that
// large (cover: the card scales it to fill), or at most max_side on its
// longest side when max_side > 0 (icon: shown contained). Returns nullptr when
// the file is missing, not a baseline JPEG or too large. Every non-null result
// must be handed back with release() once its image object is deleted.
const lv_image_dsc_t* acquire(const String& path, uint16_t min_w, uint16_t min_h, uint16_t max_side);
void release(const lv_image_dsc_t* dsc);

// LV_EVENT_DELETE callback that releases the picture passed as user data.
void release_on_delete(lv_event_t* e);

}  // namespace scene_picture
