#include "src/types/value/value_control.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <lvgl_private.h>
#include <esp_system.h>
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/network_manager.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/shared/ui_control_style.h"
#include "src/types/value/value_editor_model.h"
#include "src/types/value/value_colors.h"
#include "src/types/climate/layout.h"
#include "src/fonts/ui_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/popups/sensor/sensor_popup.h"

namespace {
const char* label(unsigned index) {
  return i18n::locale(configManager.getConfig().language).editable_labels[index];
}
bool finite_json(JsonVariantConst item, double& output) {
  if (item.isNull() || item.is<bool>() || !item.is<double>()) return false;
  // ArduinoJson can store a short decimal as float. Recover its canonical
  // decimal before step-grid arithmetic, instead of promoting float noise.
  char canonical[64]; serializeJson(item, canonical, sizeof(canonical));
  output = strtod(canonical, nullptr);
  return std::isfinite(output);
}
String request_id() {
  static uint32_t sequence = 0;
  char id[40];
  snprintf(id, sizeof(id), "%08lx-%08lx-%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(millis()), static_cast<unsigned long>(++sequence));
  return id;
}
String pending_entities[8];
uint8_t pending_count = 0;
uint32_t value_generation = 1;
}

bool editable_entity_matches(TileType type, const String& entity) {
  if (!entity.length()) return true;
  const int dot = entity.indexOf('.');
  if (dot < 1 || dot == static_cast<int>(entity.length()) - 1) return false;
  for (size_t i = dot + 1; i < entity.length(); ++i) {
    const char c = entity[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  const String domain = entity.substring(0, dot);
  if (type == TILE_NUMBER) return domain == "number" || domain == "input_number";
  if (type == TILE_SELECT) return domain == "select" || domain == "input_select";
  return type == TILE_DATETIME && (domain == "date" || domain == "time" ||
         domain == "datetime" || domain == "input_datetime");
}

EditableValue parse_editable_value(const String& payload) {
  EditableValue value;
  if (!payload.length() || payload.length() > EDITABLE_PAYLOAD_MAX) return value;
  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, payload.c_str(), payload.length()) || !doc.is<JsonObject>() || (doc["version"] | 0) != 1) return value;
  value.kind = doc["kind"] | "";
  if (value.kind != "number" && value.kind != "select" && value.kind != "date" &&
      value.kind != "time" && value.kind != "datetime") return value;
  if (!doc.containsKey("state") || (!doc["state"].isNull() && !doc["state"].is<const char*>())) return value;
  value.has_state = !doc["state"].isNull();
  value.state = doc["state"] | "";
  if (doc["last_changed"].is<uint64_t>() && !doc["last_changed"].is<bool>()) value.last_changed = doc["last_changed"].as<uint64_t>();
  if (value.state.length() > 255) return EditableValue{};
  value.available = (doc["available"] | false) && value.has_state && value.state != "unavailable";
  value.writable = value.available && (doc["writable"] | false);
  value.session = doc["session"] | "";
  value.revision = doc["revision"] | "";
  if (value.session.length() != 32 || value.revision.length() != 16) return EditableValue{};
  if (value.kind == "number") {
    const bool valid_range = finite_json(doc["min"], value.minimum) &&
      finite_json(doc["max"], value.maximum) && finite_json(doc["step"], value.step) &&
      value.minimum < value.maximum && value.step > 0 &&
      std::isfinite(value.maximum - value.minimum);
    value.writable = value.writable && valid_range;
    value.mode = doc["mode"] | "auto";
    value.unit = doc["unit"] | "";
    if (value.unit.length() > 128) value.unit = "";
  } else if (value.kind == "select") {
    JsonArrayConst options = doc["options"].as<JsonArrayConst>();
    bool complete = (doc["options_complete"] | false) && !options.isNull() && options.size() > 0 && options.size() <= 64;
    if (complete) {
      for (JsonVariantConst item : options) {
        if (!item.is<const char*>()) { complete = false; break; }
        String option = item.as<const char*>();
        if (!option.length() || option.length() > 255 || option.indexOf('\n') >= 0 || option.indexOf('\r') >= 0 ||
            std::find(value.options.begin(), value.options.end(), option) != value.options.end()) { complete = false; break; }
        value.options.push_back(option);
      }
    }
    if (!complete) value.options.clear();
    value.writable = value.writable && complete;
  }
  value.valid = true;
  return value;
}

String editable_display_value(const EditableValue& value) {
  if (!value.valid || !value.has_state) return "--";
  const char* language = configManager.getConfig().language;
  if (!value.available || value.state == "unknown")
    return i18n::binary_sensor_state_label(language, value.available ? "unknown" : "unavailable", "");
  if (value.kind == "number") {
    char* end = nullptr;
    const double number = strtod(value.state.c_str(), &end);
    if (!end || *end || end == value.state.c_str() || !std::isfinite(number))
      return i18n::binary_sensor_state_label(language, "unknown", "");
    String text = value.state;
    if (i18n::locale(language).decimal_separator[0] == ',') text.replace('.', ',');
    if (value.unit.length()) text += " " + value.unit;
    return text;
  }
  return value.state;
}

void append_editable_translations(String& html, const char* name) {
  StaticJsonDocument<512> doc;
  const char* language = configManager.getConfig().language;
  doc["unknown"] = i18n::binary_sensor_state_label(language, "unknown", "");
  doc["unavailable"] = i18n::binary_sensor_state_label(language, "unavailable", "");
  String json; serializeJson(doc, json); json.replace("<", "\\u003c");
  html += "<script>const "; html += name; html += "=Object.freeze("; html += json; html += ");</script>";
}

void refresh_editable_tile(GridType grid, uint8_t index) {
  const Tile* tile = tile_renderer_get_tile_config(grid, index);
  if (!tile || !tileTypeIsEditableValue(tile->type)) return;
  SensorTileWidgets* widgets = tile_renderer_get_sensor_widgets(grid);
  if (!widgets || !widgets[index].value_label) return;
  const EditableValue value = parse_editable_value(haBridgeConfig.findEditableValue(tile->sensor_entity));
  lv_label_set_long_mode(widgets[index].value_label, LV_LABEL_LONG_DOT);
  lv_label_set_text(widgets[index].value_label, editable_display_value(value).c_str());
}

void queue_editable_value(const String& entity, const char* payload) {
  if (!payload || strnlen(payload, EDITABLE_PAYLOAD_MAX + 1) > EDITABLE_PAYLOAD_MAX) return;
  const EditableValue value = parse_editable_value(payload);
  if (!value.valid) return;
  haBridgeConfig.updateEditableValue(entity, payload);
  ++value_generation;
  for (uint8_t i = 0; i < pending_count; ++i) if (pending_entities[i] == entity) return;
  if (pending_count == 8) {
    // The cache is authoritative; scanning all live tiles also recovers overflow.
    pending_entities[0] = "*";
    return;
  }
  pending_entities[pending_count++] = entity;
}

uint32_t editable_value_generation() { return value_generation; }
void editable_configuration_changed() { ++value_generation; pending_entities[0] = "*"; pending_count = 1; }

void process_editable_updates(uint8_t budget) {
  uint8_t processed = 0;
  while (pending_count && (!budget || processed++ < budget)) {
    const String entity = pending_entities[--pending_count];
    pending_entities[pending_count] = "";
    for (uint8_t grid = 0; grid < 4; ++grid) {
      for (uint8_t index = 0; index < TILES_PER_GRID; ++index) {
        const Tile* tile = tile_renderer_get_tile_config(static_cast<GridType>(grid), index);
        if (tile && tileTypeIsEditableValue(tile->type) && (entity == "*" || tile->sensor_entity == entity))
          refresh_editable_tile(static_cast<GridType>(grid), index);
      }
    }
  }
}

String editable_request_history(const String& entity, uint16_t hours) {
  if (!networkManager.isMqttConnected()) return "";
  const char* topic = networkManager.getHistoryRequestTopic();
  if (!topic || !*topic) return "";
  StaticJsonDocument<512> doc;
  const String id = request_id();
  doc["entity_id"] = entity; doc["kind"] = "editable"; doc["version"] = 1;
  doc["hours"] = hours; doc["max_transitions"] = 96; doc["request_id"] = id;
  String payload; serializeJson(doc, payload);
  return networkManager.mqttEnqueuePublish(topic, payload.c_str(), false) ? id : String();
}

int editable_control_height(const String& kind) {
  const int date_height = popup_layout::scale(72) + lv_font_get_line_height(popup_layout::font20());
  const int clock_height = lv_font_get_line_height(popup_layout::font40()) +
                           lv_font_get_line_height(popup_layout::font20()) + popup_layout::scale(24);
  const int value_height = std::max<int>(popup_layout::scale(80),
      lv_font_get_line_height(popup_layout::headerTitleFont()) + popup_layout::scale(54));
  return kind == "datetime" ? date_height + clock_height + popup_layout::scale(8) :
         kind == "date" ? date_height : kind == "time" ? clock_height : value_height;
}

struct EditableControl {
  struct CalendarField {
    lv_obj_t *box = nullptr, *spinbox = nullptr, *up = nullptr, *down = nullptr, *roller = nullptr;
    bool unknown_options = false;
  } fields[6];
  lv_obj_t *row = nullptr, *card = nullptr, *slider = nullptr, *field = nullptr,
           *number_box = nullptr, *number_roller = nullptr, *up = nullptr, *down = nullptr, *dropdown = nullptr,
           *apply = nullptr, *status = nullptr, *pressed = nullptr, *clock_box = nullptr, *separators[2] = {};
  EditableValue value;
  editable_colors::Palette colors{};
  value_editor::Calendar calendar;
  double draft = 0;
  uint8_t option_offset = 0;
  bool number_roller_enabled = false, number_options_placeholder = false;
  String entity, payload, command_id, command_value;
  uint32_t command_ms = 0, generation = 0, edit_ms = 0;
  bool active = false, syncing = false, editing = false, dragging = false, online = false,
       draft_valid = false, repeated = false, submit_scheduled = false;
  lv_display_t* dropdown_display = nullptr;
  uint32_t dropdown_open_ms = 0, dropdown_frame_ms = 0, dropdown_frames = 0,
           dropdown_max_ms = 0;
  uint64_t dropdown_total_ms = 0;
};
namespace {
EditableControl* active_control = nullptr;
void dropdown_render_event(lv_event_t* event) {
  auto* c = static_cast<EditableControl*>(lv_event_get_user_data(event));
  if (!c || !c->dropdown_display || !lv_dropdown_is_open(c->dropdown)) return;
  const auto code = lv_event_get_code(event);
  if (code == LV_EVENT_REFR_START) c->dropdown_frame_ms = millis();
  else if (code == LV_EVENT_REFR_READY) {
    const uint32_t elapsed = millis() - c->dropdown_frame_ms;
    c->dropdown_total_ms += elapsed;
    c->dropdown_max_ms = std::max(c->dropdown_max_ms, elapsed);
    if (++c->dropdown_frames == 1) {
      Serial.printf("[ValueDropdown] Open: options=%u, first_frame=%lums\n",
                    static_cast<unsigned>(c->value.options.size()),
                    static_cast<unsigned long>(millis() - c->dropdown_open_ms));
    }
  }
}
void finish_dropdown_timing(EditableControl* c) {
  if (!c || !c->dropdown_display) return;
  lv_display_remove_event_cb_with_user_data(c->dropdown_display, dropdown_render_event, c);
  c->dropdown_display = nullptr;
  if (c->dropdown_frames) {
    Serial.printf("[ValueDropdown] Render: frames=%lu, avg=%lums, max=%lums\n",
                  static_cast<unsigned long>(c->dropdown_frames),
                  static_cast<unsigned long>(c->dropdown_total_ms / c->dropdown_frames),
                  static_cast<unsigned long>(c->dropdown_max_ms));
  }
}
void dropdown_cover_check(lv_event_t* event) {
  auto* list = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  const lv_area_t* refresh_area = lv_event_get_cover_area(event);
  if (!list || !refresh_area ||
      lv_obj_get_style_bg_opa(list, LV_PART_MAIN) != LV_OPA_COVER ||
      lv_obj_get_style_opa(list, LV_PART_MAIN) != LV_OPA_COVER ||
      lv_obj_get_style_bg_grad_dir(list, LV_PART_MAIN) != LV_GRAD_DIR_NONE ||
      lv_obj_get_style_bg_grad(list, LV_PART_MAIN) != nullptr) return;
  // LVGL marks every corner-clipped object as MASKED, even for a refresh strip
  // fully inside its opaque background. Report that interior accurately so
  // a scrolled list does not redraw the covered history graph on every frame.
  // Corner strips retain LVGL's masked result and the existing rounded clip.
  lv_area_t area;
  lv_obj_get_coords(list, &area);
  const int32_t radius = std::min<int32_t>(lv_obj_get_style_radius(list, LV_PART_MAIN),
                                          std::min(lv_area_get_width(&area), lv_area_get_height(&area)) / 2);
  const bool contained = refresh_area->x1 >= area.x1 && refresh_area->x2 <= area.x2 &&
                         refresh_area->y1 >= area.y1 && refresh_area->y2 <= area.y2;
  const bool inside_straight_edges =
      (refresh_area->y1 > area.y1 + radius && refresh_area->y2 < area.y2 - radius) ||
      (refresh_area->x1 > area.x1 + radius && refresh_area->x2 < area.x2 - radius);
  if (contained && inside_straight_edges) {
    // The public setter only accepts more pessimistic results. This uniform
    // opaque list owns the cover result for the proven interior rectangle.
    auto* info = static_cast<lv_cover_check_info_t*>(lv_event_get_param(event));
    info->res = LV_COVER_RES_COVER;
  }
}
void visible(lv_obj_t* obj, bool show) {
  if (!obj) return;
  if (show) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
void finish_editing(EditableControl* c) {
  c->editing = false; c->pressed = nullptr; c->repeated = false; c->generation = 0; c->submit_scheduled = false;
}
void status_text(EditableControl* c, unsigned index) {
  lv_label_set_text(c->status, label(index)); visible(c->status, true);
}
void submit(EditableControl* c, const String& text) {
  if (!c || !c->active || c->syncing || !c->value.writable || !networkManager.isMqttConnected()) return;
  const time_t now = time(nullptr);
  if (now < 1700000000) { status_text(c, 9); return; }
  StaticJsonDocument<1024> doc;
  doc["entity_id"] = c->entity; doc["session"] = c->value.session; doc["revision"] = c->value.revision;
  if (c->value.kind == "number") {
    String normalized = text; normalized.replace(',', '.');
    char* end = nullptr; const double number = strtod(normalized.c_str(), &end);
    const double steps = (number - c->value.minimum) / c->value.step;
    if (!end || *end || end == normalized.c_str() || !std::isfinite(number) ||
        number < c->value.minimum || number > c->value.maximum ||
        !std::isfinite(steps) || fabs(steps - round(steps)) > 0.000001) { status_text(c, 7); return; }
    doc["value"] = number;
  } else doc["value"] = text;
  c->command_id = request_id(); doc["id"] = c->command_id;
  doc["deadline"] = static_cast<uint64_t>(now + 10);
  String payload; serializeJson(doc, payload);
  String topic = mqttTopics.deviceBase() + "/cmnd/value";
  if (networkManager.mqttEnqueuePublish(topic.c_str(), payload.c_str(), false)) {
    c->command_value = text;
    c->command_ms = millis(); status_text(c, 8); finish_editing(c);
  } else { c->command_id = ""; status_text(c, 9); }
}
String number_text(double number) {
  char text[48]; snprintf(text, sizeof(text), "%.15g", number); return text;
}
bool command_value_confirmed(const EditableControl* c, const EditableValue& next) {
  if (!next.available || !next.has_state) return false;
  if (next.kind == "number") {
    char* end = nullptr;
    const double reported = strtod(next.state.c_str(), &end);
    const double requested = strtod(c->command_value.c_str(), nullptr);
    return end && end != next.state.c_str() && !*end && std::isfinite(reported) &&
           fabs(reported - requested) <= std::max(1e-9, next.step * 1e-6);
  }
  if (next.kind == "select") return next.state == c->command_value;
  value_editor::Calendar reported, requested;
  if (!value_editor::parseCalendar(next.state.c_str(), next.kind != "time", next.kind != "date", reported) ||
      !value_editor::parseCalendar(c->command_value.c_str(), next.kind != "time", next.kind != "date", requested)) return false;
  for (int i = 0; i < 6; ++i) if (reported.values[i] != requested.values[i]) return false;
  return true;
}
bool number_uses_roller(const EditableValue& value) {
  // Keep lists bounded; large ranges retain the slider or exact step buttons.
  const bool temperature = value.unit == "°C" || value.unit == "°F" || value.unit == "K";
  const double steps = (value.maximum - value.minimum) / value.step;
  return value.kind == "number" && value.mode != "slider" && value.mode == "box" && !temperature &&
         value.step > 0 && value.maximum > value.minimum && std::isfinite(steps) && steps <= 200;
}
void render_number(EditableControl* c) {
  EditableValue shown = c->value;
  if (c->draft_valid) { shown.state = number_text(c->draft); shown.has_state = true; }
  lv_label_set_text(c->field, c->number_roller_enabled ? c->value.unit.c_str() : editable_display_value(shown).c_str());
  if (c->number_roller_enabled && c->draft_valid && c->pressed != c->number_roller) {
    const double position = (c->draft - c->value.minimum) / c->value.step;
    if (std::isfinite(position) && fabs(position - round(position)) < 1e-6)
      lv_roller_set_selected(c->number_roller, static_cast<int>(round(position)) + (c->number_options_placeholder ? 1 : 0), LV_ANIM_OFF);
  }
  if (!c->dragging && c->draft_valid && c->value.writable) {
    const double ratio = (c->draft - c->value.minimum) / (c->value.maximum - c->value.minimum);
    lv_slider_set_value(c->slider, static_cast<int>(std::max(0.0, std::min(1.0, ratio)) * 10000), LV_ANIM_OFF);
  }
}
void render_number_options(EditableControl* c) {
  const double position = c->draft_valid ? (c->draft - c->value.minimum) / c->value.step : -1;
  const int count = static_cast<int>(floor((c->value.maximum - c->value.minimum) / c->value.step + 1e-9)) + 1;
  c->number_options_placeholder = !c->draft_valid || position < 0 || position > count - 1 || fabs(position - round(position)) > 1e-6;
  String options = c->number_options_placeholder ? editable_display_value(c->value) + "\n" : String();
  for (int i = 0; i < count; ++i) {
    if (i) options += "\n";
    String text = number_text(c->value.minimum + i * c->value.step);
    if (i18n::locale(configManager.getConfig().language).decimal_separator[0] == ',') text.replace('.', ',');
    options += text;
  }
  lv_roller_set_options(c->number_roller, options.c_str(), LV_ROLLER_MODE_NORMAL);
  lv_roller_set_selected(c->number_roller, c->number_options_placeholder ? 0 : static_cast<int>(round(position)), LV_ANIM_OFF);
}
void render_calendar(EditableControl* c, lv_anim_enable_t animation = LV_ANIM_OFF, lv_obj_t* settled_roller = nullptr) {
  const bool syncing = c->syncing; c->syncing = true;
  for (int i = 0; i < 6; ++i) {
    auto& field = c->fields[i];
    if (field.roller) {
      const bool unknown = !c->draft_valid;
      const bool rebuild = field.unknown_options != unknown || lv_roller_get_option_count(field.roller) <= 1;
      if (rebuild) {
        String options = unknown ? "--\n" : "";
        for (int value = 0; value <= (i == 3 ? 23 : 59); ++value) {
          char text[4]; snprintf(text, sizeof(text), "%02d", value);
          if (value) options += "\n"; options += text;
        }
        lv_roller_set_options(field.roller, options.c_str(), unknown ? LV_ROLLER_MODE_NORMAL : LV_ROLLER_MODE_INFINITE);
        field.unknown_options = unknown;
      }
      // Native release handling already started the snap animation. Preserve it
      // while synchronizing the other fields and the outgoing value.
      if (rebuild || field.roller != settled_roller)
        lv_roller_set_selected(field.roller, unknown ? 0 : c->calendar.values[i], rebuild ? LV_ANIM_OFF : animation);
    } else {
      lv_spinbox_set_value(field.spinbox, c->calendar.values[i]);
      if (!c->draft_valid) lv_textarea_set_text(field.spinbox, "--");
    }
  }
  c->syncing = syncing;
}
void submit_draft(EditableControl* c) {
  if (!c->draft_valid) return;
  if (c->value.kind == "number") { submit(c, number_text(c->draft)); return; }
  const int* v = c->calendar.values;
  char text[32];
  if (c->value.kind == "time") snprintf(text, sizeof(text), "%02d:%02d:%02d", v[3], v[4], v[5]);
  else if (c->value.kind == "date") snprintf(text, sizeof(text), "%04d-%02d-%02d", v[0], v[1], v[2]);
  else snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d", v[0], v[1], v[2], v[3], v[4], v[5]);
  submit(c, text);
}
void schedule_draft(EditableControl* c) {
  if (!c->draft_valid) return;
  c->editing = true; c->submit_scheduled = true; c->edit_ms = millis();
}

void step_draft(EditableControl* c, lv_obj_t* target) {
  c->command_id = ""; c->editing = true; visible(c->status, false);
  if (target == c->up || target == c->down) {
    c->draft = c->draft_valid ? value_editor::stepped(c->draft, c->value.minimum, c->value.maximum,
                                                   c->value.step, target == c->up ? 1 : -1) : c->value.minimum;
    c->draft_valid = true; render_number(c);
  } else {
    for (int i = 0; i < 6; ++i) {
      if (target == c->fields[i].up || target == c->fields[i].down) {
        value_editor::stepCalendar(c->calendar, i, target == c->fields[i].up ? 1 : -1);
        c->draft_valid = true; render_calendar(c, LV_ANIM_ON); break;
      }
    }
    lv_obj_remove_state(c->apply, LV_STATE_DISABLED);
  }
}
void style_open_options(EditableControl* c) {
  finish_dropdown_timing(c);
  c->dropdown_open_ms = millis();
  c->dropdown_frames = c->dropdown_max_ms = 0;
  c->dropdown_total_ms = 0;
  c->dropdown_display = lv_obj_get_display(c->dropdown);
  // Aggregate only while this list is open; no per-frame logs or history.
  lv_display_add_event_cb(c->dropdown_display, dropdown_render_event, LV_EVENT_REFR_START, c);
  lv_display_add_event_cb(c->dropdown_display, dropdown_render_event, LV_EVENT_REFR_READY, c);
  lv_obj_t* list = lv_dropdown_get_list(c->dropdown);
  // LVGL reapplies its theme when opening a list, just as in Settings.
  ui_control_style::valueDropdownList(list);
  editable_colors::dropdownList(list, c->colors);
  // The Settings list already owns font, spacing and positioning. Only bound
  // its height to the popup body; do not synchronously relayout on opening.
  const int available = popup_layout::kNavY - 2 * popup_layout::kCardPad -
                        lv_obj_get_y(c->row) - editable_control_height("select") - popup_layout::scale(12);
  lv_obj_set_style_max_height(list, available, 0);
}
void input_event(lv_event_t* event) {
  auto* c = static_cast<EditableControl*>(lv_event_get_user_data(event));
  if (!c || !c->active || c->syncing) return;
  const auto code = lv_event_get_code(event);
  auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (target == c->dropdown && code == LV_EVENT_READY) { style_open_options(c); return; }
  if (target == c->dropdown && code == LV_EVENT_CANCEL) { finish_dropdown_timing(c); c->generation = 0; return; }
  if (!c->value.writable || !networkManager.isMqttConnected()) return;
  if (code == LV_EVENT_PRESSED) c->submit_scheduled = false;
  if (target == c->number_roller) {
    if (code == LV_EVENT_PRESSED) { c->pressed = target; c->dragging = true; c->command_id = ""; }
    if (code == LV_EVENT_VALUE_CHANGED && c->pressed == target) {
      const int index = lv_roller_get_selected(c->number_roller) - (c->number_options_placeholder ? 1 : 0);
      if (index >= 0 && index <= 200) {
        c->draft = std::min(c->value.maximum, c->value.minimum + index * c->value.step);
        c->draft_valid = true; c->editing = true; render_number(c);
      }
    }
    if (code == LV_EVENT_RELEASED) {
      c->dragging = false; c->generation = 0;
      if (c->pressed == target && c->editing) schedule_draft(c);
      c->pressed = nullptr;
    }
    if (code == LV_EVENT_PRESS_LOST) { c->dragging = false; finish_editing(c); c->payload = "\x01"; }
    return;
  }
  for (int i = 3; i < 6; ++i) {
    auto& field = c->fields[i];
    if (target != field.roller) continue;
    if (code == LV_EVENT_PRESSED) { c->pressed = target; c->dragging = true; c->command_id = ""; }
    if (code == LV_EVENT_VALUE_CHANGED && c->pressed == target) {
      const int index = lv_roller_get_selected(field.roller) - (field.unknown_options ? 1 : 0);
      if (index >= 0 && index <= (i == 3 ? 23 : 59)) {
        c->calendar.values[i] = index; c->draft_valid = true; c->editing = true;
        c->dragging = false; visible(c->status, false); render_calendar(c, LV_ANIM_OFF, field.roller);
        lv_obj_remove_state(c->apply, LV_STATE_DISABLED);
      }
    }
    if (code == LV_EVENT_RELEASED) {
      c->dragging = false; c->generation = 0;
      if (c->value.kind == "time" && c->editing && c->pressed == target) schedule_draft(c);
      c->pressed = nullptr;
    }
    if (code == LV_EVENT_PRESS_LOST) { c->dragging = false; finish_editing(c); c->payload = "\x01"; }
    return;
  }
  if (target == c->apply && code == LV_EVENT_CLICKED && c->editing) submit_draft(c);
  if (target == c->dropdown && code == LV_EVENT_VALUE_CHANGED) {
    const size_t index = lv_dropdown_get_selected(c->dropdown);
    if (index >= c->option_offset && index - c->option_offset < c->value.options.size())
      submit(c, c->value.options[index - c->option_offset]);
  }
  if (target == c->slider) {
    if (code == LV_EVENT_PRESSED) { c->dragging = true; c->command_id = ""; visible(c->status, false); }
    if (c->dragging && (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_RELEASED)) {
      const double steps = round(floor((c->value.maximum - c->value.minimum) / c->value.step + 1e-9) * lv_slider_get_value(c->slider) / 10000.0);
      c->draft = std::min(c->value.maximum, c->value.minimum + steps * c->value.step);
      c->draft_valid = true; render_number(c);
      // Keep the drag local and always publish its final value on release.
      if (code == LV_EVENT_RELEASED) { c->dragging = false; submit_draft(c); }
    }
    if (code == LV_EVENT_PRESS_LOST) { c->dragging = false; c->payload = "\x01"; c->generation = 0; }
    return;
  }
  bool arrow = target == c->up || target == c->down;
  for (const auto& field : c->fields) arrow = arrow || target == field.up || target == field.down;
  if (!arrow) return;
  if (code == LV_EVENT_PRESSED) { c->pressed = target; c->repeated = false; }
  if (code == LV_EVENT_LONG_PRESSED_REPEAT && c->pressed == target) { c->repeated = true; step_draft(c, target); }
  if (code == LV_EVENT_SHORT_CLICKED && c->pressed == target) {
    step_draft(c, target); c->pressed = nullptr;
    if (c->value.kind == "number" || c->value.kind == "time") schedule_draft(c);
  }
  if (code == LV_EVENT_RELEASED && c->pressed == target && c->repeated) {
    c->pressed = nullptr;
    if (c->value.kind == "number" || c->value.kind == "time") schedule_draft(c);
  }
  if (code == LV_EVENT_PRESS_LOST) { finish_editing(c); c->payload = "\x01"; }
}
void style_panel(lv_obj_t* obj) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(obj, lv_color_white(), 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x555555), 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, popup_layout::scale(10), 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}
lv_obj_t* arrow_button(lv_obj_t* parent, bool up, EditableControl* c) {
  auto* button = lv_button_create(parent);
  lv_obj_set_size(button, popup_layout::scale(48), popup_layout::scale(32));
  lv_obj_align(button, up ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(button, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, popup_layout::scale(12), 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  auto* icon = lv_label_create(button); lv_label_set_text(icon, LV_SYMBOL_DOWN);
  lv_obj_set_style_text_font(icon, &ui_symbols_20, 0);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);
  if (up) {
    lv_obj_set_style_transform_pivot_x(icon, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(icon, LV_PCT(50), 0);
    lv_obj_set_style_transform_rotation(icon, 1800, 0);
  }
  lv_obj_center(icon);
  lv_obj_add_event_cb(button, input_event, LV_EVENT_ALL, c);
  return button;
}
lv_obj_t* number_step_button(lv_obj_t* parent, bool increase, EditableControl* c) {
  auto* button = lv_button_create(parent);
  // Match the compact target control on Climate tiles, including its half-field
  // touch targets and plain mathematical symbols.
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x5A5A5A), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  disable_pressed_button_animation(button);
  lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  auto* icon = lv_label_create(button);
  lv_obj_set_style_text_font(icon, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);
  lv_label_set_text(icon, increase ? "+" : "-");
  lv_obj_align(icon, increase ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
               popup_layout::scale(increase ? -16 : 16), 0);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(button, input_event, LV_EVENT_ALL, c);
  return button;
}
void style_roller(lv_obj_t* roller) {
  style_panel(roller);
  lv_obj_set_style_bg_opa(roller, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_text_font(roller, popup_layout::font24(), LV_PART_MAIN);
  lv_obj_set_style_text_font(roller, popup_layout::font24(), LV_PART_SELECTED);
  lv_obj_set_style_text_color(roller, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_color(roller, lv_color_white(), LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(roller, LV_OPA_TRANSP, LV_PART_SELECTED);
  lv_obj_set_style_text_line_space(roller, popup_layout::scale(8), LV_PART_MAIN);
  lv_obj_set_style_clip_corner(roller, true, 0);
  lv_obj_set_style_anim_duration(roller, 120, 0);
  lv_roller_set_options(roller, "--", LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller, 1);
}

void apply_control_colors(EditableControl* c) {
  c->colors = editable_colors::from(lv_obj_get_style_bg_color(c->card, LV_PART_MAIN));
  editable_colors::dropdown(c->dropdown, c->colors);
  editable_colors::surface(c->number_box, c->colors.raised);
  editable_colors::surface(c->clock_box, c->colors.raised);
  for (auto& field : c->fields) {
    if (field.spinbox) editable_colors::surface(lv_obj_get_parent(field.spinbox), c->colors.field);
  }
}

void layout_controls(EditableControl* c) {
  const bool number = c->value.kind == "number", select = c->value.kind == "select";
  c->number_roller_enabled = number_uses_roller(c->value);
  const bool temperature = c->value.unit == "°C" || c->value.unit == "°F" || c->value.unit == "K";
  const bool slider = number && !temperature && !c->number_roller_enabled && c->value.mode != "box";
  const bool date = c->value.kind == "date" || c->value.kind == "datetime";
  const bool clock = c->value.kind == "time" || c->value.kind == "datetime";
  const bool seconds = clock;
  const int width = popup_layout::kContentWidth;
  lv_obj_set_height(c->row, editable_control_height(c->value.kind));
  visible(c->dropdown, select); visible(c->slider, slider); visible(c->number_box, number);
  visible(c->number_roller, c->number_roller_enabled);
  visible(c->up, number && !slider);
  visible(c->down, number && !slider);
  visible(c->field, !c->number_roller_enabled || c->value.unit.length());
  visible(c->apply, date); visible(c->clock_box, clock);
  const int slider_width = popup_layout::kCardWidth >= 760 ? popup_layout::scale(410) : popup_layout::scale(330);
  const int number_width = slider ? slider_width : std::min(width, popup_layout::scale(240));
  const int value_height = lv_font_get_line_height(popup_layout::headerTitleFont());
  const int knob_height = popup_layout::scale(36), value_gap = popup_layout::scale(10);
  const int number_height = slider ? value_height : std::max(48, popup_layout::scale(70));
  const int step_width = number_width / 2;
  lv_obj_set_size(c->number_box, number_width, number_height);
  lv_obj_set_style_bg_color(c->number_box, c->colors.raised, 0);
  lv_obj_set_style_bg_opa(c->number_box, slider ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c->number_box, climate_layout::kControlRadius, 0);
  lv_obj_align(c->number_box, LV_ALIGN_CENTER, 0, slider ? -(knob_height + value_gap) / 2 : 0);
  lv_obj_set_style_text_font(c->field, slider ? popup_layout::headerTitleFont() : popup_layout::font28(), 0);
  lv_obj_set_style_text_align(c->field, c->number_roller_enabled ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
  const int value_width = std::max(1, number_width - popup_layout::scale(32));
  lv_obj_set_width(c->field, c->number_roller_enabled ? popup_layout::scale(48) : slider ? number_width : value_width);
  lv_obj_align(c->field, c->number_roller_enabled ? LV_ALIGN_RIGHT_MID : LV_ALIGN_CENTER,
      c->number_roller_enabled ? -popup_layout::scale(16) : 0, 0);
  lv_obj_clear_flag(c->field, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(c->number_roller, std::max(1, value_width - (c->value.unit.length() ? popup_layout::scale(48) : 0)));
  lv_obj_align(c->number_roller, LV_ALIGN_LEFT_MID, popup_layout::scale(16), 0);
  lv_obj_set_size(c->down, step_width, number_height);
  lv_obj_set_size(c->up, number_width - step_width, number_height);
  lv_obj_align(c->down, LV_ALIGN_LEFT_MID, 0, 0); lv_obj_align(c->up, LV_ALIGN_RIGHT_MID, 0, 0);
  // A roller owns its touch area; step buttons would cover that interaction.
  visible(c->up, number && !slider && !c->number_roller_enabled);
  visible(c->down, number && !slider && !c->number_roller_enabled);
  lv_obj_move_foreground(c->field);
  lv_obj_set_width(c->slider, slider_width);
  lv_obj_align(c->slider, LV_ALIGN_CENTER, 0, (value_height + value_gap) / 2);
  const int gap = popup_layout::scale(12), apply_width = popup_layout::scale(128);
  const int cell_width = (width - apply_width - 3 * gap) / 3;
  const int date_height = editable_control_height("date");
  const int field_height = editable_control_height("time");
  const int clock_cell = std::max(56, popup_layout::scale(80)), separator_width = popup_layout::scale(16);
  const int roller_height = lv_font_get_line_height(popup_layout::font40());
  const int roller_y = popup_layout::scale(4);
  const int title_y = popup_layout::scale(8);
  const int clock_width = (seconds ? 3 : 2) * clock_cell + (seconds ? 2 : 1) * separator_width + 2 * gap;
  const int clock_x = date ? 0 : (width - clock_width) / 2;
  lv_obj_set_size(c->clock_box, clock_width, field_height);
  lv_obj_set_pos(c->clock_box, clock_x, date ? date_height + popup_layout::scale(8) : 0);
  for (int i = 0; i < 6; ++i) {
    auto& field = c->fields[i]; visible(field.box, i < 3 ? date : clock && (i != 5 || seconds));
    lv_obj_set_size(field.box, i < 3 ? cell_width : clock_cell, i < 3 ? date_height : field_height);
    lv_obj_set_pos(field.box, i < 3 ? i * (cell_width + gap) : gap + (i - 3) * (clock_cell + separator_width), 0);
    if (field.spinbox) lv_obj_set_width(field.spinbox, cell_width - popup_layout::scale(48));
    else {
      const int title_height = lv_font_get_line_height(popup_layout::font20());
      lv_obj_align(field.roller, LV_ALIGN_TOP_LEFT, 0, title_y + title_height + roller_y);
      lv_obj_set_size(field.roller, clock_cell, roller_height);
      lv_obj_set_ext_click_area(field.roller, std::max(0, (48 - roller_height) / 2));
    }
  }
  for (int i = 0; i < 2; ++i) {
    visible(c->separators[i], i == 0 || seconds);
    lv_obj_set_size(c->separators[i], separator_width, roller_height);
    lv_obj_set_pos(c->separators[i], gap + (i + 1) * clock_cell + i * separator_width, title_y + lv_font_get_line_height(popup_layout::font20()) + roller_y);
  }
  lv_obj_set_size(c->apply, apply_width, popup_layout::scale(64));
  lv_obj_align(c->apply, LV_ALIGN_RIGHT_MID, date ? 0 : -(width - clock_x - clock_width - gap - apply_width), date ? 0 : (field_height - popup_layout::scale(64)) / 2);
  // Status shares the first section's heading row. Reserve no empty row and
  // never move the controls or history when a delayed command is pending.
  const int status_height = lv_font_get_line_height(popup_layout::font20());
  const int heading_offset = (lv_font_get_line_height(popup_layout::font24()) - status_height) / 2;
  lv_obj_align(c->status, LV_ALIGN_BOTTOM_RIGHT, 0,
      status_height + popup_layout::scale(8) + heading_offset +
      (number || select ? 0 : popup_layout::scale(8)));
}
}

EditableControl* editable_control_create(lv_obj_t* row, lv_obj_t* card) {
  auto* c = new EditableControl; c->row = row; c->card = card;
  c->slider = lv_slider_create(row); lv_slider_set_range(c->slider, 0, 10000);
  ui_control_style::mediaSlider(c->slider);
  c->number_box = lv_obj_create(row); style_panel(c->number_box);
  c->field = lv_label_create(c->number_box);
  lv_label_set_long_mode(c->field, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(c->field, popup_layout::font28(), 0);
  lv_obj_set_style_text_color(c->field, lv_color_white(), 0);
  lv_obj_set_style_text_align(c->field, LV_TEXT_ALIGN_CENTER, 0);
  c->up = number_step_button(c->number_box, true, c); c->down = number_step_button(c->number_box, false, c);

  c->number_roller = lv_roller_create(c->number_box); style_roller(c->number_roller);
  lv_obj_set_style_border_width(c->number_roller, 0, 0);
  lv_roller_set_visible_row_count(c->number_roller, 1);
  lv_obj_add_event_cb(c->number_roller, input_event, LV_EVENT_ALL, c);
  c->clock_box = lv_obj_create(row); style_panel(c->clock_box);
  lv_obj_set_style_bg_color(c->clock_box, lv_color_hex(0x3A3A3A), 0);
  lv_obj_set_style_radius(c->clock_box, popup_layout::scale(18), 0);
  for (int i = 0; i < 2; ++i) {
    c->separators[i] = lv_obj_create(c->clock_box); lv_obj_remove_style_all(c->separators[i]);
    auto* colon = lv_label_create(c->separators[i]); lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, popup_layout::font24(), 0);
    lv_obj_set_style_text_color(colon, lv_color_white(), 0); lv_obj_center(colon);
  }
  for (int i = 0; i < 6; ++i) {
    auto& field = c->fields[i]; field.box = lv_obj_create(i < 3 ? row : c->clock_box); lv_obj_remove_style_all(field.box);
    lv_obj_remove_flag(field.box, LV_OBJ_FLAG_SCROLLABLE);
    auto* title = lv_label_create(field.box); lv_label_set_text(title, label(13 + i));
    lv_obj_set_style_text_font(title, popup_layout::font20(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xAAAAAA), 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, popup_layout::scale(8));
    if (i >= 3) {
      field.roller = lv_roller_create(field.box); style_roller(field.roller);
      lv_obj_set_size(field.roller, LV_PCT(100), popup_layout::scale(64));
      lv_obj_align(field.roller, LV_ALIGN_BOTTOM_MID, 0, 0);
      lv_obj_add_event_cb(field.roller, input_event, LV_EVENT_ALL, c);
      // The font includes substantial leading. Reduce the baseline pitch so
      // the next number enters promptly while only one number rests in view.
      lv_obj_set_style_text_line_space(field.roller, popup_layout::scale(-8), LV_PART_MAIN);
      lv_obj_set_style_text_font(field.roller, popup_layout::font40(), LV_PART_MAIN);
      lv_obj_set_style_text_font(field.roller, popup_layout::font40(), LV_PART_SELECTED);
      lv_roller_set_visible_row_count(field.roller, 1);
    } else {
    auto* panel = lv_obj_create(field.box); style_panel(panel);
    lv_obj_set_size(panel, LV_PCT(100), popup_layout::scale(64)); lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    field.spinbox = lv_spinbox_create(panel);
    lv_spinbox_set_range(field.spinbox, i < 3 ? 1 : 0, i == 0 ? 9999 : i == 1 ? 12 : i == 2 ? 31 : i == 3 ? 23 : 59);
    lv_spinbox_set_digit_format(field.spinbox, i == 0 ? 4 : 2, 0);
    lv_spinbox_set_step(field.spinbox, 1);
    lv_obj_set_height(field.spinbox, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(field.spinbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(field.spinbox, 0, 0);
    lv_obj_set_style_pad_all(field.spinbox, 0, 0);
    lv_obj_set_style_text_font(field.spinbox, popup_layout::font24(), 0);
    lv_obj_set_style_text_color(field.spinbox, lv_color_white(), 0);
    lv_obj_set_style_text_align(field.spinbox, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(field.spinbox, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_opa(field.spinbox, LV_OPA_TRANSP, LV_PART_CURSOR);
    for (auto flag : {LV_OBJ_FLAG_CLICKABLE, LV_OBJ_FLAG_CLICK_FOCUSABLE, LV_OBJ_FLAG_SCROLLABLE}) lv_obj_remove_flag(field.spinbox, flag);
    lv_obj_align(field.spinbox, LV_ALIGN_LEFT_MID, 0, 0);
    field.up = arrow_button(panel, true, c); field.down = arrow_button(panel, false, c);
    }
  }
  c->apply = lv_button_create(row);
  lv_obj_set_style_bg_color(c->apply, lv_color_white(), 0);
  lv_obj_set_style_text_color(c->apply, lv_color_hex(0x2A2A2A), 0);
  lv_obj_set_style_bg_color(c->apply, lv_color_hex(0xBBBBBB), LV_STATE_PRESSED);
  lv_obj_set_style_radius(c->apply, popup_layout::scale(18), 0);
  lv_obj_set_style_shadow_width(c->apply, 0, 0);
  auto* apply_label = lv_label_create(c->apply); lv_label_set_text(apply_label, label(6));
  lv_obj_set_style_text_font(apply_label, popup_layout::font20(), 0); lv_obj_center(apply_label);
  c->dropdown = lv_dropdown_create(row); lv_obj_set_width(c->dropdown, LV_PCT(100));
  ui_control_style::valueDropdown(c->dropdown); lv_obj_center(c->dropdown);
#if defined(DEVICE_GUITION_ESP32_4848S040)
  // Keep the compact Guition dropdown arrow proportional to its title-sized text.
  lv_obj_set_style_text_font(c->dropdown, &ui_symbols_20, LV_PART_INDICATOR);
#endif
  lv_obj_add_event_cb(lv_dropdown_get_list(c->dropdown), dropdown_cover_check,
                      LV_EVENT_COVER_CHECK, nullptr);
  c->status = lv_label_create(row); lv_obj_set_width(c->status, LV_PCT(58));
  lv_label_set_long_mode(c->status, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(c->status, popup_layout::font20(), 0);
  lv_obj_set_style_text_color(c->status, lv_color_white(), 0);
  lv_obj_set_style_text_align(c->status, LV_TEXT_ALIGN_RIGHT, 0);
  for (auto* obj : {c->slider, c->apply, c->dropdown}) lv_obj_add_event_cb(obj, input_event, LV_EVENT_ALL, c);
  apply_control_colors(c);
  visible(row, false); return c;
}

void editable_control_open(EditableControl* c, const String& entity) {
  if (!c) return;
  editable_control_close(c); c->entity = entity; c->active = true; active_control = c;
  c->value = {}; c->payload = "\x01"; c->generation = 0;
  apply_control_colors(c);
  visible(c->row, true); editable_control_refresh(c);
}

void editable_control_refresh(EditableControl* c) {
  if (!c || !c->active) return;
  const bool online = networkManager.isMqttConnected();
  if (!online && c->online) { finish_editing(c); c->dragging = false; lv_dropdown_close(c->dropdown); c->payload = "\x01"; }
  if (c->command_id.length() && (!online || millis() - c->command_ms >= 30000)) {
    c->command_id = ""; c->payload = "\x01"; c->generation = 0; status_text(c, 9);
  }
  if (c->generation == value_generation && c->online == online) {
    // Coalesce quick taps and successive clock fields without blocking LVGL.
    if (c->submit_scheduled && online && !c->pressed && !c->dragging && millis() - c->edit_ms >= 600) {
      c->submit_scheduled = false;
      submit_draft(c);
    }
    return;
  }
  c->generation = value_generation; c->online = online;
  String payload = haBridgeConfig.findEditableValue(c->entity);
  if (payload != c->payload) {
    EditableValue next = parse_editable_value(payload);
    const bool constraints_changed = next.revision != c->value.revision || next.session != c->value.session || !next.writable;
    if (constraints_changed) { finish_editing(c); c->dragging = false; c->command_id = ""; lv_dropdown_close(c->dropdown); }
    if (c->command_id.length() && command_value_confirmed(c, next)) {
      c->command_id = ""; visible(c->status, false);
    }
    // Keep the native list and its scroll position stable while it is open.
    // Session, capability and availability changes above still close it.
    if (!c->editing && !c->dragging && !c->command_id.length() && !lv_dropdown_is_open(c->dropdown)) {
      c->syncing = true; c->value = std::move(next); c->payload = payload; c->generation = value_generation;
      layout_controls(c);
      c->draft_valid = c->value.has_state && c->value.available && c->value.state != "unknown";
      if (c->value.kind == "number") {
        char* end = nullptr; c->draft = strtod(c->value.state.c_str(), &end);
        c->draft_valid = c->draft_valid && end && end != c->value.state.c_str() && !*end && std::isfinite(c->draft);
        render_number(c);
        if (c->number_roller_enabled) render_number_options(c);
        const double ratio = c->draft_valid && c->value.writable ? (c->draft - c->value.minimum) / (c->value.maximum - c->value.minimum) : 0;
        lv_slider_set_value(c->slider, std::isfinite(ratio) ? static_cast<int>(std::max(0.0, std::min(1.0, ratio)) * 10000) : 0, LV_ANIM_OFF);
      } else if (c->value.kind == "select") {
        String options; int selected = -1;
        for (size_t i = 0; i < c->value.options.size(); ++i) {
          if (i) options += "\n"; options += c->value.options[i];
          if (c->value.options[i] == c->value.state) selected = static_cast<int>(i);
        }
        c->option_offset = selected < 0 ? 1 : 0;
        if (c->option_offset) options = editable_display_value(c->value) + (options.length() ? "\n" + options : String());
        const char* options_text = options.length() ? options.c_str() : "--";
        if (strcmp(lv_dropdown_get_options(c->dropdown), options_text) != 0)
          lv_dropdown_set_options(c->dropdown, options_text);
        lv_dropdown_set_selected(c->dropdown, selected >= 0 ? selected : 0);
        lv_dropdown_set_text(c->dropdown, nullptr);
      } else {
        c->calendar = {};
        const time_t now = time(nullptr); struct tm local;
        if (localtime_r(&now, &local)) {
          c->calendar.values[0] = local.tm_year + 1900; c->calendar.values[1] = local.tm_mon + 1;
          c->calendar.values[2] = local.tm_mday;
        }
        c->draft_valid = c->draft_valid && value_editor::parseCalendar(c->value.state.c_str(),
            c->value.kind != "time", c->value.kind != "date", c->calendar);
        render_calendar(c);
      }
      c->syncing = false;
    }
  }
  const bool enabled = online && c->value.valid && c->value.writable;
  auto enable = [enabled](lv_obj_t* obj) {
    if (!obj) return;
    if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED); else lv_obj_add_state(obj, LV_STATE_DISABLED);
  };
  for (auto* obj : {c->slider, c->number_box, c->number_roller, c->up, c->down, c->dropdown}) enable(obj);
  for (const auto& field : c->fields) {
    enable(field.box);
    if (field.roller) enable(field.roller);
    enable(field.up); enable(field.down);
  }
  if (enabled && c->editing && c->draft_valid) lv_obj_remove_state(c->apply, LV_STATE_DISABLED);
  else lv_obj_add_state(c->apply, LV_STATE_DISABLED);
}

void editable_control_close(EditableControl* c) {
  if (!c) return;
  finish_editing(c); lv_dropdown_close(c->dropdown); c->command_id = "";
  finish_dropdown_timing(c);
  // LVGL places the expanded list on the screen. Keep the hidden list with
  // its owning dropdown when the reusable popup is parked or screens change.
  if (auto* list = lv_dropdown_get_list(c->dropdown)) lv_obj_set_parent(list, c->dropdown);
  c->dragging = false; c->active = false; visible(c->row, false); visible(c->status, false);
  lv_roller_set_selected(c->number_roller, lv_roller_get_selected(c->number_roller), LV_ANIM_OFF);
  for (const auto& field : c->fields) if (field.roller)
    lv_roller_set_selected(field.roller, lv_roller_get_selected(field.roller), LV_ANIM_OFF);
  if (active_control == c) active_control = nullptr;
}
void editable_control_delete(EditableControl* c) {
  if (!c) return;
  editable_control_close(c);
  // The overlay DELETE event precedes deletion of its children. Detach their
  // callbacks before freeing the data used by those later child events.
  for (auto* obj : {c->slider, c->apply, c->dropdown, c->number_roller, c->up, c->down})
    lv_obj_remove_event_cb_with_user_data(obj, input_event, c);
  for (const auto& field : c->fields) {
    if (field.roller) lv_obj_remove_event_cb_with_user_data(field.roller, input_event, c);
    if (field.up) lv_obj_remove_event_cb_with_user_data(field.up, input_event, c);
    if (field.down) lv_obj_remove_event_cb_with_user_data(field.down, input_event, c);
  }
  delete c;
}

bool editable_handle_ack(const char* topic, const char* payload, size_t length) {
  if (String(topic) != mqttTopics.deviceBase() + "/stat/value") return false;
  auto* c = active_control;
  if (!c || !payload || length > 1024) return true;
  StaticJsonDocument<1536> doc;
  if (deserializeJson(doc, payload, length)) return true;
  if (c->entity != (doc["entity_id"] | "") || !c->command_id.length() || c->command_id != (doc["id"] | "")) return true;
  // HA accepting the service does not confirm the device's state. Keep the
  // local value until matching state arrives, or the existing timeout expires.
  if (String(doc["status"] | "") == "ok") {
    if (command_value_confirmed(c, c->value)) { c->command_id = ""; visible(c->status, false); }
    return true;
  }
  c->command_id = ""; c->payload = "\x01"; c->generation = 0;
  status_text(c, 9);
  return true;
}
