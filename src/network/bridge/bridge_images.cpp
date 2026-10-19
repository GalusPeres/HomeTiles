#include "src/network/bridge/bridge_images.h"

#include <esp_heap_caps.h>

#include "src/network/mqtt/mqtt_topics.h"
#include "src/tiles/config/grid_layout.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/ui/popups/media/media_popup_layout.h"
#include "src/ui/popups/popup_layout.h"
#include "src/types/camera/picture.h"
#include "src/ui/screensaver/image_screensaver.h"

namespace bridge_images {
namespace {

// A few players show covers, the screensaver one picture and Camera tiles
// theirs; the oldest picture makes room. A tile keeps its decoded pixels, so
// it needs its JPEG only until it has decoded it.
constexpr size_t kPictures = 12;
Picture g_pictures[kPictures];
uint32_t g_tick = 0;

void release(Picture& picture) {
  if (picture.jpeg) heap_caps_free(picture.jpeg);
  picture = Picture();
}

bool same(const Picture& picture, const char* entity_id, uint16_t width, uint16_t height, uint8_t fit) {
  return picture.jpeg && strcmp(picture.entity_id, entity_id) == 0 && picture.width == width &&
         picture.height == height && picture.fit == fit;
}

Picture* slotFor(const char* entity_id, uint16_t width, uint16_t height, uint8_t fit) {
  for (Picture& picture : g_pictures) {
    if (same(picture, entity_id, width, height, fit)) return &picture;
  }
  Picture* oldest = &g_pictures[0];
  for (Picture& picture : g_pictures) {
    if (!picture.jpeg) return &picture;
    if (picture.used < oldest->used) oldest = &picture;
  }
  return oldest;
}

// A cover goes to the player's tiles, an image or camera picture to the
// screensaver and the Camera tiles.
void announce(const char* entity) {
  if (strncmp(entity, "media_player.", 13) == 0) {
    tile_renderer_media_picture_arrived(entity);
  } else {
    image_screensaver_picture_arrived(entity);
    camera_picture::arrived(entity);
  }
}

}  // namespace

uint16_t coverEdge() {
  // The Media popup's larger cover ("Cover links", media_popup_layout.h):
  // it shows as it arrives, the centred cover and the tiles take a smaller
  // copy of it.
  return static_cast<uint16_t>(media_popup_layout::kLeftCoverSide);
}

const char* coverSuffix() {
  static char suffix[24] = "";
  if (!suffix[0]) snprintf(suffix, sizeof(suffix), "image/%ux%u", coverEdge(), coverEdge());
  return suffix;
}

const char* screenSuffix(uint8_t fit, uint8_t every) {
  static char suffix[40] = "";
  pictureSuffix(suffix, sizeof(suffix), static_cast<uint16_t>(grid_layout::screen_w()),
                static_cast<uint16_t>(grid_layout::screen_h()), fit, every);
  return suffix;
}

bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length) {
  const String& configured = mqttTopics.haPrefix();
  const char* prefix = configured.length() ? configured.c_str() : "ha/statestream";
  char entity[sizeof(Picture::entity_id)];
  uint16_t width = 0, height = 0;
  uint8_t fit = kFill;
  if (!entityFromTopic(topic, prefix, entity, sizeof(entity), &width, &height, &fit)) return false;

  if (length == 0) {
    for (Picture& picture : g_pictures) {
      if (same(picture, entity, width, height, fit)) release(picture);
    }
    announce(entity);
    return true;
  }
  Header header;
  // The Bridge renders exactly the size the topic asks for.
  if (!parseHeader(payload, length, header) || header.width != width || header.height != height) {
    Serial.printf("[Images] Malformed picture dropped (%u bytes): %s\n",
                  static_cast<unsigned>(length), topic);
    return true;
  }
  const size_t jpeg_length = length - header.jpeg_offset;
  uint8_t* jpeg = static_cast<uint8_t*>(
      heap_caps_malloc(jpeg_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!jpeg) {
    Serial.printf("[Images] No memory for %s (%u bytes)\n", entity, static_cast<unsigned>(jpeg_length));
    return true;
  }
  memcpy(jpeg, payload + header.jpeg_offset, jpeg_length);
  Picture* slot = slotFor(entity, width, height, fit);
  release(*slot);
  strlcpy(slot->entity_id, entity, sizeof(slot->entity_id));
  memcpy(slot->key, header.key, sizeof(slot->key));
  slot->jpeg = jpeg;
  slot->length = jpeg_length;
  slot->width = header.width;
  slot->height = header.height;
  slot->fit = fit;
  slot->used = ++g_tick;
  Serial.printf("[Images] %s %s %ux%u, %u bytes | int=%u KB min=%u KB\n", entity, header.key,
                header.width, header.height, static_cast<unsigned>(jpeg_length),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
  announce(entity);
  return true;
}

const Picture* find(const char* entity_id) {
  if (!entity_id) return nullptr;
  Picture* newest = nullptr;
  for (Picture& picture : g_pictures) {
    if (picture.jpeg && strcasecmp(picture.entity_id, entity_id) == 0 &&
        (!newest || picture.used > newest->used)) {
      newest = &picture;
    }
  }
  if (newest) newest->used = ++g_tick;
  return newest;
}

const Picture* find(const char* entity_id, uint16_t width, uint16_t height, uint8_t fit) {
  if (!entity_id) return nullptr;
  for (Picture& picture : g_pictures) {
    if (picture.jpeg && strcasecmp(picture.entity_id, entity_id) == 0 && picture.width == width &&
        picture.height == height && picture.fit == fit) {
      picture.used = ++g_tick;
      return &picture;
    }
  }
  return nullptr;
}

}  // namespace bridge_images
