#include "src/network/bridge/bridge_images.h"

#include <esp_heap_caps.h>

#include "src/network/mqtt/mqtt_topics.h"
#include "src/tiles/config/grid_layout.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/screensaver/image_screensaver.h"

namespace bridge_images {
namespace {

// A few players at most show covers, and the screensaver one picture; the
// oldest picture makes room.
constexpr size_t kPictures = 6;
Picture g_pictures[kPictures];
uint32_t g_tick = 0;

void release(Picture& picture) {
  if (picture.jpeg) heap_caps_free(picture.jpeg);
  picture = Picture();
}

Picture* slotFor(const char* entity_id) {
  Picture* oldest = &g_pictures[0];
  for (Picture& picture : g_pictures) {
    if (picture.jpeg && strcmp(picture.entity_id, entity_id) == 0) return &picture;
    if (!picture.jpeg) return &picture;
    if (picture.used < oldest->used) oldest = &picture;
  }
  return oldest;
}

// A cover goes to the player's tiles, an image or camera picture to the
// screensaver.
void announce(const char* entity) {
  if (strncmp(entity, "media_player.", 13) == 0) {
    tile_renderer_media_picture_arrived(entity);
  } else {
    image_screensaver_picture_arrived(entity);
  }
}

}  // namespace

uint16_t coverEdge() {
  // The Media popup's cover (media_popup.cpp kCoverSize); tiles show at
  // most this much of it.
  return static_cast<uint16_t>(popup_layout::scale(240));
}

const char* coverSuffix() {
  static char suffix[24] = "";
  if (!suffix[0]) snprintf(suffix, sizeof(suffix), "image/%ux%u", coverEdge(), coverEdge());
  return suffix;
}

const char* screenSuffix() {
  static char suffix[24] = "";
  snprintf(suffix, sizeof(suffix), "image/%ux%u", static_cast<unsigned>(grid_layout::screen_w()),
           static_cast<unsigned>(grid_layout::screen_h()));
  return suffix;
}

bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length) {
  const String& configured = mqttTopics.haPrefix();
  const char* prefix = configured.length() ? configured.c_str() : "ha/statestream";
  char entity[sizeof(Picture::entity_id)];
  if (!entityFromTopic(topic, prefix, entity, sizeof(entity), nullptr, nullptr)) return false;

  if (length == 0) {
    for (Picture& picture : g_pictures) {
      if (picture.jpeg && strcmp(picture.entity_id, entity) == 0) release(picture);
    }
    announce(entity);
    return true;
  }
  Header header;
  if (!parseHeader(payload, length, header)) {
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
  Picture* slot = slotFor(entity);
  release(*slot);
  strlcpy(slot->entity_id, entity, sizeof(slot->entity_id));
  memcpy(slot->key, header.key, sizeof(slot->key));
  slot->jpeg = jpeg;
  slot->length = jpeg_length;
  slot->width = header.width;
  slot->height = header.height;
  slot->used = ++g_tick;
  Serial.printf("[Images] %s %s %ux%u, %u bytes\n", entity, header.key, header.width,
                header.height, static_cast<unsigned>(jpeg_length));
  announce(entity);
  return true;
}

const Picture* find(const char* entity_id) {
  if (!entity_id) return nullptr;
  for (Picture& picture : g_pictures) {
    if (picture.jpeg && strcasecmp(picture.entity_id, entity_id) == 0) {
      picture.used = ++g_tick;
      return &picture;
    }
  }
  return nullptr;
}

}  // namespace bridge_images
