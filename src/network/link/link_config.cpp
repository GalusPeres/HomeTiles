#include "src/network/link/link_config.h"

#include <Preferences.h>

#include "src/core/config/batched_nvs_write.h"
#include "src/devices/device.h"

namespace link_config {

namespace {

constexpr const char* kNamespace = "tab5_config";
// NVS keys are limited to 15 characters.
constexpr const char* kRecordKey = "bridge_link";
constexpr uint32_t kMagic = 0x4B4C5448;  // "HTLK"
constexpr uint8_t kVersion = 1;

struct Record {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint16_t port;
  char host[kHostSize];
  uint32_t checksum;
};

constexpr uint8_t kFlagPairRequested = 0x01;

Settings g_settings{};
bool g_loaded = false;

// FNV-1a over everything before the checksum.
uint32_t checksum(const Record& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < offsetof(Record, checksum); ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

bool write(const Record* record) {
  Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.println("[Link] Could not open preferences");
    return false;
  }
  bool written = true;
  if (record) {
    written = prefs.putBytes(kRecordKey, record, sizeof(*record)) == sizeof(*record);
  } else {
    prefs.remove(kRecordKey);
  }
  const bool committed = BatchedNvsWrite::finish(prefs);
  return written && committed;
}

}  // namespace

bool validHost(const char* host) {
  if (!host) return false;
  const size_t length = strlen(host);
  if (length == 0 || length >= kHostSize) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = host[i];
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
    if (!ok) return false;
  }
  return host[0] != '.' && host[0] != '-';
}

bool load() {
  g_loaded = true;
  memset(&g_settings, 0, sizeof(g_settings));
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;
  Record record{};
  const bool read = prefs.getBytesLength(kRecordKey) == sizeof(record) &&
                    prefs.getBytes(kRecordKey, &record, sizeof(record)) == sizeof(record);
  prefs.end();
  if (!read) return false;
  record.host[kHostSize - 1] = '\0';
  if (record.magic != kMagic || record.version != kVersion ||
      record.checksum != checksum(record) || record.port == 0 || !validHost(record.host)) {
    Serial.println("[Link] Stored Bridge address is invalid; ignored");
    return false;
  }
  memcpy(g_settings.host, record.host, kHostSize);
  g_settings.port = record.port;
  g_settings.pair_requested = (record.flags & kFlagPairRequested) != 0;
  Serial.printf("[Link] Bridge address %s:%u%s\n", g_settings.host,
                static_cast<unsigned>(g_settings.port),
                g_settings.pair_requested ? " (pairing requested)" : "");
  return true;
}

bool configured() {
  if (!g_loaded) load();
  return g_settings.host[0] != '\0' && g_settings.port != 0;
}

const Settings& current() {
  if (!g_loaded) load();
  return g_settings;
}

bool save(const Settings& settings) {
  if (!validHost(settings.host) || settings.port == 0) return false;
  Record record{};
  record.magic = kMagic;
  record.version = kVersion;
  record.flags = settings.pair_requested ? kFlagPairRequested : 0;
  record.port = settings.port;
  strncpy(record.host, settings.host, kHostSize - 1);
  record.checksum = checksum(record);
  if (!write(&record)) {
    Serial.println("[Link] Could not store the Bridge address");
    return false;
  }
  g_loaded = true;
  memset(&g_settings, 0, sizeof(g_settings));
  memcpy(g_settings.host, record.host, kHostSize);
  g_settings.port = record.port;
  g_settings.pair_requested = settings.pair_requested;
  return true;
}

bool setPairRequested(bool requested) {
  if (!configured()) return false;
  if (g_settings.pair_requested == requested) return true;
  Settings updated = g_settings;
  updated.pair_requested = requested;
  return save(updated);
}

}  // namespace link_config
