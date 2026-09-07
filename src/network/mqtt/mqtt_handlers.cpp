#include "src/types/value/value_control.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/network/mqtt/mqtt_packet_safety.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/network_manager.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/bridge/control_contract.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/ui/screensaver/screensaver_config.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/video/camera_geometry.h"
#include "src/ui/tabs/settings/tab_settings.h"
#include "src/types/energy/energy_data.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/core/config/config_manager.h"
#include "src/core/display/display_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/device_entities.h"
#include "src/core/power/power_manager.h"
#include "src/core/power/battery_state.h"
#include "src/core/hardware/board_hal.h"
#include "src/core/display/lvgl_tick_service.h"
#include "src/io/hardware_io.h"
#include "src/web/server/web_admin.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <time.h>
#include <vector>

#ifndef TAB5_HAS_ONEWIRE_DS18X20
#define TAB5_HAS_ONEWIRE_DS18X20 0
#endif

// External temperature channels are configured by hardwareIo. Library presence
// is not a board capability and must never enable GPIO 1/50 auto-probing.
// Cached values for outgoing snapshots
static float g_outside_c = 21.7f;
static float g_inside_c = 22.4f;
static int g_soc_pct = -1;
static float g_external_temp_c = NAN;
static bool g_external_temp_valid = false;
static constexpr uint32_t kExternalTempGridRefreshMs = 5000;
static String g_external_last_grid_payload;
static uint32_t g_external_last_grid_ms = 0;
static constexpr uint16_t kExternalTempHistoryHoursMax = 168;
static constexpr uint16_t kExternalTempHistorySampleMinutes = 5;
static constexpr uint16_t kExternalTempHistoryPoints =
    static_cast<uint16_t>((kExternalTempHistoryHoursMax * 60U) / kExternalTempHistorySampleMinutes);
static constexpr uint32_t kExternalTempHistorySampleMs =
    static_cast<uint32_t>(kExternalTempHistorySampleMinutes) * 60UL * 1000UL;
static uint8_t* g_external_history_storage = nullptr;
static float* g_external_history_values = nullptr;
static bool* g_external_history_valid = nullptr;
static uint16_t g_external_history_head = 0;
static uint16_t g_external_history_count = 0;
static uint32_t g_external_history_last_store_ms = 0;
static constexpr uint32_t kHistoryHaResponseTimeoutMs = 2000;
static constexpr uint32_t kDiscreteHistoryHaResponseTimeoutMs = 8000;
static constexpr uint8_t kHistoryPendingSlots = 8;
static constexpr uint32_t kDynamicSlotReloadQuietMs = 3000;
static constexpr uint32_t kDynamicSlotReloadAdminQuietMs = 5000;

static volatile bool g_dynamic_slots_reload_requested = false;
static uint32_t g_dynamic_slots_reload_due_ms = 0;
static bool g_bridge_initial_sync_complete = false;

struct PendingHistoryRequest {
  String entity_id;
  uint32_t requested_at_ms = 0;
  uint16_t hours = 24;
  uint16_t period_minutes = 5;
  uint16_t points = 288;
  bool active = false;
};

static PendingHistoryRequest g_pending_history[kHistoryPendingSlots];

struct PendingDiscreteHistoryRequest {
  String entity_id;
  String kind;
  uint32_t requested_at_ms = 0;
  uint16_t hours = 24;
  bool active = false;
};

static PendingDiscreteHistoryRequest g_pending_discrete_history;

static String buildHaStatestreamTopic(const String& entity_id, const char* suffix);

static void update_all_grids(const char* entity_id, const char* payload) {
  if (!entity_id || !payload) return;
  tiles_update_sensor_by_entity(GridType::TAB0, entity_id, payload);
  tiles_update_sensor_by_entity(GridType::TAB1, entity_id, payload);
  tiles_update_sensor_by_entity(GridType::TAB2, entity_id, payload);
}

static bool is_external_temp_entity(const char* entity_id) {
  if (!entity_id || !*entity_id) return false;
  // New firmware routes explicitly configured channels through hardwareIo.
  return false;
}

static bool is_internal_tab5_entity(const char* entity_id) {
  if (!entity_id || !*entity_id) return false;
  String normalized(entity_id);
  normalized.trim();
  normalized.toLowerCase();

  if (normalized.equalsIgnoreCase(kEntityDisplayBrightness)) return true;
  if (normalized.equalsIgnoreCase(kEntityScreensaverBrightness)) return true;
  if (normalized.equalsIgnoreCase(kEntityDisplayRotate)) return true;
  if (normalized.equalsIgnoreCase(kEntityDisplaySleep)) return true;

  return false;
}

static bool has_valid_local_time_for_history() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) return false;

  const int year = timeinfo.tm_year + 1900;
  const int month = timeinfo.tm_mon + 1;
  const int day = timeinfo.tm_mday;
  const int hour = timeinfo.tm_hour;
  const int minute = timeinfo.tm_min;

  return (year >= 2024 && year <= 2100) &&
         (month >= 1 && month <= 12) &&
         (day >= 1 && day <= 31) &&
         (hour >= 0 && hour < 24) &&
         (minute >= 0 && minute < 60);
}

static bool ensure_external_temp_history_storage() {
  if (g_external_history_storage) return true;

  constexpr size_t kValuesBytes =
      sizeof(float) * static_cast<size_t>(kExternalTempHistoryPoints);
  constexpr size_t kValidBytes =
      sizeof(bool) * static_cast<size_t>(kExternalTempHistoryPoints);
  g_external_history_storage = static_cast<uint8_t*>(heap_caps_calloc(
      1, kValuesBytes + kValidBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_external_history_storage) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      Serial.printf("[History] No PSRAM for local temperature history (%u bytes)\n",
                    static_cast<unsigned>(kValuesBytes + kValidBytes));
    }
    return false;
  }

  g_external_history_values =
      reinterpret_cast<float*>(g_external_history_storage);
  g_external_history_valid =
      reinterpret_cast<bool*>(g_external_history_storage + kValuesBytes);
  return true;
}

static void push_external_temp_history(float value, bool valid) {
  if (!ensure_external_temp_history_storage()) return;
  g_external_history_values[g_external_history_head] = value;
  g_external_history_valid[g_external_history_head] = valid;
  g_external_history_head = static_cast<uint16_t>((g_external_history_head + 1) % kExternalTempHistoryPoints);
  if (g_external_history_count < kExternalTempHistoryPoints) {
    ++g_external_history_count;
  }
}

static void store_external_temp_history(uint32_t now_ms) {
  if (!has_valid_local_time_for_history()) {
    // Wait for valid time so history is not filled with pre-NTP boot samples.
    g_external_history_last_store_ms = 0;
    return;
  }

  if (g_external_history_last_store_ms == 0) {
    if (!g_external_temp_valid || std::isnan(g_external_temp_c) || std::isinf(g_external_temp_c)) {
      return;
    }
    g_external_history_last_store_ms = now_ms - kExternalTempHistorySampleMs;
  }

  if ((int32_t)(now_ms - g_external_history_last_store_ms) < static_cast<int32_t>(kExternalTempHistorySampleMs)) {
    return;
  }
  g_external_history_last_store_ms = now_ms;

  if (g_external_temp_valid && !std::isnan(g_external_temp_c) && !std::isinf(g_external_temp_c)) {
    push_external_temp_history(g_external_temp_c, true);
  } else if (g_external_history_count > 0) {
    push_external_temp_history(0.0f, false);
  }
}

static String build_external_temp_history_payload(const char* entity_id,
                                                  uint16_t hours,
                                                  uint16_t period_minutes,
                                                  uint16_t points) {
  if (hours == 0) hours = 24;
  if (hours > kExternalTempHistoryHoursMax) hours = kExternalTempHistoryHoursMax;
  if (period_minutes == 0) period_minutes = kExternalTempHistorySampleMinutes;
  if (points == 0) {
    points = static_cast<uint16_t>((static_cast<uint32_t>(hours) * 60U) / period_minutes);
  }
  if (points == 0) points = 1;

  const uint16_t samples_per_bucket = static_cast<uint16_t>(
      (period_minutes + kExternalTempHistorySampleMinutes - 1U) / kExternalTempHistorySampleMinutes);
  const uint32_t requested_base_points =
      static_cast<uint32_t>(points) * static_cast<uint32_t>(samples_per_bucket);
  const uint16_t clamped_base_points = static_cast<uint16_t>(
      requested_base_points > kExternalTempHistoryPoints ? kExternalTempHistoryPoints : requested_base_points);
  const uint16_t available_base_points =
      (g_external_history_count > clamped_base_points) ? clamped_base_points : g_external_history_count;
  const uint16_t missing_base_points = static_cast<uint16_t>(clamped_base_points - available_base_points);
  const uint16_t history_start = static_cast<uint16_t>(
      (g_external_history_head + kExternalTempHistoryPoints - available_base_points) % kExternalTempHistoryPoints);

  String payload;
  payload.reserve(12288);
  payload = "{\"entity_id\":\"";
  payload += entity_id ? entity_id : kEntityExternalTemperature;
  payload += "\",\"unit\":\"C\",\"current\":\"";

  char current_buf[24];
  const bool has_current_value = g_external_temp_valid && !std::isnan(g_external_temp_c) && !std::isinf(g_external_temp_c);
  if (has_current_value) {
    dtostrf(g_external_temp_c, 0, 1, current_buf);
    payload += current_buf;
  } else {
    payload += "unavailable";
  }
  payload += "\",\"hours\":";
  payload += String(hours);
  payload += ",\"period_minutes\":";
  payload += String(period_minutes);
  payload += ",\"stat\":\"mean\",\"values\":[";

  bool first = true;

  for (uint16_t bucket = 0; bucket < points; ++bucket) {
    if (!first) payload += ",";

    float sum = 0.0f;
    uint16_t valid_count = 0;
    for (uint16_t sample = 0; sample < samples_per_bucket; ++sample) {
      const uint32_t virtual_index =
          static_cast<uint32_t>(bucket) * static_cast<uint32_t>(samples_per_bucket) + sample;
      if (virtual_index < missing_base_points) continue;

      const uint32_t local_offset = virtual_index - missing_base_points;
      if (local_offset >= available_base_points) continue;

      const uint16_t idx = static_cast<uint16_t>((history_start + local_offset) % kExternalTempHistoryPoints);
      if (!g_external_history_valid[idx] || std::isnan(g_external_history_values[idx]) ||
          std::isinf(g_external_history_values[idx])) {
        continue;
      }
      sum += g_external_history_values[idx];
      ++valid_count;
    }

    if (valid_count == 0) {
      payload += "null";
    } else {
      dtostrf(sum / valid_count, 0, 1, current_buf);
      payload += current_buf;
    }
    first = false;
  }

  payload += "]}";
  return payload;
}

static String build_empty_history_payload(const char* entity_id,
                                          uint16_t hours = 0,
                                          uint16_t period_minutes = 0,
                                          uint16_t points = 0) {
  String payload;
  payload.reserve(160);
  payload = "{\"entity_id\":\"";
  payload += entity_id ? entity_id : "";
  payload += "\"";
  if (hours) {
    payload += ",\"hours\":";
    payload += String(hours);
  }
  if (period_minutes) {
    payload += ",\"period_minutes\":";
    payload += String(period_minutes);
  }
  if (points) {
    payload += ",\"points\":";
    payload += String(points);
  }
  payload += ",\"values\":[]}";
  return payload;
}

static String build_discrete_history_unavailable_payload(
    const char* kind, const char* entity_id, uint16_t hours,
    const char* error) {
  String payload;
  payload.reserve(192);
  payload = "{\"version\":1,\"kind\":\"";
  payload += kind ? kind : "state";
  payload += "\",\"entity_id\":\"";
  payload += entity_id ? entity_id : "";
  payload += "\",\"hours\":";
  payload += String(hours);
  payload += ",\"history_available\":false,\"error\":\"";
  payload += error ? error : "unavailable";
  payload += "\"}";
  return payload;
}

static bool extract_json_string_field(const char* json, const char* key, String& out) {
  out = "";
  if (!json || !*json || !key || !*key) return false;

  String pattern = "\"";
  pattern += key;
  pattern += "\"";

  const char* found = strstr(json, pattern.c_str());
  if (!found) return false;

  const char* colon = strchr(found + pattern.length(), ':');
  if (!colon) return false;

  const char* q1 = strchr(colon + 1, '\"');
  if (!q1) return false;

  const char* q2 = strchr(q1 + 1, '\"');
  if (!q2 || q2 <= q1 + 1) return false;

  out = String(q1 + 1);
  out.remove(static_cast<unsigned int>(q2 - (q1 + 1)));
  out.trim();
  return out.length() > 0;
}

static bool extract_json_uint16_field(const char* json, const char* key,
                                      uint16_t& out) {
  if (!json || !*json || !key || !*key) return false;
  String pattern = "\"";
  pattern += key;
  pattern += "\"";
  const char* found = strstr(json, pattern.c_str());
  if (!found) return false;
  const char* colon = strchr(found + pattern.length(), ':');
  if (!colon) return false;
  const char* value = colon + 1;
  while (*value == ' ' || *value == '\t' || *value == '\r' ||
         *value == '\n') {
    ++value;
  }
  if (*value < '0' || *value > '9') return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (!end || end == value || parsed > 0xFFFFUL) return false;
  out = static_cast<uint16_t>(parsed);
  return true;
}

static void clear_pending_history_request(const char* entity_id) {
  if (!entity_id || !*entity_id) return;
  for (uint8_t i = 0; i < kHistoryPendingSlots; ++i) {
    if (!g_pending_history[i].active) continue;
    if (g_pending_history[i].entity_id.equalsIgnoreCase(entity_id)) {
      g_pending_history[i].active = false;
      g_pending_history[i].entity_id = "";
      g_pending_history[i].requested_at_ms = 0;
      g_pending_history[i].hours = 24;
      g_pending_history[i].period_minutes = 5;
      g_pending_history[i].points = 288;
    }
  }
}

static void clear_pending_discrete_history_request(
    const char* entity_id, const char* response_kind,
    uint16_t response_hours = 0) {
  if (!entity_id || !*entity_id || !response_kind || !*response_kind ||
      !g_pending_discrete_history.active) {
    return;
  }
  if (!g_pending_discrete_history.entity_id.equalsIgnoreCase(entity_id) ||
      !g_pending_discrete_history.kind.equalsIgnoreCase(response_kind)) {
    return;
  }
  if (response_hours != 0 &&
      response_hours != g_pending_discrete_history.hours) {
    return;
  }
  g_pending_discrete_history = {};
}

static void mark_pending_discrete_history_request(const char* entity_id,
                                                  const char* kind,
                                                  uint16_t hours) {
  if (!entity_id || !*entity_id || !kind || !*kind) return;
  g_pending_discrete_history.entity_id = entity_id;
  g_pending_discrete_history.kind = kind;
  g_pending_discrete_history.requested_at_ms = millis();
  g_pending_discrete_history.hours = hours;
  g_pending_discrete_history.active = true;
}

static void queue_discrete_history_unavailable(const char* entity_id,
                                               const char* kind,
                                               uint16_t hours,
                                               const char* error) {
  if (!entity_id || !*entity_id) return;
  const String payload = build_discrete_history_unavailable_payload(
      kind, entity_id, hours, error);
  queue_sensor_popup_history(entity_id, payload.c_str(), payload.length());
}

static void mark_pending_history_request(const char* entity_id,
                                         uint32_t now_ms,
                                         uint16_t hours,
                                         uint16_t period_minutes,
                                         uint16_t points) {
  if (!entity_id || !*entity_id) return;

  int free_idx = -1;
  int oldest_idx = -1;
  uint32_t oldest_ms = 0xFFFFFFFFu;

  for (uint8_t i = 0; i < kHistoryPendingSlots; ++i) {
    if (g_pending_history[i].active) {
      if (g_pending_history[i].entity_id.equalsIgnoreCase(entity_id)) {
        g_pending_history[i].requested_at_ms = now_ms;
        g_pending_history[i].hours = hours;
        g_pending_history[i].period_minutes = period_minutes;
        g_pending_history[i].points = points;
        return;
      }
      if (g_pending_history[i].requested_at_ms < oldest_ms) {
        oldest_ms = g_pending_history[i].requested_at_ms;
        oldest_idx = i;
      }
    } else if (free_idx < 0) {
      free_idx = i;
    }
  }

  int idx = (free_idx >= 0) ? free_idx : oldest_idx;
  if (idx < 0) return;

  g_pending_history[idx].active = true;
  g_pending_history[idx].entity_id = entity_id;
  g_pending_history[idx].requested_at_ms = now_ms;
  g_pending_history[idx].hours = hours;
  g_pending_history[idx].period_minutes = period_minutes;
  g_pending_history[idx].points = points;
}

static bool queue_history_fallback_for_entity(const char* entity_id,
                                              bool time_valid,
                                              const char* reason,
                                              uint16_t hours,
                                              uint16_t period_minutes,
                                              uint16_t points) {
  if (!entity_id || !*entity_id) return false;

  if (is_external_temp_entity(entity_id)) {
    if (time_valid) {
      store_external_temp_history(millis());
      String local_payload = build_external_temp_history_payload(entity_id, hours, period_minutes, points);
      if (local_payload.length()) {
        queue_sensor_popup_history(entity_id, local_payload.c_str(), local_payload.length());
        queue_tile_graph_history(entity_id, local_payload.c_str(), local_payload.length());
        Serial.printf("[History] %s -> local history for %s (%u points, %uh/%umin)\n",
                      reason ? reason : "Fallback", entity_id, points, hours, period_minutes);
      }
    } else {
      String empty_payload = build_empty_history_payload(entity_id, hours, period_minutes, points);
      queue_sensor_popup_history(entity_id, empty_payload.c_str(), empty_payload.length());
      queue_tile_graph_history(entity_id, empty_payload.c_str(), empty_payload.length());
      Serial.printf("[History] %s -> invalid time, empty history for %s\n",
                    reason ? reason : "Fallback", entity_id);
    }
    return true;
  }

  if (is_internal_tab5_entity(entity_id)) {
    String empty_payload = build_empty_history_payload(entity_id, hours, period_minutes, points);
    queue_sensor_popup_history(entity_id, empty_payload.c_str(), empty_payload.length());
    queue_tile_graph_history(entity_id, empty_payload.c_str(), empty_payload.length());
    Serial.printf("[History] %s -> internal history for %s is empty\n",
                  reason ? reason : "Fallback", entity_id);
    return true;
  }

  return false;
}

static void service_pending_history_fallback() {
  const uint32_t now_ms = millis();
  const bool time_valid = has_valid_local_time_for_history();

  for (uint8_t i = 0; i < kHistoryPendingSlots; ++i) {
    if (!g_pending_history[i].active) continue;
    if ((int32_t)(now_ms - g_pending_history[i].requested_at_ms) <
        static_cast<int32_t>(kHistoryHaResponseTimeoutMs)) {
      continue;
    }

    String entity = g_pending_history[i].entity_id;
    const uint16_t hours = g_pending_history[i].hours;
    const uint16_t period_minutes = g_pending_history[i].period_minutes;
    const uint16_t points = g_pending_history[i].points;
    g_pending_history[i].active = false;
    g_pending_history[i].entity_id = "";
    g_pending_history[i].requested_at_ms = 0;
    g_pending_history[i].hours = 24;
    g_pending_history[i].period_minutes = 5;
    g_pending_history[i].points = 288;

    if (!entity.length()) continue;
    const bool fallback_queued = queue_history_fallback_for_entity(
        entity.c_str(), time_valid, "HA Timeout", hours, period_minutes, points);
    if (!fallback_queued) {
      Serial.printf("[History] HA timeout for %s (no local fallback)\n",
                    entity.c_str());
    }
  }

  if (g_pending_discrete_history.active &&
      static_cast<uint32_t>(now_ms -
                            g_pending_discrete_history.requested_at_ms) >=
          kDiscreteHistoryHaResponseTimeoutMs) {
    const String entity_id = g_pending_discrete_history.entity_id;
    const String kind = g_pending_discrete_history.kind;
    const uint16_t hours = g_pending_discrete_history.hours;
    g_pending_discrete_history = {};
    queue_discrete_history_unavailable(entity_id.c_str(), kind.c_str(),
                                       hours, "ha_timeout");
    Serial.printf("[%s] HA timeout for %s\n",
                  kind == "binary" ? "BinaryHistory" : "StateHistory",
                  entity_id.c_str());
  }
}

#if TAB5_HAS_ONEWIRE_DS18X20
static constexpr uint32_t kExternalTempSampleMs = 3000;
static constexpr uint32_t kExternalTempConvertMs = 800;
static constexpr uint32_t kExternalTempDiscoveryRetryMs = 2000;
static constexpr uint8_t kExternalTempMaxFailures = 1;
static constexpr uint32_t kExternalTempMqttRepublishMs = 60000;
static constexpr uint32_t kExternalTempProbeLogMs = 10000;

static OneWire g_onewire_1(1);
static OneWire g_onewire_50(50);
static DallasTemperature g_dallas_1(&g_onewire_1);
static DallasTemperature g_dallas_50(&g_onewire_50);

struct ExternalTempCandidate {
  uint8_t pin;
  OneWire* bus;
  DallasTemperature* sensor;
};

static const ExternalTempCandidate kExternalTempCandidates[] = {
  {50, &g_onewire_50, &g_dallas_50},
  {1, &g_onewire_1, &g_dallas_1},
};

static DallasTemperature* g_external_dallas = nullptr;
static uint8_t g_external_pin = 0xFF;
static DeviceAddress g_external_addr = {0};
static uint8_t g_external_failures = 0;
static bool g_external_pending = false;
static uint32_t g_external_request_ms = 0;
static uint32_t g_external_last_sample_ms = 0;
static uint32_t g_external_last_discovery_ms = 0;
static uint32_t g_external_last_mqtt_ms = 0;
static uint32_t g_external_last_probe_log_ms = 0;
static String g_external_last_payload;

static bool is_supported_ds18x20_family(uint8_t family) {
  return family == 0x28 || family == 0x10 || family == 0x22;
}

static bool init_external_temp_on_bus(const ExternalTempCandidate& candidate,
                                      DeviceAddress out_addr,
                                      bool verbose_log) {
  if (!candidate.bus || !candidate.sensor) return false;

  pinMode(candidate.pin, INPUT_PULLUP);
  delay(2);

  uint8_t addr[8] = {0};
  candidate.bus->reset_search();
  if (!candidate.bus->search(addr)) {
    if (verbose_log) {
      Serial.printf("[OneWire] GPIO %u: no 1-Wire device found\n",
                    static_cast<unsigned>(candidate.pin));
    }
    return false;
  }

  if (OneWire::crc8(addr, 7) != addr[7]) {
    if (verbose_log) {
      Serial.printf("[OneWire] GPIO %u: CRC error on bus\n",
                    static_cast<unsigned>(candidate.pin));
    }
    return false;
  }

  if (!is_supported_ds18x20_family(addr[0])) {
    if (verbose_log) {
      Serial.printf("[OneWire] GPIO %u: unknown family 0x%02X\n",
                    static_cast<unsigned>(candidate.pin),
                    static_cast<unsigned>(addr[0]));
    }
    return false;
  }

  candidate.sensor->begin();
  candidate.sensor->setWaitForConversion(false);
  memcpy(out_addr, addr, sizeof(DeviceAddress));
  candidate.sensor->setResolution(out_addr, 12);
  Serial.printf("[OneWire] DS18x20 found on GPIO %u (family 0x%02X)\n",
                static_cast<unsigned>(candidate.pin),
                static_cast<unsigned>(addr[0]));
  return true;
}

static void discover_external_temp_sensor(uint32_t now_ms) {
  if (g_external_dallas) return;
  if (g_external_last_discovery_ms != 0 &&
      (int32_t)(now_ms - g_external_last_discovery_ms) < static_cast<int32_t>(kExternalTempDiscoveryRetryMs)) {
    return;
  }
  g_external_last_discovery_ms = now_ms;
  const bool verbose_log =
    (g_external_last_probe_log_ms == 0) ||
    ((int32_t)(now_ms - g_external_last_probe_log_ms) >= static_cast<int32_t>(kExternalTempProbeLogMs));
  if (verbose_log) {
    g_external_last_probe_log_ms = now_ms;
  }

  DeviceAddress addr = {0};
  for (const auto& candidate : kExternalTempCandidates) {
    if (!candidate.sensor || !candidate.bus) continue;
    if (init_external_temp_on_bus(candidate, addr, verbose_log)) {
      g_external_dallas = candidate.sensor;
      g_external_pin = candidate.pin;
      memcpy(g_external_addr, addr, sizeof(DeviceAddress));
      g_external_failures = 0;
      g_external_pending = false;
      return;
    }
  }

  if (verbose_log) {
    Serial.println("[OneWire] No DS18x20 found on GPIO 1/50 (check DATA + 4.7k pull-up to 3V3).");
  }
}

static void service_external_temp_sensor() {
  const uint32_t now_ms = millis();
  discover_external_temp_sensor(now_ms);
  if (!g_external_dallas) {
    g_external_temp_valid = false;
    return;
  }

  if (!g_external_pending) {
    if (g_external_last_sample_ms != 0 &&
        (int32_t)(now_ms - g_external_last_sample_ms) < static_cast<int32_t>(kExternalTempSampleMs)) {
      return;
    }
    g_external_dallas->requestTemperaturesByAddress(g_external_addr);
    g_external_request_ms = now_ms;
    g_external_pending = true;
    return;
  }

  if ((int32_t)(now_ms - g_external_request_ms) < static_cast<int32_t>(kExternalTempConvertMs)) {
    return;
  }

  g_external_pending = false;
  g_external_last_sample_ms = now_ms;

  const float value_c = g_external_dallas->getTempC(g_external_addr);
  if (value_c == DEVICE_DISCONNECTED_C || value_c < -55.0f || value_c > 125.0f) {
    g_external_temp_valid = false;
    if (g_external_failures < 255) ++g_external_failures;
    if (g_external_failures >= kExternalTempMaxFailures) {
      Serial.println("[OneWire] Sensor disconnected, starting new search...");
      g_external_dallas = nullptr;
      g_external_pin = 0xFF;
      g_external_failures = 0;
      g_external_last_discovery_ms = now_ms - kExternalTempDiscoveryRetryMs;
    }
    return;
  }

  g_external_failures = 0;
  g_external_temp_valid = true;
  g_external_temp_c = value_c;
}
#else
static constexpr uint32_t kExternalTempMqttRepublishMs = 60000;
static uint32_t g_external_last_mqtt_ms = 0;
static String g_external_last_payload;

static void service_external_temp_sensor() {
  static bool warned = false;
  if (!warned) {
    warned = true;
    Serial.println("[OneWire] OneWire/DallasTemperature not found, external DS18x20 sensor disabled.");
  }
  g_external_temp_valid = false;
}
#endif

static void sync_external_temp_entity(bool publish_mqtt) {
  service_external_temp_sensor();
  const uint32_t now_ms = millis();
  store_external_temp_history(now_ms);

  String sensor_name = "Waveshare Intern DS18x20 Temperatur (GPIO 1/50)";
#if TAB5_HAS_ONEWIRE_DS18X20
  if (g_external_pin != 0xFF) {
    sensor_name = "Waveshare Intern DS18x20 Temperatur (GPIO ";
    sensor_name += String(static_cast<unsigned>(g_external_pin));
    sensor_name += ")";
  }
#endif
  haBridgeConfig.registerSensorMeta(kEntityExternalTemperature, sensor_name, "C");
  haBridgeConfig.updateEntityMeta(kEntityExternalTemperature, sensor_name, "C", "thermometer");

  char temp_payload[24];
  const char* payload = "unavailable";
  if (g_external_temp_valid && !std::isnan(g_external_temp_c) && !std::isinf(g_external_temp_c)) {
    dtostrf(g_external_temp_c, 0, 1, temp_payload);
    payload = temp_payload;
  }

  haBridgeConfig.updateSensorValue(kEntityExternalTemperature, payload);
  if (g_external_last_grid_payload != payload ||
      g_external_last_grid_ms == 0 ||
      (int32_t)(now_ms - g_external_last_grid_ms) >= static_cast<int32_t>(kExternalTempGridRefreshMs)) {
    update_all_grids(kEntityExternalTemperature, payload);
    g_external_last_grid_payload = payload;
    g_external_last_grid_ms = now_ms;
  }

  if (!publish_mqtt) return;
  if (!networkManager.isMqttConnected()) return;

  if (g_external_last_payload == payload &&
      (int32_t)(now_ms - g_external_last_mqtt_ms) < static_cast<int32_t>(kExternalTempMqttRepublishMs)) {
    return;
  }

  String topic = buildHaStatestreamTopic(kEntityExternalTemperature, "state");
  networkManager.mqttEnqueuePublish(topic.c_str(), payload, true);
  g_external_last_payload = payload;
  g_external_last_mqtt_ms = now_ms;

#if TAB5_HAS_ONEWIRE_DS18X20
  static bool reported_pin = false;
  if (!reported_pin && g_external_pin != 0xFF) {
    Serial.printf("[OneWire] MQTT publish to %s (GPIO %u)\n",
                  topic.c_str(),
                  static_cast<unsigned>(g_external_pin));
    reported_pin = true;
  }
#endif
}

static int readBatterySocPercent() {
  batteryStateUpdate();
  const BatteryTelemetry& batt = batteryStateGet();

  if (batt.level_valid && batt.level_pct >= 0 && batt.level_pct <= 100) {
    g_soc_pct = batt.level_pct;
  } else if (g_soc_pct < 0 &&
             !batt.battery_missing &&
             batt.raw_level_pct >= 0 &&
             batt.raw_level_pct <= 100) {
    // One-time seed on startup only. Do not continuously fall back to raw,
    // otherwise short raw spikes can create vertical jumps in history graphs.
    g_soc_pct = batt.raw_level_pct;
  }

  if (g_soc_pct < 0) return 0;
  if (g_soc_pct > 100) return 100;
  return g_soc_pct;
}

static void sync_internal_battery_entity() {
  if (!batteryStateSupportsMeasurement() || batteryStateIsBatteryMissing()) {
    return;
  }
  const int soc = readBatterySocPercent();
  char soc_payload[8];
  snprintf(soc_payload, sizeof(soc_payload), "%d", soc);

  const char* sensor_name = "WS_P4 Intern Batterie SoC";
  haBridgeConfig.registerSensorMeta(kEntityInternalBatterySoc, sensor_name, "%");
  haBridgeConfig.updateEntityMeta(kEntityInternalBatterySoc, sensor_name, "%", "battery");
  haBridgeConfig.updateSensorValue(kEntityInternalBatterySoc, soc_payload);
  update_all_grids(kEntityInternalBatterySoc, soc_payload);
}

static const char* kSleepOptionLabels[] = {
  "5 s",
  "15 s",
  "30 s",
  "60 s",
  "5 min",
  "15 min",
  "30 min",
  "60 min",
  "Nie"
};

static constexpr size_t kSleepOptionLabelCount =
    sizeof(kSleepOptionLabels) / sizeof(kSleepOptionLabels[0]);

static const char* kSleepOptionsJson =
    "[\"5 s\",\"15 s\",\"30 s\",\"60 s\",\"5 min\",\"15 min\",\"30 min\",\"60 min\",\"Nie\"]";

using RouteHandler = void (*)(const char* payload, size_t len);

struct TopicRoute {
  TopicKey key;
  RouteHandler handler;
  bool use_large_buffer;
};

struct DynamicSensorRoute {
  String topic;
  String entity_id;
  std::vector<uint8_t> slots;
};

static std::vector<DynamicSensorRoute> g_dynamic_routes;

struct DynamicWeatherRoute {
  String topic;
  String entity_id;
};

static std::vector<DynamicWeatherRoute> g_dynamic_weather_routes;

static void handleOutside(const char* payload, size_t) {
  g_outside_c = atof(payload);
}

static void handleInside(const char* payload, size_t) {
  g_inside_c = atof(payload);
}

static void handleSoc(const char* payload, size_t) {
  g_soc_pct = atoi(payload);
}

static void handleSceneCommand(const char* payload, size_t) {
  Serial.printf("Scene command received: %s\n", payload);
}

static void handleHaWohnTemp(const char* payload, size_t) {
  float v = atof(payload);
  Serial.printf("HA living area temperature: %s -> %.2f C\n", payload, v);
}

static bool parseBoolPayload(const char* payload, bool* out) {
  if (!payload || !out) return false;
  String s(payload);
  s.trim();
  s.toLowerCase();
  if (s == "1" || s == "on" || s == "true" || s == "yes") {
    *out = true;
    return true;
  }
  if (s == "0" || s == "off" || s == "false" || s == "no") {
    *out = false;
    return true;
  }
  return false;
}

static bool entityEquals(const char* entity_id, const char* expected) {
  if (!entity_id || !expected) return false;
  String a(entity_id);
  String b(expected);
  a.trim();
  b.trim();
  return a.equalsIgnoreCase(b);
}

static int brightnessPctFromRaw(int raw) {
  if (raw < 0) raw = 0;
  if (raw > 255) raw = 255;
  return Device::backlightPercentFromRaw(static_cast<uint8_t>(raw));
}

static uint8_t brightnessRawFromPct(int pct) {
  if (pct < Device::kConfiguredBrightnessPercentMin) {
    pct = Device::kConfiguredBrightnessPercentMin;
  }
  if (pct > 100) pct = 100;
  return Device::backlightRawFromPercent(static_cast<uint8_t>(pct));
}

static const char* sleepLabelFromConfig(bool enabled, uint16_t seconds) {
  if (!enabled) return "Nie";
  uint16_t closest = kSleepOptionsSec[0];
  size_t closest_index = 0;
  uint16_t best_diff = (seconds > closest) ? (seconds - closest) : (closest - seconds);
  for (size_t i = 1; i < kSleepOptionsSecCount; ++i) {
    uint16_t option = kSleepOptionsSec[i];
    uint16_t diff = (seconds > option) ? (seconds - option) : (option - seconds);
    if (diff < best_diff) {
      best_diff = diff;
      closest = option;
      closest_index = i;
    }
  }
  if (closest_index >= kSleepOptionLabelCount - 1) {
    closest_index = kSleepOptionLabelCount - 2;
  }
  return kSleepOptionLabels[closest_index];
}

static bool parseSleepPayload(const char* payload, bool* enabled, uint16_t* seconds) {
  if (!payload || !enabled || !seconds) return false;
  String s(payload);
  s.trim();
  s.toLowerCase();
  if (!s.length()) return false;

  if (s == "nie" || s == "never" || s == "off" || s == "0") {
    *enabled = false;
    return true;
  }

  for (size_t i = 0; i + 1 < kSleepOptionLabelCount; ++i) {
    String label = kSleepOptionLabels[i];
    label.toLowerCase();
    if (s == label) {
      *enabled = true;
      *seconds = kSleepOptionsSec[i];
      return true;
    }
  }

  String compact = s;
  compact.replace(" ", "");
  bool is_min = compact.endsWith("min") || compact.endsWith("m");
  bool is_sec = compact.endsWith("s");
  int value = 0;
  bool found_digit = false;
  for (size_t i = 0; i < compact.length(); ++i) {
    char c = compact.charAt(i);
    if (c >= '0' && c <= '9') {
      value = value * 10 + (c - '0');
      found_digit = true;
    } else if (found_digit) {
      break;
    }
  }
  if (!found_digit) return false;

  uint32_t secs = is_min ? (uint32_t)value * 60u : (uint32_t)value;
  if (!is_min && !is_sec && value > 0 && value <= 3600) {
    secs = (uint32_t)value;
  }
  if (secs == 0) {
    *enabled = false;
    return true;
  }

  *enabled = true;
  *seconds = static_cast<uint16_t>(secs);
  return true;
}

static void handleDisplayBrightnessCommand(const char* payload, size_t) {
  if (!payload || !*payload) return;
  int command_value = atoi(payload);
  uint8_t value = 0;
  if (command_value > 100) {
    // Compatibility with Bridge versions that used the old 121..255 raw
    // protocol. New firmware and Bridge versions exchange 1..100 percent.
    if (command_value > 255) command_value = 255;
    const uint8_t configured_min = Device::backlightRawFromPercent(
        Device::kConfiguredBrightnessPercentMin);
    if (command_value < configured_min) {
      command_value = configured_min;
    }
    value = static_cast<uint8_t>(command_value);
  } else {
    value = brightnessRawFromPct(command_value);
  }

  // An HA change to normal brightness must not brighten a visible
  // screensaver. During sleep, update only the safe wake brightness and
  // keep the backlight off.
  if (!is_image_screensaver_visible()) {
    powerManager.setDisplayBrightness(value);
  }

  const DeviceConfig& cfg = configManager.getConfig();
  configManager.saveDisplaySettings(
      value,
      cfg.auto_sleep_enabled,
      cfg.auto_sleep_seconds,
      cfg.auto_sleep_battery_enabled,
      cfg.auto_sleep_battery_seconds,
      cfg.display_rotation_mode,
      cfg.display_rotated_180,
      cfg.display_rotation_quarters,
      cfg.wake_mode_mains,
      cfg.wake_mode_battery);
  mqttPublishDeviceSettings();
}

static void handleScreensaverBrightnessCommand(const char* payload, size_t) {
  if (!payload || !*payload) return;
  int percent = atoi(payload);
  if (percent < Device::kConfiguredBrightnessPercentMin) {
    percent = Device::kConfiguredBrightnessPercentMin;
  }
  if (percent > kScreensaverBrightnessPctMax) {
    percent = kScreensaverBrightnessPctMax;
  }

  configManager.saveScreensaverBrightness(static_cast<uint8_t>(percent));
  image_screensaver_brightness_changed();
  mqttPublishDeviceSettings();
}

static void handleDisplayRotateCommand(const char* payload, size_t) {
  bool rotate = false;
  if (!parseBoolPayload(payload, &rotate)) return;
  displayManager.setRotationFlipped(rotate);
  settings_sync_display_rotation(rotate);

  const DeviceConfig& cfg = configManager.getConfig();
  uint8_t rotation_quarters = rotate ? Device::kRotationFlipped : Device::kRotationDefault;
  uint8_t rotation_mode = rotate ? kDisplayRotationFlipped : kDisplayRotationNormal;
  configManager.saveDisplaySettings(
      cfg.display_brightness,
      cfg.auto_sleep_enabled,
      cfg.auto_sleep_seconds,
      cfg.auto_sleep_battery_enabled,
      cfg.auto_sleep_battery_seconds,
      rotation_mode,
      rotate,
      rotation_quarters,
      cfg.wake_mode_mains,
      cfg.wake_mode_battery);
  mqttPublishDeviceSettings();
}

static void handleDisplaySleepCommand(const char* payload, size_t) {
  bool sleep = false;
  if (!parseBoolPayload(payload, &sleep)) return;
  if (sleep) {
    powerManager.enterDisplaySleep();
  } else {
    powerManager.wakeFromDisplaySleep("mqtt-cmd");
  }
  mqttPublishDeviceSettings();
}

static void handleSleepMainsCommand(const char* payload, size_t) {
  bool enabled = false;
  uint16_t seconds = 0;
  if (!parseSleepPayload(payload, &enabled, &seconds)) return;

  const DeviceConfig& cfg = configManager.getConfig();
  uint16_t new_seconds = enabled ? seconds : cfg.auto_sleep_seconds;
  configManager.saveDisplaySettings(
      cfg.display_brightness,
      enabled,
      new_seconds,
      cfg.auto_sleep_battery_enabled,
      cfg.auto_sleep_battery_seconds,
      cfg.display_rotation_mode,
      cfg.display_rotated_180,
      cfg.display_rotation_quarters,
      cfg.wake_mode_mains,
      cfg.wake_mode_battery);
  mqttPublishDeviceSettings();
}

static void handleSleepBatteryCommand(const char* payload, size_t) {
  bool enabled = false;
  uint16_t seconds = 0;
  if (!parseSleepPayload(payload, &enabled, &seconds)) return;

  const DeviceConfig& cfg = configManager.getConfig();
  uint16_t new_seconds = enabled ? seconds : cfg.auto_sleep_battery_seconds;
  configManager.saveDisplaySettings(
      cfg.display_brightness,
      cfg.auto_sleep_enabled,
      cfg.auto_sleep_seconds,
      enabled,
      new_seconds,
      cfg.display_rotation_mode,
      cfg.display_rotated_180,
      cfg.display_rotation_quarters,
      cfg.wake_mode_mains,
      cfg.wake_mode_battery);
  mqttPublishDeviceSettings();
}

static void sync_local_device_entities(bool publish_mqtt) {
  const DeviceConfig& cfg = configManager.getConfig();

  const int bright_pct = brightnessPctFromRaw(static_cast<int>(cfg.display_brightness));
  char bright_payload[96];
  snprintf(bright_payload, sizeof(bright_payload), "{\"state\":\"on\",\"brightness_pct\":%d}", bright_pct);

  haBridgeConfig.updateSensorValue(kEntityDisplayBrightness, bright_payload);
  haBridgeConfig.updateEntityMeta(kEntityDisplayBrightness, "Display Helligkeit", "%", "brightness-6");

  char screensaver_bright_payload[96];
  snprintf(screensaver_bright_payload, sizeof(screensaver_bright_payload),
           "{\"state\":\"on\",\"brightness_pct\":%u}",
           static_cast<unsigned>(cfg.screensaver_brightness_pct));
  haBridgeConfig.updateSensorValue(kEntityScreensaverBrightness,
                                   screensaver_bright_payload);
  haBridgeConfig.updateEntityMeta(kEntityScreensaverBrightness,
                                  "Screensaver Helligkeit", "%",
                                  "brightness-4");

  const char* rotate_state = cfg.display_rotated_180 ? "ON" : "OFF";
  haBridgeConfig.updateSensorValue(kEntityDisplayRotate, rotate_state);
  haBridgeConfig.updateEntityMeta(kEntityDisplayRotate, "Display Rotation", "", "screen-rotation");

  const char* sleep_state = powerManager.isInSleep() ? "ON" : "OFF";
  haBridgeConfig.updateSensorValue(kEntityDisplaySleep, sleep_state);
  haBridgeConfig.updateEntityMeta(kEntityDisplaySleep, "Display Sleep", "", "sleep");

  update_all_grids(kEntityDisplayBrightness, bright_payload);
  update_all_grids(kEntityScreensaverBrightness,
                   screensaver_bright_payload);
  update_all_grids(kEntityDisplayRotate, rotate_state);
  update_all_grids(kEntityDisplaySleep, sleep_state);
  sync_internal_battery_entity();
  // Explicit local I/O owns all external temperature telemetry.
  (void)publish_mqtt;
}

static bool resolve_toggle_action(const char* action, bool current, bool* desired) {
  if (!desired) return false;
  if (!action || !*action) {
    *desired = !current;
    return true;
  }
  String s(action);
  s.trim();
  s.toLowerCase();
  if (s == "toggle") {
    *desired = !current;
    return true;
  }
  return parseBoolPayload(s.c_str(), desired);
}

static bool handle_local_switch_command(const char* entity_id, const char* action) {
  if (hardwareIo.handleLocalEntityCommand(entity_id, action)) return true;
  if (entityEquals(entity_id, kEntityDisplayBrightness)) {
    sync_local_device_entities(false);
    return true;
  }
  if (entityEquals(entity_id, kEntityScreensaverBrightness)) {
    sync_local_device_entities(false);
    return true;
  }
  if (entityEquals(entity_id, kEntityDisplayRotate)) {
    bool desired = false;
    const DeviceConfig& cfg = configManager.getConfig();
    if (!resolve_toggle_action(action, cfg.display_rotated_180, &desired)) return true;
    handleDisplayRotateCommand(desired ? "ON" : "OFF", 0);
    return true;
  }
  if (entityEquals(entity_id, kEntityDisplaySleep)) {
    bool desired = false;
    if (!resolve_toggle_action(action, powerManager.isInSleep(), &desired)) return true;
    handleDisplaySleepCommand(desired ? "ON" : "OFF", 0);
    return true;
  }
  return false;
}

static bool handle_local_light_command(const char* entity_id, const char* state, int brightness_pct) {
  if (entityEquals(entity_id, kEntityDisplayBrightness)) {
    if (brightness_pct >= 0) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", brightness_pct);
      handleDisplayBrightnessCommand(buf, 0);
    } else {
      (void)state;
      sync_local_device_entities(false);
    }
    return true;
  }
  if (entityEquals(entity_id, kEntityScreensaverBrightness)) {
    if (brightness_pct >= 0) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", brightness_pct);
      handleScreensaverBrightnessCommand(buf, 0);
    } else {
      (void)state;
      sync_local_device_entities(false);
    }
    return true;
  }
  return false;
}

static void handleCameraStatus(const char* payload, size_t) {
  camera_popup_handle_mqtt_status(payload);
}

static const TopicRoute kRoutes[] = {
  {TopicKey::SENSOR_OUT, handleOutside, false},
  {TopicKey::SENSOR_IN, handleInside, false},
  {TopicKey::SENSOR_SOC, handleSoc, false},
  {TopicKey::SCENE_CMND, handleSceneCommand, false},
  {TopicKey::HA_WOHN_TEMP, handleHaWohnTemp, false},
  {TopicKey::DISPLAY_BRIGHTNESS_CMND, handleDisplayBrightnessCommand, false},
  {TopicKey::SCREENSAVER_BRIGHTNESS_CMND, handleScreensaverBrightnessCommand, false},
  {TopicKey::DISPLAY_ROTATE_CMND, handleDisplayRotateCommand, false},
  {TopicKey::DISPLAY_SLEEP_CMND, handleDisplaySleepCommand, false},
  {TopicKey::SLEEP_MAINS_CMND, handleSleepMainsCommand, false},
  {TopicKey::SLEEP_BAT_CMND, handleSleepBatteryCommand, false},
  {TopicKey::CAMERA_STAT, handleCameraStatus, true},
};

static String buildHaStatestreamTopic(const String& entity_id, const char* suffix) {
  String topic = mqttTopics.haPrefix();
  if (!topic.length()) {
    topic = "ha/statestream";
  }
  topic += "/";
  for (size_t i = 0; i < entity_id.length(); ++i) {
    char c = entity_id.charAt(i);
    topic += (c == '.') ? '/' : c;
  }
  topic += "/";
  topic += (suffix && *suffix) ? suffix : "state";
  return topic;
}

static void rebuildDynamicRoutes(std::vector<DynamicSensorRoute>& routes) {
  routes.clear();

  auto add_route = [&](const String& entity, int slot_index, const char* suffix = "state") {
    String ent = entity;
    ent.trim();
    if (!ent.length()) return;
    // Physical panel I/O entities use the same IDs as HA but are updated
    // and controlled directly on this panel. A parallel HA statestream
    // subscription would be redundant and could overwrite real GPIO state
    // with a stale remote value.
    if (hardwareIo.isLocalEntityId(ent.c_str())) return;

    String topic = buildHaStatestreamTopic(ent, suffix);
    auto it = std::find_if(
        routes.begin(),
        routes.end(),
        [&](const DynamicSensorRoute& r) { return r.topic == topic; });

    if (it == routes.end()) {
      DynamicSensorRoute route;
      route.topic = topic;
      route.entity_id = ent;
      if (slot_index >= 0) {
        route.slots.push_back(static_cast<uint8_t>(slot_index));
      }
      routes.push_back(route);
    } else {
      if (slot_index >= 0) {
        it->slots.push_back(static_cast<uint8_t>(slot_index));
      }
      if (!it->entity_id.equalsIgnoreCase(ent)) {
        it->entity_id = ent;  // prefer latest casing
      }
    }
  };

  const HaBridgeConfigData& cfg = haBridgeConfig.get();
  for (const String* list : {&cfg.numbers_text, &cfg.selects_text, &cfg.datetimes_text}) {
    int start = 0;
    while (start < static_cast<int>(list->length())) {
      int end = list->indexOf('\n', start);
      if (end < 0) end = list->length();
      add_route(list->substring(start, end), -1, "control");
      start = end + 1;
    }
  }
  // Legacy HA sensor slots
  for (uint8_t slot = 0; slot < HA_SENSOR_SLOT_COUNT; ++slot) {
    add_route(cfg.sensor_slots[slot], slot);
  }

  // Load sensor routes from every folder through TileConfig's PSRAM entity
  // cache. Each folder needs a roughly 20 ms flash read only on the first
  // scan or after a grid change; subsequent full-folder scans are cheap.
  bool has_media_tiles = cfg.numbers_text.length() || cfg.selects_text.length() || cfg.datetimes_text.length();
  auto add_grid_entities = [&](const FolderEntitySlotView* slots, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      const FolderEntitySlotView& slot = slots[i];
      if (tileTypeSubscribesDynamicState(slot.type) &&
          slot.entity[0]) {
        add_route(String(slot.entity), -1, tileTypeIsEditableValue(slot.type) ? "control" : "state");
        if (tileTypeIsEditableValue(slot.type)) has_media_tiles = true;
        if (slot.type == TILE_MEDIA) {
          has_media_tiles = true;
          // New bridges publish cover-free state changes here first. Keep the
          // normal state subscription as well for retained full payloads and
          // compatibility with older bridge versions.
          add_route(String(slot.entity), -1, "state_fast");
        }
      }
    }
  };

  // Scan all folders, not just the active one
  const std::vector<FolderEntry>& folders = tileConfig.getFolders();
  FolderEntitySlotView slots[TILES_PER_GRID];
  for (const auto& folder : folders) {
    uint32_t t_load0 = millis();
    bool loaded = tileConfig.getFolderEntitiesCached(folder.id, slots, TILES_PER_GRID);
    uint32_t load_ms = millis() - t_load0;
    if (load_ms >= 5) {
      Serial.printf("[Bridge]   getFolderEntitiesCached(%u): %u ms\n", folder.id, (unsigned)load_ms);
    }
    if (loaded) {
      add_grid_entities(slots, TILES_PER_GRID);
    }
    // This runs synchronously on the loop task (mqtt_process_inbound_queue ->
    // processMqttMessage -> mqttReloadDynamicSlots), before lv_timer_handler()
    // gets its turn at the end of loop(). On a cache miss (first scan / after
    // a grid change) each folder still costs a flash read -- servicing LVGL
    // between folders keeps the UI breathing during the reload.
    lvglServiceDuringBlockingWork();
  }

  // The screensaver grid intentionally lives outside the folder tree.
  // Add its entities separately to the same MQTT routes.
  const TileGridConfig& screensaver_grid = screensaverConfig.tileGrid();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = screensaver_grid.tiles[i];
    if (!tileTypeSubscribesScreensaverState(tile.type) ||
        !tile.sensor_entity.length()) {
      continue;
    }
    add_route(tile.sensor_entity, -1, tileTypeIsEditableValue(tile.type) ? "control" : "state");
    if (tileTypeIsEditableValue(tile.type)) has_media_tiles = true;
    if (tile.type == TILE_MEDIA) {
      has_media_tiles = true;
      add_route(tile.sensor_entity, -1, "state_fast");
    }
  }

  // Media states with embedded covers need about 19 KB, exceeding the
  // 16 KB base receive buffer; the worker adjusts its size.
  networkManager.setMqttMediaBufferNeeded(has_media_tiles);
}

// Scan at boot before networkManager.init(). If stored configuration
// already contains media tiles, allocate the matching receive buffer
// immediately so the first retained cover is not lost while buffer
// growth waits for the startup burst to finish.
bool mqttAnyMediaTileConfigured() {
  const TileGridConfig& screensaver_grid = screensaverConfig.tileGrid();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = screensaver_grid.tiles[i];
    if ((tile.type == TILE_MEDIA || tileTypeIsEditableValue(tile.type)) && tile.sensor_entity.length()) return true;
  }
  const std::vector<FolderEntry>& folders = tileConfig.getFolders();
  FolderEntitySlotView slots[TILES_PER_GRID];
  for (const auto& folder : folders) {
    if (!tileConfig.getFolderEntitiesCached(folder.id, slots, TILES_PER_GRID)) continue;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      if ((slots[i].type == TILE_MEDIA || tileTypeIsEditableValue(slots[i].type)) && slots[i].entity[0]) return true;
    }
  }
  return false;
}

static void rebuildDynamicWeatherRoutes(std::vector<DynamicWeatherRoute>& routes) {
  routes.clear();

  auto add_route = [&](const String& entity) {
    String ent = entity;
    ent.trim();
    if (!ent.length()) return;

    String topic = buildHaStatestreamTopic(ent, "weather");
    auto it = std::find_if(
        routes.begin(),
        routes.end(),
        [&](const DynamicWeatherRoute& r) { return r.topic == topic; });

    if (it == routes.end()) {
      DynamicWeatherRoute route;
      route.topic = topic;
      route.entity_id = ent;
      routes.push_back(route);
    } else if (!it->entity_id.equalsIgnoreCase(ent)) {
      it->entity_id = ent;
    }
  };

  const std::vector<FolderEntry>& folders = tileConfig.getFolders();
  FolderEntitySlotView slots[TILES_PER_GRID];
  for (const auto& folder : folders) {
    uint32_t t_load0 = millis();
    bool loaded = tileConfig.getFolderEntitiesCached(folder.id, slots, TILES_PER_GRID);
    uint32_t load_ms = millis() - t_load0;
    if (load_ms >= 5) {
      Serial.printf("[Bridge]   getFolderEntitiesCached(%u): %u ms\n", folder.id, (unsigned)load_ms);
    }
    if (loaded) {
      for (size_t i = 0; i < TILES_PER_GRID; ++i) {
        const FolderEntitySlotView& slot = slots[i];
        if (slot.type == TILE_WEATHER && slot.entity[0]) {
          add_route(String(slot.entity));
        }
      }
    }
    // Same reasoning as rebuildDynamicRoutes() above: give LVGL a turn between
    // folders so this synchronous MQTT-callback reload doesn't stall the screen.
    lvglServiceDuringBlockingWork();
  }
}

static bool tryHandleDynamicSensor(const char* topic, const char* payload,
                                   size_t payload_len) {
  for (const auto& route : g_dynamic_routes) {
    if (route.topic == topic) {
      if (route.topic.endsWith("/control")) {
        if (payload_len <= EDITABLE_PAYLOAD_MAX) queue_editable_value(route.entity_id, payload);
        return true;
      }
      if (route.entity_id.startsWith("binary_sensor.") &&
          payload_len > BINARY_SENSOR_PAYLOAD_MAX) {
        static uint32_t last_oversize_log_ms = 0;
        const uint32_t now_ms = millis();
        if (last_oversize_log_ms == 0 ||
            static_cast<uint32_t>(now_ms - last_oversize_log_ms) >= 5000) {
          last_oversize_log_ms = now_ms;
          Serial.printf(
              "[BinarySensor] Oversized state dropped: %s (%u bytes)\n",
              route.entity_id.c_str(), static_cast<unsigned>(payload_len));
        }
        return true;
      }
      // Update tile-based system (display) - active folder only
      tiles_update_sensor_by_entity(GridType::TAB0, route.entity_id.c_str(), payload);
      // Update sensor values map (for web interface)
      haBridgeConfig.updateSensorValue(route.entity_id, payload);
      return true;
    }
  }
  return false;
}

static bool tryHandleDynamicWeather(const char* topic, const char* payload) {
  for (const auto& route : g_dynamic_weather_routes) {
    if (route.topic == topic) {
      tiles_update_weather_by_entity(GridType::TAB0, route.entity_id.c_str(), payload);
      return true;
    }
  }
  return false;
}

static constexpr size_t SMALL_BUF = 96;
static constexpr size_t LARGE_BUF = 32768;
static constexpr size_t CFG_BUF = 32768;
static char small_buf[SMALL_BUF];
static char* g_large_buf = nullptr;
static char* g_cfg_buf = nullptr;

static char* mqttScratchBuffer(char*& slot, size_t bytes, const char* name) {
  if (slot) return slot;
  slot = static_cast<char*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  const char* where = "PSRAM";
  if (!slot) {
    slot = static_cast<char*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    where = "internal";
  }
  if (!slot) {
    Serial.printf("[MQTT] Scratch buffer alloc failed: %s %u bytes\n",
                  name ? name : "?",
                  static_cast<unsigned>(bytes));
    return nullptr;
  }
  Serial.printf("[MQTT] Scratch buffer %s: %u bytes in %s\n",
                name ? name : "?",
                static_cast<unsigned>(bytes),
                where);
  return slot;
}

static char* mqttLargeBuffer() {
  return mqttScratchBuffer(g_large_buf, LARGE_BUF, "large");
}

static char* mqttConfigBuffer() {
  return mqttScratchBuffer(g_cfg_buf, CFG_BUF, "config");
}

// ---------------------------------------------------------------------------
// Inbound MQTT decoupling
//
// PubSubClient::loop() reassembles a whole incoming message (an ~11.6KB bridge
// payload can take ~170ms over several TCP segments) and then calls mqttCallback
// synchronously on whatever task ran loop(). Doing the heavy per-topic work
// right there is what stalls the render loop. Split it: mqttCallback() only
// copies the raw (topic,payload) into a PSRAM-backed message and pushes a
// pointer onto a FreeRTOS queue; mqtt_process_inbound_queue() (called from the
// main loop) drains it and runs the real processing, processMqttMessage(),
// entirely on the loop task. All existing flash, LVGL and shared-state
// access stays there. The worker only reads the socket, allocates one
// PSRAM message and enqueues it; those application paths need no locks.
// ---------------------------------------------------------------------------
struct MqttInboundMsg {
  char* topic;       // -> into the same backing allocation
  uint8_t* payload;  // -> into the same backing allocation
  unsigned int length;
};

// A complete retained reconnect includes 46 dynamic topics plus Bridge
// and Settings messages. A 16-entry queue filled during the measured
// 1.3-second route reload and discarded media state. Messages live in
// PSRAM; increasing only the queued pointers accommodates the burst
// without significant SRAM use.
static constexpr size_t kMqttInboundQueueDepth = 64;
static QueueHandle_t g_mqtt_inbound_queue = nullptr;
static MqttInboundMsg* g_deferred_bridge_apply = nullptr;

static void processMqttMessage(char* topic, uint8_t* payload, unsigned int length);

static QueueHandle_t mqttInboundQueue() {
  if (!g_mqtt_inbound_queue) {
    g_mqtt_inbound_queue = xQueueCreate(kMqttInboundQueueDepth, sizeof(MqttInboundMsg*));
    if (!g_mqtt_inbound_queue) {
      Serial.println("[MQTT] Could not create inbound queue");
    }
  }
  return g_mqtt_inbound_queue;
}

// One allocation per message, laid out as [MqttInboundMsg][topic\0][payload].
// PSRAM preferred (freed by the drainer after processMqttMessage()).
static MqttInboundMsg* mqttAllocInbound(const char* topic,
                                        const uint8_t* payload,
                                        unsigned int length,
                                        size_t packet_capacity,
                                        bool* invalid_out) {
  if (invalid_out) *invalid_out = false;

  // The callback topic starts at most MQTT_MAX_HEADER_SIZE + 1 bytes into the
  // PubSubClient buffer. Keep the bounded scan inside that buffer even if a
  // future parser regression forgets the terminator.
  const size_t topic_prefix_reserve = MQTT_MAX_HEADER_SIZE + 1U;
  if (!topic || (!payload && length != 0) ||
      packet_capacity <= topic_prefix_reserve) {
    if (invalid_out) *invalid_out = true;
    return nullptr;
  }
  const size_t topic_scan_limit = packet_capacity - topic_prefix_reserve;
  const size_t topic_len = strnlen(topic, topic_scan_limit);
  if (topic_len == 0 || topic_len == topic_scan_limit) {
    if (invalid_out) *invalid_out = true;
    return nullptr;
  }

  size_t total = 0;
  if (!hometiles_mqtt::checkedInboundAllocationSize(
          sizeof(MqttInboundMsg), topic_len, length, packet_capacity,
          &total)) {
    if (invalid_out) *invalid_out = true;
    return nullptr;
  }

  uint8_t* block = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!block) block = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_8BIT));
  if (!block) return nullptr;
  MqttInboundMsg* msg = reinterpret_cast<MqttInboundMsg*>(block);
  msg->topic = reinterpret_cast<char*>(block + sizeof(MqttInboundMsg));
  msg->payload = reinterpret_cast<uint8_t*>(msg->topic + topic_len + 1);
  msg->length = length;
  if (topic_len) memcpy(msg->topic, topic, topic_len);
  msg->topic[topic_len] = '\0';
  if (length) memcpy(msg->payload, payload, length);
  return msg;
}

void mqtt_process_inbound_queue(uint8_t max_msgs) {
  QueueHandle_t q = g_mqtt_inbound_queue;  // do NOT lazily create here -- nothing to drain if never used
  uint8_t processed = 0;

  // A bridge/apply payload rebuilds several indexes and schedules hidden tile
  // cache work. Keep MQTT receiving while the camera popup is visible, but
  // retain only the newest full bridge snapshot in PSRAM and apply it after
  // the popup closes. Normal sensor/switch/media messages continue below.
  const bool camera_busy = camera_popup_is_busy();
  if (!camera_busy && g_deferred_bridge_apply &&
      (max_msgs == 0 || processed < max_msgs)) {
    MqttInboundMsg* deferred = g_deferred_bridge_apply;
    g_deferred_bridge_apply = nullptr;
    Serial.printf(
        "[Bridge] Applying deferred full synchronization after camera (%u bytes)\n",
        deferred->length);
    processMqttMessage(
        deferred->topic, deferred->payload, deferred->length);
    heap_caps_free(deferred);
    ++processed;
  }

  if (!q) return;
  MqttInboundMsg* msg = nullptr;
  while ((max_msgs == 0 || processed < max_msgs) && xQueueReceive(q, &msg, 0) == pdTRUE) {
    if (msg) {
      const char* apply_topic = networkManager.getBridgeApplyTopic();
      if (camera_busy && apply_topic &&
          strcmp(msg->topic, apply_topic) == 0) {
        if (g_deferred_bridge_apply) {
          heap_caps_free(g_deferred_bridge_apply);
        }
        g_deferred_bridge_apply = msg;
        Serial.printf(
            "[Bridge] Full synchronization deferred while camera is active (%u bytes)\n",
            msg->length);
        msg = nullptr;
        ++processed;
        continue;
      }
      processMqttMessage(msg->topic, msg->payload, msg->length);
      heap_caps_free(msg);
      ++processed;
    }
  }
}

// ========== MQTT Callback (thin: enqueue only) ==========
void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
  // How much real time passed since the LVGL tick was last serviced, before we
  // even get here. This is the pure socket-read / loop-cadence latency (the
  // reassembly of a large multi-segment payload), independent of our own
  // processing -- which now runs later, on the loop drainer.
  uint32_t entry_since_tick = millis() - g_lvgl_tick_last_ms;
  if (entry_since_tick >= 50) {
    Serial.printf("[Bridge] mqttCallback entry: %ums since last LVGL tick service\n",
                  (unsigned)entry_since_tick);
  }

  QueueHandle_t q = mqttInboundQueue();
  bool invalid = false;
  const size_t packet_capacity = networkManager.getMqttBufferSize();
  MqttInboundMsg* msg =
      q ? mqttAllocInbound(topic, payload, length, packet_capacity, &invalid)
        : nullptr;
  if (invalid) {
    static uint32_t invalid_drop_count = 0;
    const uint32_t count = ++invalid_drop_count;
    if (count == 1 || (count % 10U) == 0U) {
      Serial.printf(
          "[MQTT] Dropped invalid inbound packet: payload=%u, capacity=%u "
          "(count=%u)\n",
          static_cast<unsigned>(length),
          static_cast<unsigned>(packet_capacity),
          static_cast<unsigned>(count));
    }
    return;
  }
  if (!msg) {
    Serial.printf("[MQTT] Inbound allocation/queue unavailable; dropped '%s'\n",
                  topic ? topic : "?");
    return;
  }
  // mqttCallback runs on the owning MQTT worker. It must never fall back to
  // inline processMqttMessage(), which touches flash and LVGL on the loop
  // task. Briefly wait for a full queue to drain, then drop. Rate-limit
  // drop logs because display sleep drains only about every 150 ms.
  if (xQueueSend(q, &msg, pdMS_TO_TICKS(50)) != pdTRUE) {
    heap_caps_free(msg);
    static uint32_t last_drop_log_ms = 0;
    const uint32_t drop_now_ms = millis();
    if (last_drop_log_ms == 0 || (uint32_t)(drop_now_ms - last_drop_log_ms) >= 5000) {
      last_drop_log_ms = drop_now_ms;
      Serial.printf("[MQTT] Inbound queue full -> message dropped (latest: %s)\n",
                    topic ? topic : "?");
    }
  }
}

// ========== MQTT message processing (Topic-Routing, runs on the loop) ==========
static void processMqttMessage(char* topic, uint8_t* payload, unsigned int length) {
  yield();  // Let the web server run.

  // Local relays use a small direct topic path, avoiding JSON parsing
  // while normal Bridge and camera messages retain the established router.
  if (hardwareIo.handleMqttMessage(topic, payload, length)) return;

  if (viewNavigationHandleMessage(topic, reinterpret_cast<const char*>(payload), length)) return;
  if (editable_handle_ack(topic, reinterpret_cast<const char*>(payload), length)) return;
  const char* apply_topic = networkManager.getBridgeApplyTopic();
  if (apply_topic && strcmp(topic, apply_topic) == 0) {
    char* cfg_buf = mqttConfigBuffer();
    if (!cfg_buf) return;
    if (length >= CFG_BUF) {
      Serial.printf("[Bridge] WARNING: Payload too large (%u bytes), truncating!\n", length);
    }
    size_t copy_len = length < CFG_BUF - 1 ? length : CFG_BUF - 1;
    memcpy(cfg_buf, payload, copy_len);
    cfg_buf[copy_len] = '\0';
    yield();  // After the large copy.
    Serial.printf("[Bridge] apply-topic hit (%u bytes)\n", (unsigned)copy_len);
    bool reload = false;
    bool icons_changed = false;
    uint32_t t_parse0 = millis();
    bool applied = haBridgeConfig.applyJson(cfg_buf, &reload, &icons_changed);
    Serial.printf("[Bridge] applyJson: %u ms\n", (unsigned)(millis() - t_parse0));
    if (applied) {
      Serial.println("[Bridge] Configuration received from HA");
      // applyJson replaces Bridge metadata and value maps. Restore panel-local
      // I/O entities immediately so the tile editor and runtime cache retain
      // them without depending on the Bridge.
      hardwareIo.refreshLocalEntityCache();
      // Only the first successful Bridge sync releases the boot sleep guard
      // and starts the idle timer. Later periodic apply refreshes used to reset
      // that timer repeatedly, preventing a 60-minute screensaver from starting.
      if (!g_bridge_initial_sync_complete) {
        g_bridge_initial_sync_complete = true;
        powerManager.allowSleep();
        displayManager.resetActivityTimer();
        Serial.println("[Bridge] Initial sync: idle timer started once");
      }
      tiles_request_bridge_cache_refresh();
      if (reload) {
        yield();  // After JSON parsing.
        // Do not announce the device again: HA answers each announcement with
        // bridge/apply, so replying here creates an apply/announce feedback loop.
        // Refresh dynamic topics after a quiet window and coalesce repeated reloads.
        mqttRequestDynamicSlotsReload();
        Serial.println("[Bridge] mqttReloadDynamicSlots scheduled after quiet period");
      }
      if (icons_changed) {
        tiles_request_icon_refresh();
        queue_sensor_popup_icon_refresh();
      }
      sync_local_device_entities(false);
    } else {
      Serial.println("[Bridge] Invalid bridge configuration received");
    }
    return;
  }

  const char* icons_topic = networkManager.getBridgeIconsTopic();
  if (icons_topic && strcmp(topic, icons_topic) == 0) {
    char* large_buf = mqttLargeBuffer();
    if (!large_buf) return;
    size_t copy_len = length < LARGE_BUF - 1 ? length : LARGE_BUF - 1;
    memcpy(large_buf, payload, copy_len);
    large_buf[copy_len] = '\0';
    if (haBridgeConfig.applyIconUpdate(large_buf)) {
      tiles_request_icon_refresh();
      queue_sensor_popup_icon_refresh();
    }
    return;
  }

  bool processed_static = false;
  for (const auto& route : kRoutes) {
    const char* expected = mqttTopics.topic(route.key);
    if (!expected || strcmp(topic, expected) != 0) {
      continue;
    }

    char* buf = route.use_large_buffer ? mqttLargeBuffer() : small_buf;
    if (!buf) return;
    size_t buf_len = route.use_large_buffer ? LARGE_BUF : sizeof(small_buf);
    size_t copy_len = length < (buf_len - 1) ? length : (buf_len - 1);
    memcpy(buf, payload, copy_len);
    buf[copy_len] = '\0';

    route.handler(buf, copy_len);
    processed_static = true;
    break;
  }

  if (processed_static) {
    return;
  }

  // Check dynamic sensor routes; larger JSON may require the large buffer.
  char* dyn_buf = (length < SMALL_BUF) ? small_buf : mqttLargeBuffer();
  if (!dyn_buf) return;
  size_t dyn_len = (length < SMALL_BUF) ? sizeof(small_buf) : LARGE_BUF;
  size_t copy_len = length < (dyn_len - 1) ? length : (dyn_len - 1);
  memcpy(dyn_buf, payload, copy_len);
  dyn_buf[copy_len] = '\0';
  if (tryHandleDynamicSensor(topic, dyn_buf, copy_len)) {
    yield();  // After the sensor update.
    return;
  }
  if (tryHandleDynamicWeather(topic, dyn_buf)) {
    yield();
    return;
  }

  const char* history_topic = networkManager.getHistoryResponseTopic();
  if (history_topic && strcmp(topic, history_topic) == 0) {
    char* large_buf = mqttLargeBuffer();
    if (!large_buf) return;
    size_t copy_len = length < (LARGE_BUF - 1) ? length : (LARGE_BUF - 1);
    memcpy(large_buf, payload, copy_len);
    large_buf[copy_len] = '\0';
    String response_entity;
    const bool has_entity =
        extract_json_string_field(large_buf, "entity_id", response_entity) &&
        response_entity.length();
    String response_kind;
    extract_json_string_field(large_buf, "kind", response_kind);
    const bool is_binary = response_kind.equalsIgnoreCase("binary");
    const bool is_state = response_kind.equalsIgnoreCase("state");
    const bool is_discrete = is_binary || is_state;
    if (has_entity) {
      if (is_discrete) {
        uint16_t response_hours = 0;
        extract_json_uint16_field(large_buf, "hours", response_hours);
        clear_pending_discrete_history_request(
            response_entity.c_str(), response_kind.c_str(), response_hours);
        if (is_state) {
          // A new Bridge may upgrade a legacy numeric request after inspecting
          // the entity metadata. Clear that old pending slot as well.
          clear_pending_history_request(response_entity.c_str());
        }
      } else {
        clear_pending_history_request(response_entity.c_str());
      }
    }
    Serial.printf("[%s] Response received: %s, %u bytes\n",
                  is_binary ? "BinaryHistory"
                            : (is_state ? "StateHistory" : "History"),
                  response_entity.length() ? response_entity.c_str()
                                           : "unknown entity",
                  static_cast<unsigned>(copy_len));
    queue_sensor_popup_history(nullptr, large_buf, copy_len);
    if (!is_discrete) {
      queue_tile_graph_history(nullptr, large_buf, copy_len);
    }
    return;
  }

  const char* energy_topic = networkManager.getEnergyResponseTopic();
  if (energy_topic && strcmp(topic, energy_topic) == 0) {
    char* large_buf = mqttLargeBuffer();
    if (!large_buf) return;
    size_t copy_len = length < (LARGE_BUF - 1) ? length : (LARGE_BUF - 1);
    memcpy(large_buf, payload, copy_len);
    large_buf[copy_len] = '\0';
    queue_energy_response(large_buf, copy_len);
    return;
  }

  Serial.printf("MQTT: Unhandled topic %s\n", topic);
}

// ========== Subscribe to topics ==========
void mqttSubscribeTopics() {
  networkManager.mqttEnqueueSubscribe((mqttTopics.deviceBase() + "/stat/value").c_str());
  for (const auto& route : kRoutes) {
    const char* tpc = mqttTopics.topic(route.key);
    if (!tpc || !*tpc) continue;
    if (networkManager.mqttEnqueueSubscribe(tpc)) {
      Serial.printf("MQTT: subscribe queued %s\n", tpc);
    }
  }

  // After MQTT reconnects, the broker has no subscriptions for this session.
  // Subscribe to every existing route once without first queueing pointless
  // unsubscribe commands.
  mqttReloadDynamicSlots(true);
  hardwareIo.subscribeMqttTopics();
}

// ========== Publish home snapshot ==========
void mqttPublishHomeSnapshot() {
  if (!networkManager.isMqttConnected()) return;

  char buf[24];
  dtostrf(g_outside_c, 0, 1, buf);
  networkManager.mqttEnqueuePublish(mqttTopics.topic(TopicKey::SENSOR_OUT), buf, true);

  dtostrf(g_inside_c, 0, 1, buf);
  networkManager.mqttEnqueuePublish(mqttTopics.topic(TopicKey::SENSOR_IN), buf, true);

  if (batteryStateSupportsMeasurement()) {
    const int soc = readBatterySocPercent();
    if (batteryStateHasDisplayPercent()) snprintf(buf, sizeof(buf), "%d", soc);
    else snprintf(buf, sizeof(buf), "unavailable");
    networkManager.mqttEnqueuePublish(mqttTopics.topic(TopicKey::SENSOR_SOC), buf, true);
  } else {
    // Remove the old retained synthetic zero rather than publishing false data.
    networkManager.mqttEnqueuePublish(mqttTopics.topic(TopicKey::SENSOR_SOC), "", true);
  }
}

// ========== Publish device settings ==========
void mqttPublishDeviceSettings() {
  sync_local_device_entities(false);
  if (!networkManager.isMqttConnected()) return;

  const DeviceConfig& cfg = configManager.getConfig();

  auto publish_state = [&](TopicKey key, const char* payload) {
    const char* tpc = mqttTopics.topic(key);
    if (!tpc || !*tpc || !payload) return;
    networkManager.mqttEnqueuePublish(tpc, payload, true);
  };

  char buf[16];
  snprintf(buf, sizeof(buf), "%d",
           brightnessPctFromRaw(static_cast<int>(cfg.display_brightness)));
  publish_state(TopicKey::DISPLAY_BRIGHTNESS_STAT, buf);
  snprintf(buf, sizeof(buf), "%u",
           static_cast<unsigned>(cfg.screensaver_brightness_pct));
  publish_state(TopicKey::SCREENSAVER_BRIGHTNESS_STAT, buf);
  publish_state(TopicKey::DISPLAY_ROTATE_STAT, cfg.display_rotated_180 ? "ON" : "OFF");
  publish_state(TopicKey::DISPLAY_SLEEP_STAT, powerManager.isInSleep() ? "ON" : "OFF");

  publish_state(TopicKey::SLEEP_MAINS_STAT,
                sleepLabelFromConfig(cfg.auto_sleep_enabled, cfg.auto_sleep_seconds));
  publish_state(TopicKey::SLEEP_BAT_STAT,
                sleepLabelFromConfig(cfg.auto_sleep_battery_enabled, cfg.auto_sleep_battery_seconds));
}

void mqttServiceLocalSensors() {
  service_pending_history_fallback();
  // Advance one due 1-Wire phase per call; the relay-only path is cheap.
  // The state machine never waits synchronously for DS18x20 conversion.
  hardwareIo.service();

  static uint32_t last_run_ms = 0;
  const uint32_t now_ms = millis();
  if (last_run_ms != 0 && (int32_t)(now_ms - last_run_ms) < 500) {
    return;
  }
  last_run_ms = now_ms;
  sync_internal_battery_entity();

}

// ========== Publish scene command ==========
void mqttPublishScene(const char* scene_name) {
  if (!scene_name || !*scene_name) return;
  const String entity = haBridgeConfig.findSceneEntity(scene_name);
  if (entity.length() && !ha_control::supportsSceneTile(entity.c_str())) return;

  if (!networkManager.isMqttConnected()) {
    Serial.printf("Scene command skipped (MQTT offline): %s\n", scene_name);
    return;
  }

  bool queued = networkManager.mqttEnqueuePublish(mqttTopics.topic(TopicKey::SCENE_CMND), scene_name, false);
  Serial.printf("Scene command -> MQTT '%s' (%s)\n", scene_name, queued ? "queued" : "queue-full");
}

// ========== Publish light/switch command ==========
void mqttPublishSwitchCommand(const char* entity_id, const char* state) {
  if (!entity_id || !*entity_id) return;
  if (handle_local_switch_command(entity_id, state)) return;
  if (!ha_control::supportsSwitchTile(entity_id)) return;

  if (!networkManager.isMqttConnected()) {
    Serial.printf("Switch command skipped (MQTT offline): %s\n", entity_id);
    return;
  }

  const char* action = (state && *state) ? state : "toggle";
  const char* topic = nullptr;
  if (strncmp(entity_id, "light.", 6) == 0) {
    topic = mqttTopics.topic(TopicKey::LIGHT_CMND);
  } else {
    topic = mqttTopics.topic(TopicKey::SWITCH_CMND);
  }

  if (!topic || !*topic) {
    Serial.printf("Switch command skipped (no topic): %s\n", entity_id);
    return;
  }

  char payload[256];
  snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\",\"state\":\"%s\"}", entity_id, action);
  bool queued = networkManager.mqttEnqueuePublish(topic, payload, false);
  Serial.printf("Switch command -> MQTT '%s' (%s)\n", topic, queued ? "queued" : "queue-full");
}

void mqttPublishMediaCommand(const char* entity_id, const char* command) {
  if (!entity_id || !*entity_id) return;

  if (!networkManager.isMqttConnected()) {
    Serial.printf("Media command skipped (MQTT offline): %s\n", entity_id);
    return;
  }

  const char* topic = mqttTopics.topic(TopicKey::MEDIA_CMND);
  if (!topic || !*topic) {
    Serial.printf("Media command skipped (no topic): %s\n", entity_id);
    return;
  }

  const char* action = (command && *command) ? command : "play_pause";
  char payload[256];
  snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\",\"command\":\"%s\"}", entity_id, action);
  bool queued = networkManager.mqttEnqueuePublishPriority(topic, payload, false);
  Serial.printf("Media command -> MQTT '%s' (%s, priority)\n",
                topic, queued ? "queued" : "queue-full");
}

void mqttPublishMediaSeek(const char* entity_id, float position_seconds) {
  if (!entity_id || !*entity_id) return;
  if (position_seconds < 0.0f) position_seconds = 0.0f;

  if (!networkManager.isMqttConnected()) {
    Serial.printf("Media seek skipped (MQTT offline): %s\n", entity_id);
    return;
  }

  const char* topic = mqttTopics.topic(TopicKey::MEDIA_CMND);
  if (!topic || !*topic) {
    Serial.printf("Media seek skipped (no topic): %s\n", entity_id);
    return;
  }

  char payload[288];
  snprintf(payload,
           sizeof(payload),
           "{\"entity_id\":\"%s\",\"command\":\"media_seek\",\"seek_position\":%.1f}",
           entity_id,
           position_seconds);
  bool queued = networkManager.mqttEnqueuePublishPriority(topic, payload, false);
  Serial.printf("Media seek -> MQTT '%s' %.1fs (%s, priority)\n",
                topic,
                position_seconds,
                queued ? "queued" : "queue-full");
}

void mqttPublishMediaVolume(const char* entity_id, float volume_level) {
  if (!entity_id || !*entity_id) return;
  if (volume_level < 0.0f) volume_level = 0.0f;
  if (volume_level > 1.0f) volume_level = 1.0f;

  if (!networkManager.isMqttConnected()) {
    Serial.printf("Media volume skipped (MQTT offline): %s\n", entity_id);
    return;
  }

  const char* topic = mqttTopics.topic(TopicKey::MEDIA_CMND);
  if (!topic || !*topic) {
    Serial.printf("Media volume skipped (no topic): %s\n", entity_id);
    return;
  }

  char payload[288];
  snprintf(payload,
           sizeof(payload),
           "{\"entity_id\":\"%s\",\"command\":\"volume_set\",\"volume_level\":%.3f}",
           entity_id,
           volume_level);
  bool queued = networkManager.mqttEnqueuePublishPriority(topic, payload, false);
  Serial.printf("Media volume -> MQTT '%s' %.0f%% (%s, priority)\n",
                topic, volume_level * 100.0f,
                queued ? "queued" : "queue-full");
}

void mqttPublishMediaMute(const char* entity_id, bool muted) {
  if (!entity_id || !*entity_id) return;

  if (!networkManager.isMqttConnected()) {
    Serial.printf("Media mute skipped (MQTT offline): %s\n", entity_id);
    return;
  }

  const char* topic = mqttTopics.topic(TopicKey::MEDIA_CMND);
  if (!topic || !*topic) {
    Serial.printf("Media mute skipped (no topic): %s\n", entity_id);
    return;
  }

  char payload[288];
  snprintf(payload,
           sizeof(payload),
           "{\"entity_id\":\"%s\",\"command\":\"volume_mute\",\"is_volume_muted\":%s}",
           entity_id,
           muted ? "true" : "false");
  bool queued = networkManager.mqttEnqueuePublishPriority(topic, payload, false);
  Serial.printf("Media mute -> MQTT '%s' %s (%s, priority)\n",
                topic, muted ? "on" : "off",
                queued ? "queued" : "queue-full");
}

void mqttPublishClimateTemperature(const char* entity_id,
                                   float temperature,
                                   bool use_range,
                                   float target_low,
                                   float target_high) {
  if (!entity_id || !*entity_id) return;
  if (!networkManager.isMqttConnected()) {
    Serial.printf("Climate temperature skipped (MQTT offline): %s\n", entity_id);
    return;
  }
  const char* topic = mqttTopics.topic(TopicKey::CLIMATE_CMND);
  if (!topic || !*topic) return;

  char payload[320];
  if (use_range) {
    snprintf(payload, sizeof(payload),
             "{\"entity_id\":\"%s\",\"command\":\"set_temperature\","
             "\"target_temp_low\":%.2f,\"target_temp_high\":%.2f}",
             entity_id, target_low, target_high);
  } else {
    snprintf(payload, sizeof(payload),
             "{\"entity_id\":\"%s\",\"command\":\"set_temperature\","
             "\"temperature\":%.2f}",
             entity_id, temperature);
  }
  const bool queued =
      networkManager.mqttEnqueuePublishPriority(topic, payload, false);
  Serial.printf("Climate temperature -> MQTT '%s' (%s, priority)\n",
                topic, queued ? "queued" : "queue-full");
}

void mqttPublishClimateHumidity(const char* entity_id, float humidity) {
  if (!entity_id || !*entity_id) return;
  if (!networkManager.isMqttConnected()) {
    Serial.printf("Climate humidity skipped (MQTT offline): %s\n", entity_id);
    return;
  }
  const char* topic = mqttTopics.topic(TopicKey::CLIMATE_CMND);
  if (!topic || !*topic) return;

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"entity_id\":\"%s\",\"command\":\"set_humidity\","
           "\"humidity\":%.1f}",
           entity_id, humidity);
  const bool queued =
      networkManager.mqttEnqueuePublishPriority(topic, payload, false);
  Serial.printf("Climate humidity -> MQTT '%s' (%s, priority)\n",
                topic, queued ? "queued" : "queue-full");
}

void mqttPublishClimateHvacMode(const char* entity_id, const char* hvac_mode) {
  if (!entity_id || !*entity_id || !hvac_mode || !*hvac_mode) return;
  if (!networkManager.isMqttConnected()) {
    Serial.printf("Climate mode skipped (MQTT offline): %s\n", entity_id);
    return;
  }
  const char* topic = mqttTopics.topic(TopicKey::CLIMATE_CMND);
  if (!topic || !*topic) return;

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"entity_id\":\"%s\",\"command\":\"set_hvac_mode\","
           "\"hvac_mode\":\"%s\"}",
           entity_id, hvac_mode);
  const bool queued =
      networkManager.mqttEnqueuePublishPriority(topic, payload, false);
  Serial.printf("Climate mode -> MQTT '%s' (%s, priority)\n",
                topic, queued ? "queued" : "queue-full");
}

void mqttPublishClimatePresetMode(
    const char* entity_id, const char* preset_mode) {
  if (!entity_id || !*entity_id || !preset_mode || !*preset_mode) return;
  if (!networkManager.isMqttConnected()) {
    Serial.printf("Climate preset skipped (MQTT offline): %s\n", entity_id);
    return;
  }
  const char* topic = mqttTopics.topic(TopicKey::CLIMATE_CMND);
  if (!topic || !*topic) return;

  char payload[272];
  snprintf(payload, sizeof(payload),
           "{\"entity_id\":\"%s\",\"command\":\"set_preset_mode\","
           "\"preset_mode\":\"%s\"}",
           entity_id, preset_mode);
  const bool queued =
      networkManager.mqttEnqueuePublishPriority(topic, payload, false);
  Serial.printf("Climate preset -> MQTT '%s' (%s, priority)\n",
                topic, queued ? "queued" : "queue-full");
}

static void mqtt_publish_climate_option(
    const char* entity_id,
    const char* command,
    const char* field,
    const char* value,
    const char* log_name) {
  if (!entity_id || !*entity_id || !command || !*command ||
      !field || !*field || !value || !*value) {
    return;
  }
  if (!networkManager.isMqttConnected()) {
    Serial.printf("Climate %s skipped (MQTT offline): %s\n",
                  log_name, entity_id);
    return;
  }
  const char* topic = mqttTopics.topic(TopicKey::CLIMATE_CMND);
  if (!topic || !*topic) return;

  char payload[304];
  snprintf(payload, sizeof(payload),
           "{\"entity_id\":\"%s\",\"command\":\"%s\",\"%s\":\"%s\"}",
           entity_id, command, field, value);
  const bool queued =
      networkManager.mqttEnqueuePublishPriority(topic, payload, false);
  Serial.printf("Climate %s -> MQTT '%s' (%s, priority)\n",
                log_name, topic, queued ? "queued" : "queue-full");
}

void mqttPublishClimateFanMode(
    const char* entity_id, const char* fan_mode) {
  mqtt_publish_climate_option(
      entity_id, "set_fan_mode", "fan_mode", fan_mode, "fan");
}

void mqttPublishClimateSwingMode(
    const char* entity_id, const char* swing_mode) {
  mqtt_publish_climate_option(
      entity_id, "set_swing_mode", "swing_mode", swing_mode, "swing");
}

void mqttPublishClimateHorizontalSwingMode(
    const char* entity_id, const char* swing_mode) {
  mqtt_publish_climate_option(
      entity_id, "set_swing_horizontal_mode",
      "swing_horizontal_mode", swing_mode, "horizontal swing");
}

void mqttPublishCoverCommand(const char* entity_id,
                             const char* command,
                             int position) {
  if (!entity_id || !*entity_id || !command || !*command) return;

  static const char* const kCommands[] = {
      "open_cover", "close_cover", "stop_cover", "set_cover_position",
      "open_cover_tilt", "close_cover_tilt", "stop_cover_tilt",
      "set_cover_tilt_position", "toggle", "toggle_cover_tilt"};
  bool supported_command = false;
  for (const char* allowed : kCommands) {
    if (strcmp(command, allowed) == 0) {
      supported_command = true;
      break;
    }
  }
  if (!supported_command) {
    Serial.printf("Cover command rejected: %s\n", command);
    return;
  }
  if (!networkManager.isMqttConnected()) {
    Serial.printf("Cover command skipped (MQTT offline): %s\n", entity_id);
    return;
  }

  const char* topic = mqttTopics.topic(TopicKey::COVER_CMND);
  if (!topic || !*topic) return;
  if (position < 0) position = 0;
  if (position > 100) position = 100;

  char payload[384];
  int written = 0;
  if (strcmp(command, "set_cover_position") == 0) {
    written = snprintf(
        payload, sizeof(payload),
        "{\"entity_id\":\"%s\",\"command\":\"%s\",\"position\":%d}",
        entity_id, command, position);
  } else if (strcmp(command, "set_cover_tilt_position") == 0) {
    written = snprintf(
        payload, sizeof(payload),
        "{\"entity_id\":\"%s\",\"command\":\"%s\",\"tilt_position\":%d}",
        entity_id, command, position);
  } else {
    written = snprintf(
        payload, sizeof(payload),
        "{\"entity_id\":\"%s\",\"command\":\"%s\"}",
        entity_id, command);
  }
  if (written < 0 || static_cast<size_t>(written) >= sizeof(payload)) {
    Serial.printf("Cover command rejected (payload too long): %s\n", entity_id);
    return;
  }
  const bool queued =
      networkManager.mqttEnqueuePublishPriority(topic, payload, false);
  Serial.printf("Cover command -> MQTT '%s' command=%s (%s, priority)\n",
                topic, command, queued ? "queued" : "queue-full");
}

void mqttPublishLightCommand(const char* entity_id,
                             const char* state,
                             int brightness_pct,
                             bool has_color,
                             uint32_t color,
                             int color_temp_kelvin) {
  if (!entity_id || !*entity_id) return;
  if (handle_local_light_command(entity_id, state, brightness_pct)) return;

  if (!networkManager.isMqttConnected()) {
    Serial.printf("Light command skipped (MQTT offline): %s\n", entity_id);
    return;
  }

  const char* topic = mqttTopics.topic(TopicKey::LIGHT_CMND);
  if (!topic || !*topic) {
    Serial.printf("Light command skipped (no topic): %s\n", entity_id);
    return;
  }

  String payload = "{\"entity_id\":\"";
  payload += entity_id;
  payload += "\"";

  if (state && *state) {
    payload += ",\"state\":\"";
    payload += state;
    payload += "\"";
  }

  if (brightness_pct >= 0) {
    if (brightness_pct > 100) brightness_pct = 100;
    payload += ",\"brightness_pct\":";
    payload += brightness_pct;
  }

  if (has_color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    payload += ",\"rgb_color\":[";
    payload += r;
    payload += ",";
    payload += g;
    payload += ",";
    payload += b;
    payload += "]";
  }

  if (color_temp_kelvin > 0) {
    payload += ",\"color_temp_kelvin\":";
    payload += color_temp_kelvin;
  }

  payload += "}";

  bool queued = networkManager.mqttEnqueuePublish(topic, payload.c_str(), false);
  Serial.printf("Light command -> MQTT '%s' (%s)\n", topic, queued ? "queued" : "queue-full");
}

void mqttPublishHistoryRequest(const char* entity_id,
                               uint16_t hours,
                               uint16_t period_minutes,
                               uint16_t points) {
  if (!entity_id || !*entity_id) return;
  // Local live values are available without the Bridge, but history is not.
  // Do not start unnecessary HA history requests for panel I/O.
  if (hardwareIo.isLocalEntityId(entity_id)) return;

  if (hours == 0) hours = 24;
  if (period_minutes == 0) period_minutes = 5;
  if (points == 0) {
    points = static_cast<uint16_t>((static_cast<uint32_t>(hours) * 60U) / period_minutes);
  }
  if (points == 0) points = 1;

  const bool mqtt_online = networkManager.isMqttConnected();
  const char* history_topic = networkManager.getHistoryRequestTopic();
  const bool can_request_ha = mqtt_online && history_topic && *history_topic;
  const bool time_valid = has_valid_local_time_for_history();

  // Discard previous pending entries for the same entity.
  clear_pending_history_request(entity_id);

  // Prefer HA history when HA is reachable.
  if (can_request_ha) {
    if (!time_valid && is_internal_tab5_entity(entity_id)) {
      Serial.printf("[History] Local time invalid, requesting HA history for %s\n", entity_id);
    }

    String payload = "{\"entity_id\":\"";
    payload += entity_id;
    payload += "\",\"hours\":";
    payload += String(hours);
    payload += ",\"period_minutes\":";
    payload += String(period_minutes);
    payload += ",\"points\":";
    payload += String(points);
    payload += ",\"stat\":\"mean\"}";
    // The user is waiting for this graph. A route reload can leave many
    // safely paced subscribe commands ahead of the history request. Appending
    // it behind that burst would let the 2-second fallback empty the graph
    // before the request was even sent.
    bool ok = networkManager.mqttEnqueuePublishWithLargeBuffer(
        history_topic, payload.c_str(), false, 20000, true);
    Serial.printf("History request -> MQTT '%s' (%s, priority)\n",
                  history_topic, ok ? "queued" : "queue-full");
    if (ok) {
      mark_pending_history_request(entity_id, millis(), hours, period_minutes, points);
      return;
    }
    queue_history_fallback_for_entity(entity_id, time_valid, "HA publish failed",
                                      hours, period_minutes, points);
    return;
  }

  if (queue_history_fallback_for_entity(entity_id, time_valid, "HA unavailable",
                                        hours, period_minutes, points)) {
    return;
  }

  if (!mqtt_online) {
    Serial.printf("History request skipped (MQTT offline): %s\n", entity_id);
  } else if (!history_topic || !*history_topic) {
    Serial.printf("History request skipped (no topic): %s\n", entity_id);
  }
}

static void mqttPublishDiscreteHistoryRequest(const char* entity_id,
                                              const char* kind,
                                              const char* log_tag,
                                              uint16_t hours,
                                              uint16_t max_transitions) {
  if (!entity_id || !*entity_id) return;

  hours = hours == 168 ? 168 : 24;
  if (max_transitions == 0) max_transitions = 48;
  if (max_transitions < 2) max_transitions = 2;
  if (max_transitions > 96) max_transitions = 96;

  // Only one popup can be visible. A new request supersedes any pending
  // request for the previous popup entity or range.
  g_pending_discrete_history = {};
  const bool mqtt_online = networkManager.isMqttConnected();
  const char* history_topic = networkManager.getHistoryRequestTopic();
  if (!mqtt_online || !history_topic || !*history_topic) {
    queue_discrete_history_unavailable(
        entity_id, kind, hours,
        mqtt_online ? "missing_topic" : "mqtt_offline");
    Serial.printf("[%s] Request unavailable for %s (%s)\n", log_tag,
                  entity_id,
                  mqtt_online ? "missing topic" : "MQTT offline");
    return;
  }

  String payload;
  payload.reserve(160);
  payload = "{\"version\":1,\"kind\":\"";
  payload += kind;
  payload += "\",\"entity_id\":\"";
  payload += entity_id;
  payload += "\",\"hours\":";
  payload += String(hours);
  payload += ",\"max_transitions\":";
  payload += String(max_transitions);
  payload += "}";

  const bool queued = networkManager.mqttEnqueuePublishWithLargeBuffer(
      history_topic, payload.c_str(), false, 20000, true);
  Serial.printf("[%s] Request -> MQTT '%s' (%s, priority)\n", log_tag,
                history_topic, queued ? "queued" : "queue-full");
  if (queued) {
    mark_pending_discrete_history_request(entity_id, kind, hours);
    return;
  }

  queue_discrete_history_unavailable(entity_id, kind, hours,
                                     "publish_failed");
}

void mqttPublishBinaryHistoryRequest(const char* entity_id,
                                     uint16_t hours,
                                     uint16_t max_transitions) {
  mqttPublishDiscreteHistoryRequest(entity_id, "binary", "BinaryHistory",
                                    hours, max_transitions);
}

void mqttPublishStateHistoryRequest(const char* entity_id,
                                    uint16_t hours,
                                    uint16_t max_transitions) {
  mqttPublishDiscreteHistoryRequest(entity_id, "state", "StateHistory",
                                    hours, max_transitions);
}

void mqttPublishWeatherRequest(const char* entity_id) {
  if (!entity_id || !*entity_id) return;

  if (!networkManager.isMqttConnected()) return;

  const char* weather_topic = networkManager.getWeatherRequestTopic();
  if (!weather_topic || !*weather_topic) return;

  String payload = "{\"entity_id\":\"";
  payload += entity_id;
  payload += "\"}";

  bool queued = networkManager.mqttEnqueuePublish(weather_topic, payload.c_str(), false);
  Serial.printf("Weather request -> MQTT '%s' (%s)\n", weather_topic, queued ? "queued" : "queue-full");
}

bool mqttPublishEnergyRequest(const char* period) {
  const char* p = (period && *period) ? period : "day";
  if (strcmp(p, "week") != 0 && strcmp(p, "month") != 0) {
    p = "day";
  }

  if (!networkManager.isMqttConnected()) {
    Serial.printf("Energy request skipped (MQTT offline): %s\n", p);
    return false;
  }

  const char* energy_topic = networkManager.getEnergyRequestTopic();
  if (!energy_topic || !*energy_topic) {
    Serial.printf("Energy request skipped (no topic): %s\n", p);
    return false;
  }

  String payload = "{\"period\":\"";
  payload += p;
  payload += "\"}";

  // Changing the selected range is interactive like a history request.
  // Queue it ahead of a long subscribe burst without touching the client
  // from the loop task or bypassing P4 SDIO guard intervals.
  bool queued = networkManager.mqttEnqueuePublishWithLargeBuffer(
      energy_topic, payload.c_str(), false, 12000, true);
  Serial.printf("Energy request -> MQTT '%s' period=%s (%s, priority)\n",
                energy_topic,
                p,
                queued ? "queued" : "queue-full");
  return queued;
}

// ========== Home Assistant MQTT Discovery ==========
void mqttPublishDiscovery() {
  if (!networkManager.isMqttConnected()) return;

  Serial.println("Publishing Home Assistant discovery payloads...");

  char did[24];
  buildDeviceId(did, sizeof(did));

  char tpc[128];

  // Remove legacy discovery entries: these sensors and buttons now come
  // from the HA integration (tab5_lvgl/HomeTiles Bridge). They were
  // retained MQTT discovery messages; an empty retained payload removes
  // each entry. Otherwise every device_id change, such as the full-MAC fix,
  // leaves another orphan "Waveshare P4 Panel" in native MQTT discovery.
  // Four such phantom devices were observed on one installation.
  const char* legacy_configs[] = {
    "outside_c", "inside_c", "external_c", "soc_pct", "uptime",
  };
  for (const char* leaf : legacy_configs) {
    snprintf(tpc, sizeof(tpc), "homeassistant/sensor/%s_%s/config", did, leaf);
    networkManager.mqttEnqueuePublish(tpc, "", true);
  }
  const char* legacy_buttons[] = {
    "scene_abend", "scene_lesen", "scene_allesaus",
  };
  for (const char* leaf : legacy_buttons) {
    snprintf(tpc, sizeof(tpc), "homeassistant/button/%s_%s/config", did, leaf);
    networkManager.mqttEnqueuePublish(tpc, "", true);
  }

  Serial.println("Legacy Home Assistant discovery cleared");
}

static bool topicListContains(const std::vector<String>& topics,
                              const String& needle) {
  return std::find(topics.begin(), topics.end(), needle) != topics.end();
}

void mqttReloadDynamicSlots(bool subscribe_all) {
  const bool mqtt_online = networkManager.isMqttConnected();
  std::vector<String> old_topics;
  old_topics.reserve(g_dynamic_routes.size() + g_dynamic_weather_routes.size());
  for (const auto& route : g_dynamic_routes) {
    if (!topicListContains(old_topics, route.topic)) old_topics.push_back(route.topic);
  }
  for (const auto& route : g_dynamic_weather_routes) {
    if (!topicListContains(old_topics, route.topic)) old_topics.push_back(route.topic);
  }

  uint32_t t_sensor0 = millis();
  rebuildDynamicRoutes(g_dynamic_routes);
  Serial.printf("[Bridge] rebuildDynamicRoutes: %u ms\n", (unsigned)(millis() - t_sensor0));
  uint32_t t_weather0 = millis();
  rebuildDynamicWeatherRoutes(g_dynamic_weather_routes);
  Serial.printf("[Bridge] rebuildDynamicWeatherRoutes: %u ms\n", (unsigned)(millis() - t_weather0));

  std::vector<String> new_topics;
  new_topics.reserve(g_dynamic_routes.size() + g_dynamic_weather_routes.size());
  for (const auto& route : g_dynamic_routes) {
    if (!topicListContains(new_topics, route.topic)) new_topics.push_back(route.topic);
  }
  for (const auto& route : g_dynamic_weather_routes) {
    if (!topicListContains(new_topics, route.topic)) new_topics.push_back(route.topic);
  }

  uint32_t t_control0 = millis();
  size_t unsubscribe_count = 0;
  size_t subscribe_count = 0;
  if (mqtt_online) {
    // For ordinary Bridge refreshes, apply only the actual route difference.
    // Previously every old topic was unsubscribed and every new topic
    // resubscribed each time; two quick refreshes could exceed 128 control
    // commands and continually overflow the queue.
    if (!subscribe_all) {
      for (const auto& topic : old_topics) {
        if (topicListContains(new_topics, topic)) continue;
        if (networkManager.mqttEnqueueUnsubscribe(topic.c_str())) {
          ++unsubscribe_count;
        }
        lvglServiceDuringBlockingWork();
      }
    }
    for (const auto& topic : new_topics) {
      if (!subscribe_all && topicListContains(old_topics, topic)) continue;
      if (networkManager.mqttEnqueueSubscribe(topic.c_str())) {
        ++subscribe_count;
        Serial.printf("MQTT: subscribe queued %s\n", topic.c_str());
      }
      lvglServiceDuringBlockingWork();
    }
  }
  Serial.printf("[Bridge] mqttReloadDynamicSlots: -%u +%u Topics in %u ms%s\n",
                static_cast<unsigned>(unsubscribe_count),
                static_cast<unsigned>(subscribe_count),
                static_cast<unsigned>(millis() - t_control0),
                subscribe_all ? " (reconnect)" : "");
}

void mqttRequestDynamicSlotsReload(uint32_t quiet_ms) {
  if (quiet_ms == 0) quiet_ms = kDynamicSlotReloadQuietMs;
  const uint32_t due = millis() + quiet_ms;
  if (!g_dynamic_slots_reload_requested ||
      (int32_t)(due - g_dynamic_slots_reload_due_ms) > 0) {
    g_dynamic_slots_reload_due_ms = due;
  }
  g_dynamic_slots_reload_requested = true;
}

void mqttServiceDynamicSlotsReload() {
  if (!g_dynamic_slots_reload_requested) return;

  const uint32_t now = millis();
  if (webAdminRecentlyActive(kDynamicSlotReloadAdminQuietMs)) {
    g_dynamic_slots_reload_due_ms = now + kDynamicSlotReloadQuietMs;
    return;
  }
  if ((int32_t)(now - g_dynamic_slots_reload_due_ms) < 0) {
    return;
  }

  g_dynamic_slots_reload_requested = false;
  uint32_t t_slots0 = millis();
  mqttReloadDynamicSlots();
  Serial.printf("[Bridge] deferred mqttReloadDynamicSlots: %u ms\n",
                (unsigned)(millis() - t_slots0));
}

// ========== Post-Connect (Loop-Task) ==========
// The MQTT worker marks each successful reconnect pending. This function
// consumes that flag once per loop iteration and starts the application
// layer on the loop task: route reload scans flash folders and services
// LVGL; settings/snapshot publication reads battery I2C and updates grids.
// All resulting publishes and subscribes return through the outbound queue.
void mqttServicePostConnect() {
  if (!networkManager.consumeMqttPostConnectPending()) return;
  Serial.println("[MQTT] Post-connect: subscriptions/discovery/settings (loop task)");
  viewNavigationConnected();
  mqttSubscribeTopics();
  mqttPublishDiscovery();
  mqttPublishDeviceSettings();
  mqttPublishHomeSnapshot();
  // Announce the exact MAC-based Bridge topic after every connection. Without
  // this retained message the HA integration cannot discover a new device or
  // repair an entry that still points at an older device ID. The publish uses
  // the large-buffer queue, which is held back until the startup storm ends.
  networkManager.publishBridgeConfig();
}

void mqttPublishCameraCommand(const char* entity_id, const char* command) {
  if (!entity_id || !*entity_id) return;
  const auto& text = i18n::strings(configManager.getConfig().language);
  if (!networkManager.isMqttConnected()) {
    camera_popup_set_status(text.camera_mqtt_disconnected, true);
    return;
  }

  const char* topic = mqttTopics.topic(TopicKey::CAMERA_CMND);
  if (!topic || !*topic) {
    camera_popup_set_status(text.camera_mqtt_topic_missing, true);
    return;
  }

  const char* action = (command && *command) ? command : "open";
  char payload[384];
  if (strcmp(action, "open") == 0) {
    snprintf(payload, sizeof(payload),
             "{\"entity_id\":\"%s\",\"command\":\"%s\","
             "\"width\":%u,\"height\":%u,\"fps\":%u,"
             "\"transport\":\"tcp-ack-v1\",\"protocol_version\":1}",
             entity_id, action,
             camera_geometry::kWidth,
             camera_geometry::kHeight,
             camera_geometry::kFps);
  } else {
    snprintf(payload, sizeof(payload),
             "{\"entity_id\":\"%s\",\"command\":\"%s\"}",
             entity_id, action);
  }
  const bool queued =
      networkManager.mqttEnqueuePublishPriority(topic, payload, false);
  Serial.printf("[Camera] command %s -> %s (%s)\n", action, topic,
                queued ? "queued" : "queue-full");
  if (!queued) camera_popup_set_status(text.camera_mqtt_queue_full, true);
}
