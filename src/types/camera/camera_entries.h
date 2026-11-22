#pragma once

// A Camera tile's entries (user 2026-11-06, mockup webcam-radar/radar-tall.html):
// one source per tile, under it a list of Home Assistant cameras and images,
// rain radar places or webcams. The record lives in Tile::flow_entities
// (/_tile_flow_entities), one line per entry, tab separated:
//
//   <kind>\t<value>\t<own title>\t<own icon>\t<name>\t<line under the name>
//
//   h  a Home Assistant camera or image, value camera.x or image.x
//   r  a radar place, value zone.x or geo:<lat>,<lon> from the search
//   c  a radar place by own coordinates, value geo:<lat>,<lon>
//   w  a webcam, value its https address (a picture or a page)
//
// An empty own title takes <name>, an empty own icon the entity's. Every kind
// becomes one entity id; the Bridge creates the radar places and webcams
// itself when the panel declares them (camera_sources.py), so everything else
// on the panel keeps working with plain entity ids.
//
// No Arduino types, so tools/tests/tiles/test-camera-entries.mjs runs it
// unchanged. web/admin mirrors entity_of in types/camera/admin.js.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace camera_entries {

constexpr size_t kMaxEntries = 8;
constexpr size_t kMaxValue = 400;  // webcam_source.py URL_MAX_CHARS
constexpr size_t kMaxTitle = 64;   // camera_sources.py MAX_NAME_CHARS
constexpr size_t kMaxIcon = 48;
constexpr size_t kMaxLine = 64;
constexpr size_t kMaxRecord = 4096;  // tile_config.cpp kFlowEntityRecordMax

constexpr char kHomeAssistant = 'h';
constexpr char kRadarPlace = 'r';
constexpr char kRadarCoordinates = 'c';
constexpr char kWebcam = 'w';

struct Entry {
  char kind = kHomeAssistant;
  std::string value;
  std::string title;
  std::string icon;
  std::string name;
  std::string line;
  std::string entity;  // the entity id of kind and value
};

inline uint32_t fnv1a(const char* text) {
  uint32_t hash = 2166136261u;
  for (const char* c = text; *c; ++c) {
    hash ^= static_cast<uint8_t>(*c);
    hash *= 16777619u;
  }
  return hash;
}

// geo:<lat>,<lon> in range; a comma or a dot never appears inside a number here.
inline bool parse_coordinates(const std::string& value, double& lat, double& lon) {
  if (value.compare(0, 4, "geo:") != 0) return false;
  const char* start = value.c_str() + 4;
  char* end = nullptr;
  lat = std::strtod(start, &end);
  if (end == start || *end != ',') return false;
  const char* second = end + 1;
  lon = std::strtod(second, &end);
  if (end == second || *end) return false;
  return std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0;
}

inline std::string hundredths(double value) {
  const long number = std::lround(value * 100.0);
  char text[16];
  if (number < 0) std::snprintf(text, sizeof(text), "m%ld", -number);
  else std::snprintf(text, sizeof(text), "%ld", number);
  return text;
}

inline bool plain_part(const char* text) {
  if (!*text) return false;
  for (const char* c = text; *c; ++c) {
    const bool plain = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_';
    if (!plain) return false;
  }
  return true;
}

inline bool https_address(const std::string& value) {
  if (value.size() > kMaxValue || value.compare(0, 8, "https://") != 0 || value.size() <= 8) return false;
  for (char c : value) {
    if (static_cast<uint8_t>(c) <= ' ' || c == '"' || c == '\\') return false;
  }
  return true;
}

// The entity id of an entry, "" when its value names nothing.
inline std::string entity_of(char kind, const std::string& value) {
  char hash[16];
  double lat = 0, lon = 0;
  switch (kind) {
    case kHomeAssistant: {
      const bool camera = value.compare(0, 7, "camera.") == 0;
      const bool image = value.compare(0, 6, "image.") == 0;
      if ((!camera && !image) || !plain_part(value.c_str() + (camera ? 7 : 6))) return "";
      return value;
    }
    case kRadarPlace:
      if (value == "zone.home") return "camera.ht_radar_home";
      if (value.compare(0, 5, "zone.") == 0) {
        const char* zone = value.c_str() + 5;
        if (!plain_part(zone)) return "";
        if (std::strlen(zone) <= 43) return "camera.ht_radar_zone_" + std::string(zone);
        // The Bridge takes object ids up to 48 characters.
        std::snprintf(hash, sizeof(hash), "%08lx", static_cast<unsigned long>(fnv1a(zone)));
        return "camera.ht_radar_zone_" + std::string(hash);
      }
      [[fallthrough]];
    case kRadarCoordinates:
      if (!parse_coordinates(value, lat, lon)) return "";
      return "camera.ht_radar_" + hundredths(lat) + "_" + hundredths(lon);
    case kWebcam:
      if (!https_address(value)) return "";
      std::snprintf(hash, sizeof(hash), "%08lx", static_cast<unsigned long>(fnv1a(value.c_str())));
      return "camera.ht_cam_" + std::string(hash);
    default:
      return "";
  }
}

// A field without control characters, cut at a whole UTF-8 character.
inline std::string clean_field(const std::string& text, size_t limit) {
  std::string out;
  for (char c : text) {
    if (static_cast<uint8_t>(c) >= ' ') out += c;
  }
  if (out.size() > limit) {
    size_t cut = limit;
    while (cut > 0 && (static_cast<uint8_t>(out[cut]) & 0xC0) == 0x80) --cut;
    out.resize(cut);
  }
  return out;
}

// The valid entries of a record, each entity once, at most kMaxEntries.
inline std::vector<Entry> parse(const char* record) {
  std::vector<Entry> entries;
  const char* line = record ? record : "";
  while (*line && entries.size() < kMaxEntries) {
    const char* end = std::strchr(line, '\n');
    const size_t length = end ? static_cast<size_t>(end - line) : std::strlen(line);
    std::string fields[6];
    size_t field = 0;
    for (size_t i = 0; i < length; ++i) {
      if (line[i] == '\t') {
        if (++field == 6) break;
      } else {
        fields[field] += line[i];
      }
    }
    line += length + (end ? 1 : 0);
    Entry entry;
    entry.kind = fields[0].size() == 1 ? fields[0][0] : '\0';
    entry.value = fields[1];
    entry.entity = entity_of(entry.kind, entry.value);
    if (entry.entity.empty()) continue;
    bool known = false;
    for (const Entry& other : entries) known = known || other.entity == entry.entity;
    if (known) continue;
    entry.title = clean_field(fields[2], kMaxTitle);
    entry.icon = clean_field(fields[3], kMaxIcon);
    entry.name = clean_field(fields[4], kMaxTitle);
    entry.line = clean_field(fields[5], kMaxLine);
    entries.push_back(entry);
  }
  return entries;
}

// The record again from its valid entries ("" with none); what would not fit
// kMaxRecord is left out.
inline std::string normalize(const char* record) {
  std::string out;
  for (const Entry& entry : parse(record)) {
    std::string line(1, entry.kind);
    for (const std::string* field : {&entry.value, &entry.title, &entry.icon, &entry.name, &entry.line}) {
      line += '\t';
      line += *field;
    }
    if (out.size() + line.size() + 1 > kMaxRecord) break;
    if (!out.empty()) out += '\n';
    out += line;
  }
  return out;
}

// The entry's position by its entity id, 0 without it.
inline size_t index_of(const std::vector<Entry>& entries, const char* entity) {
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].entity == (entity ? entity : "")) return i;
  }
  return 0;
}

inline bool is_bridge_source(const Entry& entry) { return entry.kind != kHomeAssistant; }

inline void append_json_string(std::string& out, const std::string& text) {
  out += '"';
  for (char c : text) {
    if (c == '"' || c == '\\') out += '\\';
    if (static_cast<uint8_t>(c) >= ' ') out += c;
  }
  out += '"';
}

// The Bridge's declaration item of a radar place or webcam ("" for a Home
// Assistant entity): camera_sources.py clean_source.
inline std::string source_json(const Entry& entry) {
  if (!is_bridge_source(entry)) return "";
  const std::string id = entry.entity.substr(7);  // after "camera."
  if (id == "ht_radar_home") return "{\"id\":\"ht_radar_home\"}";
  std::string out = "{\"id\":\"" + id + "\",";
  if (entry.kind == kWebcam) {
    out += "\"url\":";
    append_json_string(out, entry.value);
  } else if (entry.value.compare(0, 5, "zone.") == 0) {
    out += "\"zone\":\"" + entry.value + "\"";
  } else {
    double lat = 0, lon = 0;
    if (!parse_coordinates(entry.value, lat, lon)) return "";
    char numbers[48];
    std::snprintf(numbers, sizeof(numbers), "\"lat\":%.4f,\"lon\":%.4f", lat, lon);
    out += numbers;
  }
  const std::string& name = entry.title.empty() ? entry.name : entry.title;
  if (!name.empty()) {
    out += ",\"n\":";
    append_json_string(out, name);
  }
  out += '}';
  return out;
}

}  // namespace camera_entries
