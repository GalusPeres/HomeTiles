#pragma once

// Pictures from the Bridge over the direct link (docs-dev/images.md): the
// platform-independent parts, run by tools/tests/network/test-bridge-images.mjs.
// A picture message on "<ha_prefix>/media_player/<object>/image/<w>x<h>" is
//   "HTIMG1 <16 hex key> <w>x<h>\n" followed by a baseline JPEG of w x h;
// an empty message clears the picture.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace bridge_images {

constexpr size_t kKeyLength = 16;
constexpr uint16_t kMinEdge = 16;
constexpr uint16_t kMaxEdge = 1280;

struct Header {
  char key[kKeyLength + 1] = "";
  uint16_t width = 0;
  uint16_t height = 0;
  size_t jpeg_offset = 0;
};

// Reads "<digits>" up to `end`; false without digits, with a leading zero
// or above kMaxEdge.
inline bool readEdge(const char*& p, const char* end, uint16_t& out) {
  if (p >= end || *p < '1' || *p > '9') return false;
  uint32_t value = 0;
  while (p < end && *p >= '0' && *p <= '9') {
    value = value * 10 + static_cast<uint32_t>(*p - '0');
    if (value > kMaxEdge) return false;
    ++p;
  }
  if (value < kMinEdge) return false;
  out = static_cast<uint16_t>(value);
  return true;
}

// "<w>x<h>" filling exactly [p, end).
inline bool readSize(const char* p, const char* end, uint16_t& w, uint16_t& h) {
  return readEdge(p, end, w) && p < end && *p++ == 'x' && readEdge(p, end, h) && p == end;
}

inline bool parseHeader(const uint8_t* data, size_t length, Header& out) {
  static const char kMagic[] = "HTIMG1 ";
  const size_t magic = sizeof(kMagic) - 1;
  if (!data || length < magic + kKeyLength + 1 || memcmp(data, kMagic, magic) != 0) return false;
  const char* text = reinterpret_cast<const char*>(data);
  const size_t scan = length < 64 ? length : 64;
  const char* newline = static_cast<const char*>(memchr(text, '\n', scan));
  if (!newline) return false;
  const char* key = text + magic;
  for (size_t i = 0; i < kKeyLength; ++i) {
    const char c = key[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  if (key[kKeyLength] != ' ') return false;
  if (!readSize(key + kKeyLength + 1, newline, out.width, out.height)) return false;
  memcpy(out.key, key, kKeyLength);
  out.key[kKeyLength] = '\0';
  out.jpeg_offset = static_cast<size_t>(newline - text) + 1;
  // A JPEG starts with FF D8.
  return length >= out.jpeg_offset + 2 && data[out.jpeg_offset] == 0xFF &&
         data[out.jpeg_offset + 1] == 0xD8;
}

// The media player of a picture topic below `prefix`: "media_player.<object>"
// and the picture's size; false for any other topic.
inline bool entityFromTopic(const char* topic, const char* prefix, char* entity, size_t entity_size,
                            uint16_t* width, uint16_t* height) {
  static const char kDomain[] = "/media_player/";
  static const char kImage[] = "/image/";
  if (!topic || !prefix || !entity || entity_size == 0) return false;
  const size_t prefix_length = strlen(prefix);
  if (prefix_length == 0 || strncmp(topic, prefix, prefix_length) != 0) return false;
  const char* p = topic + prefix_length;
  if (strncmp(p, kDomain, sizeof(kDomain) - 1) != 0) return false;
  const char* object = p + sizeof(kDomain) - 1;
  const char* image = strstr(object, kImage);
  if (!image || image == object) return false;
  for (const char* c = object; c < image; ++c) {
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) return false;
  }
  const char* size = image + sizeof(kImage) - 1;
  uint16_t w = 0, h = 0;
  if (!readSize(size, size + strlen(size), w, h)) return false;
  const size_t object_length = static_cast<size_t>(image - object);
  static const char kEntityDomain[] = "media_player.";
  if (sizeof(kEntityDomain) - 1 + object_length + 1 > entity_size) return false;
  memcpy(entity, kEntityDomain, sizeof(kEntityDomain) - 1);
  memcpy(entity + sizeof(kEntityDomain) - 1, object, object_length);
  entity[sizeof(kEntityDomain) - 1 + object_length] = '\0';
  if (width) *width = w;
  if (height) *height = h;
  return true;
}

}  // namespace bridge_images
