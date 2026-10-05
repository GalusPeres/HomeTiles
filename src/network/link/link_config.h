#pragma once

#include <Arduino.h>

// Stored address of the HomeTiles Bridge for the direct link
// (docs-dev/bridge-link.md). The Bridge sends it with POST /api/link while a
// panel is added in Home Assistant; NVS tab5_config/bridge_link keeps it with
// a checksum. Without a record the panel uses MQTT as before.
//
// Threads: load() runs once in setup(); save()/clear()/setPairRequested() on
// the loop task (Web Admin handlers and pairing). The network worker never
// reads this module; HomeTilesNetworkManager keeps its own copy.
namespace link_config {

constexpr size_t kHostSize = 64;

struct Settings {
  char host[kHostSize];
  uint16_t port;
  // The panel was just added and pairs over the link after its restart.
  bool pair_requested;
};

// Reads the record into the cache; false (and an empty cache) without one.
bool load();
bool configured();
// The cached settings; host is empty when no link is configured.
const Settings& current();
bool save(const Settings& settings);
bool setPairRequested(bool requested);

// A host the panel can connect to: an IPv4 address or a DNS name of letters,
// digits, dots and dashes.
bool validHost(const char* host);

}  // namespace link_config
