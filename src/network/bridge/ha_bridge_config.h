#ifndef HA_BRIDGE_CONFIG_H
#define HA_BRIDGE_CONFIG_H

#include <Arduino.h>
#include <strings.h>
#include <map>
#include <string>
#include <utility>
#include <esp_heap_caps.h>
// PsramAllocator/PsString keep the entity index (std::map nodes and string
// buffers) out of the scarce internal SRAM.
#include "src/core/memory/psram_allocator.h"

// Case-insensitive ordered map for entity keys. The text blob maps below match
// keys with strncasecmp/equalsIgnoreCase everywhere, so the index has to behave
// identically. is_transparent allows find(const char*) without a temporary copy
// of the key.
struct HaEntityKeyLess {
  using is_transparent = void;
  bool operator()(const PsString& a, const PsString& b) const {
    return strcasecmp(a.c_str(), b.c_str()) < 0;
  }
  bool operator()(const PsString& a, const char* b) const {
    return strcasecmp(a.c_str(), b) < 0;
  }
  bool operator()(const char* a, const PsString& b) const {
    return strcasecmp(a, b.c_str()) < 0;
  }
};
using HaEntityKeyMap =
    std::map<PsString, PsString, HaEntityKeyLess,
             PsramAllocator<std::pair<const PsString, PsString>>>;

static constexpr size_t HA_SENSOR_SLOT_COUNT = 6;
static constexpr size_t HA_SCENE_SLOT_COUNT = 6;

struct HaBridgeConfigData {
  String sensors_text;
  String configured_sensors_text;
  bool has_configured_sensors = false;
  String binary_sensors_text;
  bool has_editable_lists = false;
  String numbers_text;
  String selects_text;
  String datetimes_text;
  String energy_text;
  String weathers_text;
  String lights_text;
  String switches_text;
  String media_players_text;
  String climates_text;
  String covers_text;
  // Lock, Alarm panel and Fan tiles (Bridge keys locks, alarm_panels, fans).
  String locks_text;
  String alarm_panels_text;
  String fans_text;
  String cameras_text;
  // Image entities released for the screensaver picture (Bridge "images").
  String images_text;
  String scene_alias_text;
  String sensor_slots[HA_SENSOR_SLOT_COUNT];
  String scene_slots[HA_SCENE_SLOT_COUNT];
  String sensor_units_map;
  String sensor_names_map;
  String sensor_values_map;
  String sensor_state_kinds_map;
  String entity_icons_map;
  String sensor_titles[HA_SENSOR_SLOT_COUNT];
  String sensor_custom_units[HA_SENSOR_SLOT_COUNT];
  String scene_titles[HA_SCENE_SLOT_COUNT];
  uint32_t sensor_colors[HA_SENSOR_SLOT_COUNT];  // RGB hex (0 = default 0x2A2A2A)
  uint32_t scene_colors[HA_SCENE_SLOT_COUNT];    // RGB hex (0 = default 0x353535)
};

class HaBridgeConfig {
public:
  HaBridgeConfig();

  bool load();
  bool save(const HaBridgeConfigData& data);
  bool applyJson(const char* json_payload, bool* out_reload = nullptr, bool* out_icons_changed = nullptr);

  // Live sensor values update only values_index_ (updateSensorValue());
  // the "key=value" blob read by Web Admin is rebuilt from it here, at most
  // once per batch of updates.
  const HaBridgeConfigData& get() const {
    if (values_blob_dirty_) materializeValuesBlob();
    return data;
  }
  bool hasData() const;
  // The Bridge sends an "energy" section only when its entry enables Energy
  // and Home Assistant has an Energy dashboard; without one it never answers
  // energy requests (all Bridge versions).
  bool hasEnergyEntries() const { return data.energy_text.length() > 0; }
  // The live Bridge configuration announced "entity_search"
  // (network/bridge/entity_search.h): 1 = the picker searches through the
  // Bridge, 2 = also entity declarations. Not stored: 0 until the Bridge's
  // configuration arrived after a start.
  bool supportsEntitySearch() const { return entity_search_ >= 1; }
  bool supportsEntityDeclarations() const { return entity_search_ >= 2; }
  // The live Bridge configuration announced "push": 1. Such a Bridge sends
  // name, unit and icon changes by itself, so the panel's periodic refresh is
  // only a slow safety net. Not stored: 0 until the configuration arrived.
  bool bridgePushesChanges() const { return push_ >= 1; }
  String findSensorUnit(const String& entity_id) const;
  String findSensorName(const String& entity_id) const;
  String findSensorInitialValue(const String& entity_id) const;
  String findSensorStateKind(const String& entity_id) const;
  String findEntityIcon(const String& entity_id) const;
  // const char* variants for callers that hold no Arduino Strings themselves,
  // such as the PSRAM folder entity cache. Thanks to is_transparent these are
  // free of allocations except for the return value.
  String findSensorUnit(const char* entity_id) const;
  String findSensorName(const char* entity_id) const;
  String findSensorInitialValue(const char* entity_id) const;
  String findSensorStateKind(const char* entity_id) const;
  String findEntityIcon(const char* entity_id) const;
  String findSceneEntity(const String& alias) const;

  // Update live sensor value (for web interface)
  String findEditableValue(const String& entity_id) const;
  void updateEditableValue(const String& entity_id, const String& payload);
  // The Bridge's retained `detail` state of a Lock, Alarm panel or Fan
  // (src/types/device/device_state.h); empty while none arrived.
  String findDetailValue(const String& entity_id) const;
  void updateDetailValue(const String& entity_id, const String& payload);
  void updateSensorValue(const String& entity_id, const String& value);
  void registerSensorMeta(const String& entity_id, const String& name, const String& unit);
  void updateEntityMeta(const String& entity_id, const String& name, const String& unit, const String& icon);
  bool applyIconUpdate(const char* json_payload);

  String buildJsonPayload(const char* device_id,
                          const char* base_topic,
                          const char* ha_prefix) const;

private:
  HaBridgeConfigData data;

  // Lookup index over the five "key=value\n" text blobs (sensor_units_map and
  // friends). The blobs stay the leading format, because Web Admin reads them
  // directly and applyJson swaps them as a whole, but EVERY find* lookup goes
  // through these maps instead of scanning the blob linearly. A single lookup on
  // a blob that had grown over runtime was already a multi-millisecond block on
  // its own, and the bridge cache refresh does about 150 of them in a row
  // (measured: bridge_cache=2324ms in the [LoopGap] log). Deliberately NOT part
  // of HaBridgeConfigData: applyJson copies that whole struct (merged = data)
  // and the indexes must not be copied along.
  HaEntityKeyMap units_index_;
  HaEntityKeyMap names_index_;
  HaEntityKeyMap values_index_;
  HaEntityKeyMap editable_values_index_;
  HaEntityKeyMap detail_values_index_;
  HaEntityKeyMap state_kinds_index_;
  HaEntityKeyMap icons_index_;
  uint8_t entity_search_ = 0;
  uint8_t push_ = 0;
  // Call after every complete blob swap (load/save/applyJson). The single-value
  // updates maintain blob and index together, except updateSensorValue(),
  // which marks the values blob stale until materializeValuesBlob().
  void rebuildEntityIndexes();
  void materializeValuesBlob() const;
  mutable bool values_blob_dirty_ = false;
  void pruneEditableValues();

  static void appendJsonEscaped(String& out, const String& value);
  static void appendSensorsJson(String& out, const String& text);
  static void appendSceneMapJson(String& out, const String& text);
  static String normalizeLine(const String& line);
};

extern HaBridgeConfig haBridgeConfig;

#endif // HA_BRIDGE_CONFIG_H
