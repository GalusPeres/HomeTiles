#include "src/types/calendar/web.h"

#include <ArduinoJson.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/calendar/calendar_model.h"
#include "src/web/server/web_admin_utils.h"

namespace {

const char* language() { return configManager.getConfig().language; }

const char* text(i18n::CalendarLabel label) { return i18n::calendar_label(language(), label); }

// An empty field, or a Home Assistant calendar id.
bool calendar_entity(const String& entity) {
  if (!entity.length()) return true;
  if (!entity.startsWith("calendar.") || entity.length() <= 9 || entity.length() > 128) return false;
  for (size_t i = 9; i < entity.length(); ++i) {
    const char c = entity[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

const char* field_name(uint8_t index) {
  static const char* const kFields[calendar_model::kMaxCalendars] = {"calendar_entity", "calendar_entity_2",
                                                                    "calendar_entity_3", "calendar_entity_4"};
  return kFields[index];
}

void append_option(String& html, const char* value, const char* label) {
  html += "<option value=\"";
  html += value;
  html += "\">";
  appendHtmlEscaped(html, label);
  html += "</option>";
}

}  // namespace

void append_calendar_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(language());
  const auto& loc = i18n::locale(language());
  html += "<div id=\"";
  html += tab_id;
  html += "_calendar_fields\" class=\"type-fields\">";
  for (uint8_t i = 0; i < calendar_model::kMaxCalendars; ++i) {
    char label[64];
    if (i == 0) {
      snprintf(label, sizeof(label), "%s", text(i18n::CalendarLabel::EntityCalendar));
    } else {
      snprintf(label, sizeof(label), text(i18n::CalendarLabel::CalendarN), static_cast<unsigned>(i + 1));
    }
    appendEntityPickerField(html, tab_id, field_name(i), label, "calendars");
  }
  html += "<label for=\"";
  html += tab_id;
  html += "_calendar_view\">";
  appendHtmlEscaped(html, text(i18n::CalendarLabel::View));
  html += "</label><select id=\"";
  html += tab_id;
  html += "_calendar_view\">";
  append_option(html, "0", text(i18n::CalendarLabel::ViewAgenda));
  append_option(html, "1", text(i18n::CalendarLabel::ViewWeek));
  // The month grid needs a panel larger than 480x480 (calendar_tile.cpp).
#if !defined(DEVICE_LAYOUT_480X480)
  append_option(html, "2", text(i18n::CalendarLabel::ViewMonth));
#endif
  // Today: the count of today's events on a half-height tile.
  append_option(html, "3", text(i18n::CalendarLabel::ViewToday));
  html += "</select>";
  html += "<label for=\"";
  html += tab_id;
  html += "_calendar_week_start\">";
  appendHtmlEscaped(html, text(i18n::CalendarLabel::WeekStart));
  html += "</label><select id=\"";
  html += tab_id;
  html += "_calendar_week_start\">";
  // Automatic follows the panel language (LocaleProfile::first_weekday).
  String automatic = text(i18n::CalendarLabel::Automatic);
  automatic += " (";
  automatic += loc.weekday_names[loc.first_weekday <= 6 ? loc.first_weekday : 1];
  automatic += ")";
  append_option(html, "255", automatic.c_str());
  append_option(html, "1", loc.weekday_names[1]);
  append_option(html, "0", loc.weekday_names[0]);
  append_option(html, "6", loc.weekday_names[6]);
  html += "</select>";
  if (tab_id != "screensaver") {
    html += "<label>";
    html += tr.popup_open;
    html += "</label><select id=\"";
    html += tab_id;
    html += "_calendar_popup_open_mode\"><option value=\"1\">";
    html += tr.short_press;
    html += "</option><option value=\"0\">";
    html += tr.long_press;
    html += "</option></select>";
  }
  html += "</div>\n";
}

bool apply_calendar_fields_from_request(WebServer& server, Tile& tile) {
  String list;
  bool valid = true;
  uint8_t count = 0;
  for (uint8_t i = 0; i < calendar_model::kMaxCalendars; ++i) {
    String entity = server.hasArg(field_name(i)) ? server.arg(field_name(i)) : String();
    entity.trim();
    if (!calendar_entity(entity)) {
      valid = false;
      continue;
    }
    if (!entity.length() || ("," + list + ",").indexOf("," + entity + ",") >= 0) continue;
    if (count++) list += ",";
    list += entity;
  }
  // A request without the per-field names (an older import) keeps the list
  // in sensor_entity.
  if (!count && server.hasArg("sensor_entity")) {
    String fallback = server.arg("sensor_entity");
    fallback.trim();
    int start = 0;
    while (start <= static_cast<int>(fallback.length()) && count < calendar_model::kMaxCalendars) {
      int end = fallback.indexOf(',', start);
      if (end < 0) end = fallback.length();
      String entity = fallback.substring(start, end);
      entity.trim();
      start = end + 1;
      if (!entity.length()) continue;
      if (!calendar_entity(entity)) {
        valid = false;
        continue;
      }
      if (("," + list + ",").indexOf("," + entity + ",") >= 0) continue;
      if (count++) list += ",";
      list += entity;
    }
  }
  tile.sensor_entity = list;
  const uint8_t popup_mode =
      server.hasArg("popup_open_mode") && server.arg("popup_open_mode").toInt() == TILE_POPUP_OPEN_LONG_PRESS
          ? TILE_POPUP_OPEN_LONG_PRESS
          : TILE_POPUP_OPEN_SHORT_PRESS;
  setTilePopupOpenMode(tile, popup_mode);
  tile.sensor_unit = "";
  tile.sensor_display_mode =
      calendar_model::normalize_view(server.hasArg("calendar_view") ? server.arg("calendar_view").toInt() : 0);
  tile.sensor_decimals = calendar_model::normalize_week_start(
      server.hasArg("calendar_week_start") ? server.arg("calendar_week_start").toInt() : 255);
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
  tile.key_code = 0;
  tile.key_modifier = 0;
  return valid;
}

void append_calendar_scripts(String& html) {
  // The preview's words from the central translations (escaped for a script
  // element).
  JsonDocument doc;
  const char* lang = language();
  const auto& loc = i18n::locale(lang);
  JsonArray labels = doc["labels"].to<JsonArray>();
  for (size_t i = 0; i < static_cast<size_t>(i18n::CalendarLabel::Count); ++i) {
    labels.add(i18n::calendar_label(lang, static_cast<i18n::CalendarLabel>(i)));
  }
  JsonArray months = doc["months"].to<JsonArray>();
  for (int m = 1; m <= 12; ++m) months.add(i18n::month_name(lang, m));
  JsonArray months_short = doc["monthsShort"].to<JsonArray>();
  for (int m = 1; m <= 12; ++m) months_short.add(i18n::weather_month_short(lang, m));
  JsonArray weekdays = doc["weekdaysShort"].to<JsonArray>();
  for (int d = 0; d < 7; ++d) weekdays.add(loc.weather_weekdays_short[d]);
  JsonArray weekday_names = doc["weekdays"].to<JsonArray>();
  for (int d = 0; d < 7; ++d) weekday_names.add(loc.weekday_names[d]);
  doc["today"] = i18n::weather_today_label(lang);
  doc["tomorrow"] = i18n::weather_tomorrow_label(lang);
  doc["shortDate"] = loc.short_date_pattern;
  doc["firstWeekday"] = loc.first_weekday;
  doc["monthView"] =
#if defined(DEVICE_LAYOUT_480X480)
      false;
#else
      true;
#endif
  String json;
  serializeJson(doc, json);
  json.replace("<", "\\u003c");
  html += "<script>const CALENDAR_I18N=Object.freeze(";
  html += json;
  html += ");</script>\n";
}

void append_calendar_styles(String&) {}
