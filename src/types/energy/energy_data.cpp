#include "src/types/energy/energy_data.h"

#include <ArduinoJson.h>
#include <cstring>
#include <math.h>
#include <time.h>
#include <utility>
#include <vector>

#include "src/core/power/power_manager.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/network/network_manager.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/types/energy/renderer.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/screensaver/screensaver_config.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"

namespace {

struct PendingEnergyResponse {
  String payload;
  bool valid = false;
};

// Responses for different periods must not overwrite each other, for example
// when the periodic day response arrives just after a manual week request.
// The Energy popup's periods: today by the hour, this week and this month by
// the day, this year by the month.
PendingEnergyResponse g_pending_day;
PendingEnergyResponse g_pending_week;
PendingEnergyResponse g_pending_month;
PendingEnergyResponse g_pending_year;
std::vector<EnergyEntryData> g_day_entries;
std::vector<EnergyEntryData> g_week_entries;
std::vector<EnergyEntryData> g_month_entries;
std::vector<EnergyEntryData> g_year_entries;

struct EnergyRequestState {
  uint32_t last_attempt_ms = 0;  // loop-task only
  volatile bool awaiting_response = false;
  volatile bool retry_requested = false;
  // Timeouts in a row (loop-task only); the response clears it.
  uint8_t timeouts = 0;
  // millis() of the last response for this period (loop-task only).
  uint32_t last_response_ms = 0;
};

EnergyRequestState g_day_request;
EnergyRequestState g_week_request;
EnergyRequestState g_month_request;
EnergyRequestState g_year_request;
// The tiles' hourly refresh (loop-task only): the hour whose answer reached
// them (tm_hour, -1 before the first), the newest bar of that answer, the hour
// last asked for, when an answer last reached them, and the last look at the
// clock.
int g_refreshed_hour = -1;
int g_refreshed_newest = -1;
int g_asked_hour = -1;
uint32_t g_day_applied_ms = 0;
uint32_t g_refresh_checked_ms = 0;

constexpr uint32_t kEnergyRequestThrottleMs = 10000UL;
constexpr uint32_t kEnergyRetryBackoffMs = 2000UL;
constexpr uint32_t kEnergyResponseTimeoutMs = 15000UL;
// Energy tiles show hours and the day's total, so once an hour is enough, right
// after the new hour's bar exists: the Bridge builds the running hour from the
// 5-minute statistics, and the Recorder writes an hour's first row at 5 minutes
// 10 seconds past it. Home Assistant's own dashboard refreshes at 20 past
// (frontend src/data/energy.ts, scheduleHourlyRefresh) but shows finished hours
// only; at 20 past, a tap found a new bar for 15 minutes of every hour (user
// 2026-11-04). Every request costs a statistics query in Home Assistant and a
// parse in the loop, and it must stay sparing. The Energy popup asks for itself
// when it opens.
constexpr int kEnergyRefreshMinute = 6;
// A busy Recorder writes the new bar later: an answer without it does not count
// and the refresh asks again. From the minute the dashboard refreshes an answer
// counts as it is, so sources without new statistics cost four requests an hour.
constexpr int kEnergyLastRetryMinute = 20;
// Without a clock (no time server reached): an hour after the last answer.
constexpr uint32_t kEnergyRefreshMs = 60UL * 60UL * 1000UL;
// The hour counts only once its answer reached the tiles. A request or an
// answer can get lost (QoS 0, a busy Recorder, no memory for the parse), so
// until then it asks again this often: Home Assistant's own statistics step.
constexpr uint32_t kEnergyUnansweredMs = 5UL * 60UL * 1000UL;
// The clock is set from 2024-01-01 on (the same check as the head bar's time).
constexpr time_t kClockSetEpoch = 1704067200;
// A Bridge without Energy dashboard never answers. After this many timeouts
// in a row the request waits for the next periodic slot or a popup instead of
// retrying every 15 seconds (each try logged a Home Assistant warning).
constexpr uint8_t kEnergyMaxTimeoutsInRow = 2;
// The popup asks again on every Day/Week/Month switch; within a minute the
// statistics cannot have changed, so the cached answer is shown instead.
constexpr uint32_t kEnergyFreshMs = 60UL * 1000UL;

const char* normalize_period(const char* period) {
  if (!period || !*period) return "day";
  if (strcmp(period, "week") == 0) return "week";
  if (strcmp(period, "month") == 0) return "month";
  if (strcmp(period, "year") == 0) return "year";
  return "day";
}

PendingEnergyResponse& pending_for_period(const char* period) {
  const char* p = normalize_period(period);
  if (strcmp(p, "week") == 0) return g_pending_week;
  if (strcmp(p, "month") == 0) return g_pending_month;
  if (strcmp(p, "year") == 0) return g_pending_year;
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
  if (len == 4 && strncmp(begin, "year", len) == 0) return "year";
  return "day";
}

EnergyRequestState& request_state_for_period(const char* period) {
  const char* p = normalize_period(period);
  if (strcmp(p, "week") == 0) return g_week_request;
  if (strcmp(p, "month") == 0) return g_month_request;
  if (strcmp(p, "year") == 0) return g_year_request;
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
    if (state.timeouts < 255) ++state.timeouts;
    state.retry_requested = state.timeouts < kEnergyMaxTimeoutsInRow;
    if (!state.retry_requested) {
      Serial.printf("[Energy] No response to %s request (%u in a row); waiting for the next refresh\n",
                    period, static_cast<unsigned>(state.timeouts));
    }
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
  if (strcmp(p, "year") == 0) return g_year_entries;
  return g_day_entries;
}

String cached_unit_for_entry(const String& id) {
  if (!id.length()) return String();
  const std::vector<EnergyEntryData>* caches[] = {
      &g_day_entries, &g_week_entries, &g_month_entries, &g_year_entries};
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

// The local time, when the clock is set.
bool read_local_clock(struct tm& local) {
  const time_t wall = time(nullptr);
  return wall >= kClockSetEpoch && localtime_r(&wall, &local);
}

// A day answer reached the tiles: this hour's refresh is done once the answer
// brought a new bar (compared with the last one, so it needs no agreement on
// the hour with Home Assistant's clock), or when none will come any more. An
// answer without any hour gets here only when the dashboard has no sources.
void note_day_answer_applied(int newest_hour) {
  g_day_applied_ms = millis();
  struct tm local {};
  if (!read_local_clock(local) || local.tm_min < kEnergyRefreshMinute) return;
  if (newest_hour >= 0 && newest_hour == g_refreshed_newest && local.tm_min < kEnergyLastRetryMinute) return;
  g_refreshed_hour = local.tm_hour;
  g_refreshed_newest = newest_hour;
}

// The newest hour any source of an answer has a value for, -1 without any.
int answer_newest_hour(JsonArray entries) {
  int newest = -1;
  for (JsonObject obj : entries) {
    int hour = 0;
    for (JsonVariant value : obj["values"].as<JsonArray>()) {
      if (!value.isNull() && hour > newest) newest = hour;
      ++hour;
    }
  }
  return newest;
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
  // Sources without a single hour: the Recorder failed, or the first minutes
  // of a day hold no statistics yet. Such an answer must not blank the tiles'
  // bars and totals; it does not count, so the refresh asks again.
  const int newest_hour = answer_newest_hour(entries);
  if (strcmp(period, "day") == 0 && entries.size() > 0 && newest_hour < 0) {
    Serial.println("[Energy] Day response without hours; the tiles keep their values");
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
      energy_tile_bars_update(entry);
    }
    parsed.push_back(entry);
  }

  // A source without an icon so far gets its category's from this response:
  // tiles that drew none redraw their icons.
  bool new_icon = false;
  for (const auto& entry : parsed) {
    if (!energy_source_icon(entry.id).length()) {
      new_icon = true;
      break;
    }
  }
  cache_for_period(period) = parsed;
  if (new_icon) tiles_request_icon_refresh();
  if (strcmp(period, "day") == 0) note_day_answer_applied(newest_hour);
  queue_energy_popup_refresh(period);
  // Timing of one response: since its request was sent, and the parse itself.
  const uint32_t now = millis();
  const uint32_t requested_ms = request_state_for_period(period).last_attempt_ms;
  Serial.printf("[Energy] Response parsed: period=%s entries=%u newest=%d after=%lu ms parse=%lu ms\n",
                period,
                static_cast<unsigned>(parsed.size()),
                newest_hour,
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
  request.timeouts = 0;
  request.last_response_ms = millis();
  if (request.last_response_ms == 0) request.last_response_ms = 1;
}

void process_energy_response_queue() {
  // Week, month and year data usually follow a direct user action; process
  // them before the periodic daily refresh.
  PendingEnergyResponse* pending = nullptr;
  if (g_pending_week.valid) {
    pending = &g_pending_week;
  } else if (g_pending_month.valid) {
    pending = &g_pending_month;
  } else if (g_pending_year.valid) {
    pending = &g_pending_year;
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
  if (force && !request.retry_requested && request.last_response_ms != 0 &&
      (uint32_t)(now - request.last_response_ms) < kEnergyFreshMs) {
    return true;
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

bool energy_request_waiting() {
  return g_day_request.awaiting_response || g_week_request.awaiting_response ||
         g_month_request.awaiting_response || g_year_request.awaiting_response;
}

bool energy_period_fresh(const char* period) {
  const EnergyRequestState& request = request_state_for_period(period);
  return request.last_response_ms != 0 && (uint32_t)(millis() - request.last_response_ms) < kEnergyFreshMs;
}

void energy_service_periodic() {
  if (!networkManager.isMqttConnected()) return;
  const uint32_t now = millis();
  service_energy_retry("day", g_day_request, now);
  service_energy_retry("week", g_week_request, now);
  service_energy_retry("month", g_month_request, now);
  service_energy_retry("year", g_year_request, now);
  // Energy tiles follow the day also without an energy section: their sources
  // come from the Energy dashboard (picker list "energy"). Gated on the section
  // alone, such a tile only got new values when it was built or a popup asked
  // (user 2026-11-04: they only moved after a tap). With neither there is
  // nothing to ask for.
  if (!haBridgeConfig.hasEnergyEntries() && !energy_tiles_alive()) return;
  // This runs in every loop; the clock once a second is plenty.
  if (g_refresh_checked_ms != 0 && (uint32_t)(now - g_refresh_checked_ms) < 1000UL) return;
  g_refresh_checked_ms = now;
  struct tm local {};
  const bool clock_set = read_local_clock(local);
  const bool due = clock_set
                       ? local.tm_min >= kEnergyRefreshMinute && local.tm_hour != g_refreshed_hour
                       : g_day_applied_ms == 0 || (uint32_t)(now - g_day_applied_ms) >= kEnergyRefreshMs;
  if (!due) return;
  // Nothing reached the tiles yet: ask, and again until an answer does.
  const int asked = clock_set ? local.tm_hour : -2;
  if (asked == g_asked_hour && (uint32_t)(now - g_day_request.last_attempt_ms) < kEnergyUnansweredMs) return;
  if (energy_request_day_for_tiles(false)) g_asked_hour = asked;
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

namespace {

// The source in any received Energy response, nullptr without one.
const EnergyEntryData* cached_entry(const String& id) {
  if (!id.length()) return nullptr;
  const std::vector<EnergyEntryData>* caches[] = {
      &g_day_entries, &g_week_entries, &g_month_entries, &g_year_entries};
  for (const auto* cache : caches) {
    for (const auto& entry : *cache) {
      if (entry.id.equalsIgnoreCase(id)) return &entry;
    }
  }
  return nullptr;
}

void append_unique(std::vector<String>& ids, const char* id) {
  if (!id || !*id) return;
  for (const auto& known : ids) {
    if (known.equalsIgnoreCase(id)) return;
  }
  ids.push_back(String(id));
}

}  // namespace

String energy_source_icon(const String& id) {
  String icon = haBridgeConfig.findEntityIcon(id);
  if (icon.length()) return icon;
  const EnergyEntryData* entry = cached_entry(id);
  return entry ? String(energySourceIcon(entry->category, entry->id, entry->unit)) : String();
}

String energy_source_name(const String& id) {
  String name = haBridgeConfig.findSensorName(id);
  if (name.length()) return name;
  const EnergyEntryData* entry = cached_entry(id);
  return entry ? entry->name : String();
}

void energy_append_tile_entity_ids(std::vector<String>& ids) {
  FolderEntitySlotView slots[TILES_PER_GRID];
  for (const FolderEntry& folder : tileConfig.getFolders()) {
    if (!tileConfig.getFolderEntitiesCached(folder.id, slots, TILES_PER_GRID)) continue;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      if (slots[i].type == TILE_ENERGY) append_unique(ids, slots[i].entity);
    }
  }
  const TileGridConfig& screensaver = screensaverConfig.tileGrid();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (screensaver.tiles[i].type == TILE_ENERGY) append_unique(ids, screensaver.tiles[i].sensor_entity.c_str());
  }
}

void energy_append_cached_entity_ids(std::vector<String>& ids) {
  const std::vector<EnergyEntryData>* caches[] = {
      &g_day_entries, &g_week_entries, &g_month_entries, &g_year_entries};
  for (const auto* cache : caches) {
    for (const auto& entry : *cache) append_unique(ids, entry.id.c_str());
  }
}
