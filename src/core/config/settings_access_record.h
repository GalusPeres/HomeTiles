#pragma once

#include "src/core/config/config_manager.h"

#include <stddef.h>
#include <string.h>

// Internal persistence codec for the Settings parental-control record.
// Record layouts, migration versions, and credential handling are unchanged.
// Corrupt storage deliberately recovers by revealing and unlocking Settings;
// this prevents a damaged parental-control record from locking out the owner.
// NVS reads/writes and post-load policy remain in ConfigManager. Keep this
// implementation local to its caller so extraction adds no runtime boundary.
namespace settings_access_record {

namespace {

constexpr uint32_t kSettingsAccessMagic = 0x43415448;  // HTAC
constexpr uint8_t kSettingsAccessVersion = 4;
constexpr uint8_t kSettingsAccessEnabled = 1U << 0;
constexpr uint8_t kSettingsAccessHidden = 1U << 1;
constexpr uint8_t kSettingsAccessSwipe = 1U << 2;

constexpr uint8_t kSettingsTileSnapshotValid = 1U << 0;

struct __attribute__((packed)) LegacySettingsAccessRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint8_t reveal_edge;
  uint8_t reserved;
  uint8_t salt[pin_access::kSaltSize];
  uint8_t hash[pin_access::kHashSize];
  uint32_t checksum;
};

static_assert(sizeof(LegacySettingsAccessRecord) == 60,
              "Legacy Settings access record size changed");

struct __attribute__((packed)) SettingsAccessRecordV3 {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint8_t reveal_edge;
  uint8_t snapshot_flags;
  uint8_t salt[pin_access::kSaltSize];
  uint8_t hash[pin_access::kHashSize];
  uint32_t snapshot_bg_color;
  char snapshot_title[32];
  char snapshot_icon_name[32];
  uint8_t snapshot_col;
  uint8_t snapshot_row;
  uint8_t snapshot_span_w;
  uint8_t snapshot_span_h;
  uint32_t checksum;
};

static_assert(sizeof(SettingsAccessRecordV3) == 132,
              "Legacy Settings v3 access record size changed");

struct __attribute__((packed)) SettingsAccessRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint8_t reveal_edge;
  uint8_t snapshot_flags;
  uint8_t salt[pin_access::kSaltSize];
  uint8_t hash[pin_access::kHashSize];
  uint32_t snapshot_bg_color;
  char snapshot_title[32];
  char snapshot_icon_name[32];
  uint8_t snapshot_col;
  uint8_t snapshot_row;
  uint8_t snapshot_span_w;
  uint8_t snapshot_span_h;
  uint8_t pin_length;
  char pin_digits[pin_access::kUserPinMaxDigits];
  uint8_t reserved[3];
  uint32_t checksum;
};

static_assert(sizeof(SettingsAccessRecord) == 144,
              "Settings access record size changed");

template <typename Record>
uint32_t settings_access_checksum(const Record& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < offsetof(Record, checksum); ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

void clear_settings_tile_snapshot(DeviceConfig& config) {
  memset(&config.settings_tile_snapshot, 0,
         sizeof(config.settings_tile_snapshot));
  config.settings_tile_snapshot.span_w = 1;
  config.settings_tile_snapshot.span_h = 1;
}

SettingsAccessRecord make_settings_access_record(const DeviceConfig& config) {
  SettingsAccessRecord record{};
  record.magic = kSettingsAccessMagic;
  record.version = kSettingsAccessVersion;
  if (config.settings_pin_enabled) record.flags |= kSettingsAccessEnabled;
  if (config.settings_tile_hidden) record.flags |= kSettingsAccessHidden;
  if (config.settings_swipe_enabled) record.flags |= kSettingsAccessSwipe;
  record.reveal_edge = config.settings_reveal_edge;
  memcpy(record.salt, config.settings_pin_salt, sizeof(record.salt));
  memcpy(record.hash, config.settings_pin_hash, sizeof(record.hash));
  const SettingsTileSnapshot& snapshot = config.settings_tile_snapshot;
  if (snapshot.valid) record.snapshot_flags |= kSettingsTileSnapshotValid;
  record.snapshot_bg_color = snapshot.bg_color;
  strncpy(record.snapshot_title, snapshot.title,
          sizeof(record.snapshot_title) - 1);
  strncpy(record.snapshot_icon_name, snapshot.icon_name,
          sizeof(record.snapshot_icon_name) - 1);
  record.snapshot_col = snapshot.col;
  record.snapshot_row = snapshot.row;
  record.snapshot_span_w = snapshot.span_w;
  record.snapshot_span_h = snapshot.span_h;
  const String stored_pin(config.settings_pin_value);
  if (config.settings_pin_enabled &&
      pin_access::isValidUserPin(stored_pin) &&
      pin_access::verifyCredential(stored_pin.c_str(),
                                   config.settings_pin_salt,
                                   config.settings_pin_hash)) {
    record.pin_length = static_cast<uint8_t>(stored_pin.length());
    memcpy(record.pin_digits, stored_pin.c_str(), stored_pin.length());
  }
  record.checksum = settings_access_checksum(record);
  return record;
}

bool apply_settings_access_record(const SettingsAccessRecord& record,
                                  DeviceConfig& config) {
  if (record.magic != kSettingsAccessMagic ||
      record.version != kSettingsAccessVersion ||
      record.checksum != settings_access_checksum(record) ||
      record.reveal_edge >
          static_cast<uint8_t>(SettingsRevealEdge::Bottom)) {
    return false;
  }

  config.settings_pin_enabled =
      (record.flags & kSettingsAccessEnabled) != 0;
  config.settings_tile_hidden = (record.flags & kSettingsAccessHidden) != 0;
  config.settings_swipe_enabled =
      config.settings_tile_hidden ||
      (record.flags & kSettingsAccessSwipe) != 0;
  config.settings_reveal_edge = record.reveal_edge;
  memcpy(config.settings_pin_salt, record.salt,
         sizeof(config.settings_pin_salt));
  memcpy(config.settings_pin_hash, record.hash,
         sizeof(config.settings_pin_hash));
  pin_access::secureClear(config.settings_pin_value,
                          sizeof(config.settings_pin_value));
  if (config.settings_pin_enabled &&
      record.pin_length >= pin_access::kUserPinMinDigits &&
      record.pin_length <= pin_access::kUserPinMaxDigits) {
    char candidate[pin_access::kUserPinMaxDigits + 1]{};
    memcpy(candidate, record.pin_digits, record.pin_length);
    const String candidate_pin(candidate);
    if (pin_access::isValidUserPin(candidate_pin) &&
        pin_access::verifyCredential(candidate_pin.c_str(), record.salt,
                                     record.hash)) {
      strncpy(config.settings_pin_value, candidate,
              sizeof(config.settings_pin_value) - 1);
    }
    pin_access::secureClear(candidate, sizeof(candidate));
  }
  clear_settings_tile_snapshot(config);
  const bool snapshot_valid =
      (record.snapshot_flags & kSettingsTileSnapshotValid) != 0 &&
      record.snapshot_col < Device::kGridCols &&
      record.snapshot_row < Device::kGridRows &&
      record.snapshot_span_w >= 1 && record.snapshot_span_h >= 1 &&
      record.snapshot_span_w <= Device::kGridCols &&
      record.snapshot_span_h <= Device::kGridRows;
  if (snapshot_valid) {
    SettingsTileSnapshot& snapshot = config.settings_tile_snapshot;
    snapshot.valid = true;
    snapshot.bg_color = record.snapshot_bg_color;
    memcpy(snapshot.title, record.snapshot_title, sizeof(record.snapshot_title));
    snapshot.title[sizeof(record.snapshot_title) - 1] = '\0';
    memcpy(snapshot.icon_name, record.snapshot_icon_name,
           sizeof(snapshot.icon_name));
    snapshot.title[sizeof(snapshot.title) - 1] = '\0';
    snapshot.icon_name[sizeof(snapshot.icon_name) - 1] = '\0';
    snapshot.col = record.snapshot_col;
    snapshot.row = record.snapshot_row;
    snapshot.span_w = record.snapshot_span_w;
    snapshot.span_h = record.snapshot_span_h;
  }
  return true;
}

bool apply_settings_access_record_v3(const SettingsAccessRecordV3& record,
                                     DeviceConfig& config) {
  if (record.magic != kSettingsAccessMagic || record.version != 3 ||
      record.checksum != settings_access_checksum(record) ||
      record.reveal_edge >
          static_cast<uint8_t>(SettingsRevealEdge::Bottom)) {
    return false;
  }

  config.settings_pin_enabled =
      (record.flags & kSettingsAccessEnabled) != 0;
  config.settings_tile_hidden = (record.flags & kSettingsAccessHidden) != 0;
  config.settings_swipe_enabled =
      config.settings_tile_hidden ||
      (record.flags & kSettingsAccessSwipe) != 0;
  config.settings_reveal_edge = record.reveal_edge;
  memcpy(config.settings_pin_salt, record.salt,
         sizeof(config.settings_pin_salt));
  memcpy(config.settings_pin_hash, record.hash,
         sizeof(config.settings_pin_hash));
  pin_access::secureClear(config.settings_pin_value,
                          sizeof(config.settings_pin_value));
  clear_settings_tile_snapshot(config);
  const bool snapshot_valid =
      (record.snapshot_flags & kSettingsTileSnapshotValid) != 0 &&
      record.snapshot_col < Device::kGridCols &&
      record.snapshot_row < Device::kGridRows &&
      record.snapshot_span_w >= 1 && record.snapshot_span_h >= 1 &&
      record.snapshot_span_w <= Device::kGridCols &&
      record.snapshot_span_h <= Device::kGridRows;
  if (snapshot_valid) {
    SettingsTileSnapshot& snapshot = config.settings_tile_snapshot;
    snapshot.valid = true;
    snapshot.bg_color = record.snapshot_bg_color;
    memcpy(snapshot.title, record.snapshot_title, sizeof(record.snapshot_title));
    snapshot.title[sizeof(record.snapshot_title) - 1] = '\0';
    memcpy(snapshot.icon_name, record.snapshot_icon_name,
           sizeof(snapshot.icon_name));
    snapshot.title[sizeof(snapshot.title) - 1] = '\0';
    snapshot.icon_name[sizeof(snapshot.icon_name) - 1] = '\0';
    snapshot.col = record.snapshot_col;
    snapshot.row = record.snapshot_row;
    snapshot.span_w = record.snapshot_span_w;
    snapshot.span_h = record.snapshot_span_h;
  }
  return true;
}

bool apply_legacy_settings_access_record(
    const LegacySettingsAccessRecord& record, DeviceConfig& config) {
  if (record.magic != kSettingsAccessMagic ||
      (record.version != 1 && record.version != 2) ||
      record.checksum != settings_access_checksum(record) ||
      record.reveal_edge >
          static_cast<uint8_t>(SettingsRevealEdge::Bottom)) {
    return false;
  }
  config.settings_pin_enabled =
      (record.flags & kSettingsAccessEnabled) != 0;
  config.settings_tile_hidden =
      (record.flags & kSettingsAccessHidden) != 0;
  config.settings_swipe_enabled =
      config.settings_tile_hidden ||
      (record.version != 1 && (record.flags & kSettingsAccessSwipe) != 0);
  config.settings_reveal_edge = record.reveal_edge;
  memcpy(config.settings_pin_salt, record.salt,
         sizeof(config.settings_pin_salt));
  memcpy(config.settings_pin_hash, record.hash,
         sizeof(config.settings_pin_hash));
  pin_access::secureClear(config.settings_pin_value,
                          sizeof(config.settings_pin_value));
  clear_settings_tile_snapshot(config);
  return true;
}

void fail_open_settings_access(DeviceConfig& config) {
  config.settings_pin_enabled = false;
  config.settings_tile_hidden = false;
  config.settings_swipe_enabled = false;
  config.settings_reveal_edge =
      static_cast<uint8_t>(SettingsRevealEdge::Left);
  pin_access::clearCredential(config.settings_pin_salt,
                              config.settings_pin_hash);
  pin_access::secureClear(config.settings_pin_value,
                          sizeof(config.settings_pin_value));
  clear_settings_tile_snapshot(config);
}

}  // namespace

}  // namespace settings_access_record
