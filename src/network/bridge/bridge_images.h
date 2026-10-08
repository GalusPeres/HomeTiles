#pragma once

#include <Arduino.h>

#include "src/network/bridge/bridge_images_core.h"

// Pictures from the Bridge over the direct link (docs-dev/images.md): a
// Media tile subscribes to its player's cover in the size the Media popup
// shows it; the Bridge renders and sends it, keyed like the player state's
// "image_key". Loop task only.
namespace bridge_images {

struct Picture {
  char entity_id[72] = "";
  char key[kKeyLength + 1] = "";
  uint8_t* jpeg = nullptr;  // PSRAM
  size_t length = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t used = 0;
};

// Edge of a media player's cover picture: the Media popup's cover.
uint16_t coverEdge();
// The statestream route suffix of a cover picture, "image/<edge>x<edge>".
const char* coverSuffix();

// A message on a picture topic: stored (or dropped when malformed), and the
// player's tiles told. False for any other topic.
bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length);

// The newest picture of a player; nullptr without one.
const Picture* find(const char* entity_id);

}  // namespace bridge_images
