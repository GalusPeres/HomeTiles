#pragma once

#include <Arduino.h>
#include "src/core/json_scan.h"

namespace media_artwork {

inline bool has_fields(const char* json) {
  const int length = strlen(json);
  return hometiles_json::valueOffset(json, length, "entity_picture_data") >= 0 ||
         hometiles_json::valueOffset(json, length, "entity_picture") >= 0 ||
         hometiles_json::valueOffset(json, length, "media_image_url") >= 0;
}

inline bool read_string(const char* json, const char* key, String& out) {
  int begin = 0, end = 0;
  // A null must not consume the next quoted JSON key as a URL or Base64 data.
  if (!hometiles_json::stringSpan(json, strlen(json), key, &begin, &end)) return false;
  out = String(json + begin, end - begin);
  out.trim();
  return true;
}

}  // namespace media_artwork
