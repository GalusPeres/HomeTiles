#include "src/types/energy/energy_data.h"

#include <ArduinoJson.h>
#include <cstring>
#include <math.h>
#include <utility>
#include <vector>

#include "src/core/power/power_manager.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/network/network_manager.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"

namespace {

struct PendingEnergyResponse {
  String payload;
  bool valid = false;
};

// Responses for different periods must not overwrite each other, for example
// when the periodic 24-hour response arrives just after a manual 7-day request.
PendingEnergyResponse g_pending_day;
PendingEnergyResponse g_pending_week;
PendingEnergyResponse g_pending_month;
std::vector<EnergyEntryData> g_day_entries;
std::vector<EnergyEntryData> g_week_entries;
std::vector<EnergyEntryData> g_month_entries;

struct EnergyRequestState {
  uint32_t last_attempt_ms = 0;  // loop-task only
  volatile bool awaiting_response = false;
  volatile bool retry_requested = false;
};

EnergyRequestState g_day_request;
EnergyRequestState g_week_request;
EnergyRequestState g_month_request;
uint32_t g_last_periodic_ms = 0;

constexpr uint32_t kEnergyRequestThrottleMs = 10000UL;
constexpr uint32_t kEnergyRetryBackoffMs = 2000UL;
constexpr uint32_t kEnergyResponseTimeoutMs = 15000UL;

const char* normalize_period(const char* period) {
  if (!period || !*period) return "day";
  if (strcmp(period, "week") == 0) return "week";
  if (strcmp(period, "month") == 0) return "month";
  return "day";
}

PendingEnergyResponse& pending_for_period(const char* period) {
  const char* p = normalize_period(period);
  if (strcmp(p, "week") == 0) return g_pending_week;
  if (strcmp(p, "month") == 0) return g_pending_month;
  return g_pending_day;
}

// Read only the small period field before the normal loop parses the complete
// JSON response. This avoids a second large JSON document or payload copy.
const char* response_period(const char* payload) {
  if (!payload) return "day";
  const char* key = strstr(payload, "\"period\"");
  if (!key) return "day";
  const char* colon = strchr(key + 8, ':');
  if (!colon) return "day";
  const char* begin = strchr(colon + 1, '"');
  if (!begin) return "day";
  ++begin;
  const char* end = strchr(begin, '"');
  if (!end) return "day";
  const size_t len = static_cast<size_t>(end - begin);
  if (len == 4 && strncmp(begin, "week", len) == 0) return "week";
  if (len == 5 && strncmp(begin, "month", len) == 0) return "month";
  return "day";
}

EnergyRequestState& request_state_for_period(const char* period) {
  const char* p = normalize_period(period);
  if (strcmp(p, "week") == 0) return g_week_request;
  if (strcmp(p, "month") == 0) return g_month_request;
  return g_day_request;
}

bool enqueue_energy_request(const char* period,
                            EnergyRequestState& state,
                            uint32_t now) {
  state.last_attempt_ms = now;
  // Mark before enqueueing across tasks: the MQTT worker may send the request
  // and receive its response before this call returns to the loop task.
  state.awaiting_response = true;
  state.retry_requested = false;
  const bool queued = mqttPublishEnergyRequest(period);
  if (!queued) {
    state.awaiting_response = false;
    state.retry_requested = true;
  }
  return queued;
}

void service_energy_retry(const char* period,
                          EnergyRequestState& state,
                          uint32_t now) {
  if (state.awaiting_response) {
    if ((uint32_t)(now - state.last_attempt_ms) < kEnergyResponseTimeoutMs) return;
    state.awaiting_response = false;
    state.retry_requested = true;
  }
  if (!state.retry_requested) return;
  if (state.last_attempt_ms != 0 &&
      (uint32_t)(now - state.last_attempt_ms) < kEnergyRetryBackoffMs) {
    return;
  }
  enqueue_energy_request(period, state, now);
}

std::vector<EnergyEntryData>& cache_for_period(const char* period) {
  const char* p = normalize_period(period);
  if (strcmp(p, "week") == 0) return g_week_entries;
  if (strcmp(p, "month") == 0) return g_month_entries;
  return g_day_entries;
}

String cached_unit_for_entry(const String& id) {
  if (!id.length()) return String();
  const std::vector<EnergyEntryData>* caches[] = {
      &g_day_entries, &g_week_entries, &g_month_entries};
  for (const auto* cache : caches) {
    for (const auto& entry : *cache) {
      if (entry.id.equalsIgnoreCase(id) && entry.unit.length()) {
        return entry.unit;
      }
    }
  }
  return String();
}

bool is_disabled_token(const String& value) {
  if (!value.length()) return false;
  String t = value;
  t.trim();
  if (!t.length()) return true;
  t.toLowerCase();
  return t == "-" || t == "none" || t == "null" || t == "no" || t == "off";
}

String format_energy_total(float value, bool is_cost) {
  if (!isfinite(value)) return String("--");
  return String(value, is_cost ? 2 : 3);
}

float apply_energy_sign(float value, int8_t sign) {
  if (sign < 0 && value > 0.0f) return -value;
  return value;
}

const char* icon_for_energy(const EnergyEntryData& entry) {
  if (entry.is_cost) return "currency-eur";
  String cat = entry.category;
  cat.toLowerCase();
  if (cat == "solar") return "solar-power";
  if (cat == "grid") return "transmission-tower";
  if (cat == "battery") return "battery-charging";
  if (cat == "gas") return "fire";
  if (cat == "water" || cat == "device_water") return "water";
  return "lightning-bolt";
}

void queue_energy_tile_update_for_entry(const EnergyEntryData& entry) {
  if (!entry.id.length()) return;

  String raw_value = format_energy_total(entry.total, entry.is_cost);
  tiles_cache_entity_payload(entry.id.c_str(), raw_value.c_str());

  if (powerManager.isInSleep()) return;
  if (!tiles_is_loaded(GridType::TAB0)) return;

  const TileGridConfig& grid = tileConfig.getActiveGrid();
  for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = grid.tiles[i];
    if (tile.type != TILE_ENERGY) continue;
    if (!tile.sensor_entity.equalsIgnoreCase(entry.id)) continue;

    String unit = tile.sensor_unit;
    if (is_disabled_token(unit)) {
      unit = "";
    } else if (!unit.length()) {
      unit = entry.unit.length() ? entry.unit : haBridgeConfig.findSensorUnit(entry.id);
    }
    queue_sensor_tile_update(GridType::TAB0,
                             i,
                             raw_value.c_str(),
                             unit.length() ? unit.c_str() : nullptr);
  }
}

void parse_energy_response(const char* payload) {
  const uint32_t parse_started_ms = millis();
  DynamicJsonDocument doc(32768);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[Energy] Response JSON invalid: %s\n", err.c_str());
    return;
  }

  const char* period_c = doc["period"] | "day";
  const char* period = normalize_period(period_c);
  String start = doc["start"] | "";
  JsonArray entries = doc["entries"].as<JsonArray>();
  if (entries.isNull()) {
    Serial.println("[Energy] Response without entries");
    return;
  }

  std::vector<EnergyEntryData> parsed;
  parsed.reserve(entries.size());

  for (JsonObject obj : entries) {
    const char* id_c = obj["id"] | "";
    if (!id_c || !*id_c) continue;

    EnergyEntryData entry;
    entry.id = id_c;
    entry.period = period;
    entry.start = start;
    entry.category = obj["category"] | "";
    entry.name = obj["name"] | "";
    entry.unit = obj["unit"] | "";
    entry.is_cost = obj["is_cost"] | false;
    entry.is_total = obj["is_total"] | false;
    int raw_sign = obj["sign"] | 1;
    entry.sign = raw_sign < 0 ? -1 : 1;

    if (!entry.name.length()) {
      entry.name = haBridgeConfig.findSensorName(entry.id);
    }
    if (!entry.unit.length()) {
      entry.unit = haBridgeConfig.findSensorUnit(entry.id);
      if (!entry.unit.length()) {
        // Some Energy responses omit the unit. Preserve a unit already known
        // to the RAM cache when this happens.
        entry.unit = cached_unit_for_entry(entry.id);
      }
    }

    JsonVariant total = obj["total"];
    entry.total = total.isNull() ? 0.0f : apply_energy_sign(total.as<float>(), entry.sign);
    JsonVariant cost = obj["cost"];
    if (!cost.isNull()) {
      entry.cost = cost.as<float>();
      entry.has_cost = true;
    }

    JsonArray values = obj["values"].as<JsonArray>();
    if (!values.isNull()) {
      for (JsonVariant v : values) {
        if (entry.value_count >= ENERGY_VALUES_MAX) break;
        const uint8_t idx = entry.value_count++;
        if (v.isNull()) {
          entry.value_valid[idx] = false;
          entry.values[idx] = 0.0f;
        } else {
          float f = apply_energy_sign(v.as<float>(), entry.sign);
          entry.value_valid[idx] = isfinite(f);
          entry.values[idx] = entry.value_valid[idx] ? f : 0.0f;
        }
      }
    }

    if (strcmp(period, "day") == 0) {
      queue_energy_tile_update_for_entry(entry);
    }
    parsed.push_back(entry);
  }

  cache_for_period(period) = parsed;
  queue_energy_popup_refresh(period);
  // Timing of one response: since its request was sent, and the parse itself.
  const uint32_t now = millis();
  const uint32_t requested_ms = request_state_for_period(period).last_attempt_ms;
  Serial.printf("[Energy] Response parsed: period=%s entries=%u after=%lu ms parse=%lu ms\n",
                period,
                static_cast<unsigned>(parsed.size()),
                static_cast<unsigned long>(requested_ms ? now - requested_ms : 0),
                static_cast<unsigned long>(now - parse_started_ms));
}

}  // namespace

void queue_energy_response(const char* payload, size_t len) {
  if (!payload || len == 0) return;
  const char* period = response_period(payload);
  PendingEnergyResponse& pending = pending_for_period(period);
  pending.payload = String(payload).substring(0, len);
  pending.valid = true;
  EnergyRequestState& request = request_state_for_period(period);
  request.awaiting_response = false;
  request.retry_requested = false;
}

void process_energy_response_queue() {
  // Weekly and monthly data usually follow a direct user action; process them
  // before the periodic daily refresh.
  PendingEnergyResponse* pending = nullptr;
  if (g_pending_week.valid) {
    pending = &g_pending_week;
  } else if (g_pending_month.valid) {
    pending = &g_pending_month;
  } else if (g_pending_day.valid) {
    pending = &g_pending_day;
  }
  if (!pending) return;

  // Take ownership of the buffer instead of copying a response of up to 32 KB.
  // The local String releases the memory immediately after parsing.
  String payload = std::move(pending->payload);
  pending->valid = false;
  parse_energy_response(payload.c_str());
  if (!powerManager.isInSleep()) {
    process_sensor_update_queue();
  }
}

bool energy_request_period(const char* period, bool force) {
  const char* p = normalize_period(period);
  EnergyRequestState& request = request_state_for_period(p);
  const uint32_t now = millis();

  if (request.awaiting_response) {
    if ((uint32_t)(now - request.last_attempt_ms) < kEnergyResponseTimeoutMs) {
      return true;
    }
    request.awaiting_response = false;
    request.retry_requested = true;
  }
  if (request.retry_requested) {
    if (request.last_attempt_ms != 0 &&
        (uint32_t)(now - request.last_attempt_ms) < kEnergyRetryBackoffMs) {
      return true;
    }
    return enqueue_energy_request(p, request, now);
  }
  if (!force && request.last_attempt_ms != 0 &&
      (uint32_t)(now - request.last_attempt_ms) < kEnergyRequestThrottleMs) {
    return true;
  }
  return enqueue_energy_request(p, request, now);
}

bool energy_request_day_for_tiles(bool force) {
  return energy_request_period("day", force);
}

void energy_service_periodic() {
  if (!networkManager.isMqttConnected()) return;
  const uint32_t now = millis();
  service_energy_retry("day", g_day_request, now);
  service_energy_retry("week", g_week_request, now);
  service_energy_retry("month", g_month_request, now);
  if (g_last_periodic_ms != 0 && (uint32_t)(now - g_last_periodic_ms) < 60UL * 1000UL) {
    return;
  }
  if (energy_request_day_for_tiles(false)) {
    g_last_periodic_ms = now;
  }
}

bool energy_find_entry(const String& id, const char* period, EnergyEntryData& out) {
  if (!id.length()) return false;
  const std::vector<EnergyEntryData>& entries = cache_for_period(period);
  for (const auto& entry : entries) {
    if (entry.id.equalsIgnoreCase(id)) {
      out = entry;
      return true;
    }
  }
  return false;
}

String energy_find_cached_unit(const String& id) {
  return cached_unit_for_entry(id);
}

void energy_append_cached_entity_ids(std::vector<String>& ids) {
  const std::vector<EnergyEntryData>* caches[] = {
      &g_day_entries, &g_week_entries, &g_month_entries};
  for (const auto* cache : caches) {
    for (const auto& entry : *cache) {
      if (!entry.id.length()) continue;
      bool exists = false;
      for (const auto& id : ids) {
        if (id.equalsIgnoreCase(entry.id)) {
          exists = true;
          break;
        }
      }
      if (!exists) ids.push_back(entry.id);
    }
  }
}
