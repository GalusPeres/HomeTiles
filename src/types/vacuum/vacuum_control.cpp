#include "src/types/vacuum/vacuum_control.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include <cstring>
#include <new>

#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/network_manager.h"

namespace vacuum_control {
namespace {

using vacuum_detail::Detail;

// A few vacuums at a time; a vacuum beyond them is parsed again from the
// stored payload when it is asked for (least recently used slot).
constexpr size_t kSlots = 4;
struct Slot {
  char entity[vacuum_detail::kEntityBytes + 1] = {};
  uint32_t used = 0;
  Detail detail;
};
Slot* g_slots = nullptr;
uint32_t g_clock = 0;
const Detail kNone{};

bool ensure_slots() {
  if (g_slots) return true;
  void* memory = heap_caps_calloc(kSlots, sizeof(Slot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = calloc(kSlots, sizeof(Slot));
  if (!memory) return false;
  g_slots = static_cast<Slot*>(memory);
  for (size_t i = 0; i < kSlots; ++i) new (&g_slots[i]) Slot();
  return true;
}

Slot* find(const String& entity) {
  if (!g_slots) return nullptr;
  for (size_t i = 0; i < kSlots; ++i) {
    if (g_slots[i].entity[0] && entity.equalsIgnoreCase(g_slots[i].entity)) return &g_slots[i];
  }
  return nullptr;
}

Slot* claim(const String& entity) {
  if (!ensure_slots() || entity.length() > vacuum_detail::kEntityBytes) return nullptr;
  Slot* slot = find(entity);
  if (!slot) {
    slot = &g_slots[0];
    for (size_t i = 0; i < kSlots; ++i) {
      if (!g_slots[i].entity[0]) {
        slot = &g_slots[i];
        break;
      }
      if (g_slots[i].used < slot->used) slot = &g_slots[i];
    }
    slot->detail = Detail{};
    strlcpy(slot->entity, entity.c_str(), sizeof(slot->entity));
  }
  slot->used = ++g_clock;
  return slot;
}

bool parse_into(Slot& slot, const char* payload, size_t length) {
  DynamicJsonDocument doc(6144);
  return vacuum_detail::parse(payload, length, doc, slot.detail);
}

void publish(const String& entity, JsonDocument& doc) {
  if (!entity.length() || !networkManager.isMqttConnected()) return;
  doc["entity_id"] = entity;
  String body;
  serializeJson(doc, body);
  const String topic = mqttTopics.deviceBase() + "/cmnd/vacuum";
  networkManager.mqttEnqueuePublish(topic.c_str(), body.c_str(), false);
}

}  // namespace

const Detail& detail(const String& entity) {
  if (!entity.startsWith("vacuum.")) return kNone;
  if (Slot* slot = find(entity)) {
    slot->used = ++g_clock;
    return slot->detail;
  }
  const String payload = haBridgeConfig.findDetailValue(entity);
  if (!payload.length()) return kNone;
  Slot* slot = claim(entity);
  if (!slot) return kNone;
  if (!parse_into(*slot, payload.c_str(), payload.length())) slot->detail = Detail{};
  return slot->detail;
}

bool accept_detail(const String& entity, const char* payload, size_t length) {
  Slot* slot = claim(entity);
  if (!slot || !parse_into(*slot, payload, length)) {
    if (slot) slot->entity[0] = '\0';
    static uint32_t last_log_ms = 0;
    if (!last_log_ms || millis() - last_log_ms > 5000) {
      last_log_ms = millis();
      Serial.printf("[Vacuum] Malformed detail state ignored: %s (%u bytes)\n", entity.c_str(),
                    static_cast<unsigned>(length));
    }
    return false;
  }
  return true;
}

void send(const String& entity, const char* action) {
  if (!action || !*action) return;
  StaticJsonDocument<192> doc;
  doc["action"] = action;
  publish(entity, doc);
}

void set_fan_speed(const String& entity, const char* speed) {
  if (!speed || !*speed) return;
  StaticJsonDocument<256> doc;
  doc["action"] = "set_fan_speed";
  doc["fan_speed"] = speed;
  publish(entity, doc);
}

void clean_areas(const String& entity, const char* const* area_ids, size_t count) {
  if (!area_ids || !count || count > vacuum_detail::kMaxAreas) return;
  DynamicJsonDocument doc(1536);
  doc["action"] = "clean_area";
  JsonArray areas = doc.createNestedArray("areas");
  for (size_t i = 0; i < count; ++i) {
    if (area_ids[i] && *area_ids[i]) areas.add(area_ids[i]);
  }
  if (!areas.size()) return;
  publish(entity, doc);
}

}  // namespace vacuum_control
