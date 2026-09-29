#include "src/types/weather/weather_icons.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "src/devices/device_select.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/types/weather/weather_icon_table.h"

// Same size as FONT_MDI_ICONS; each font falls back to that MDI font.
#if defined(DEVICE_LAYOUT_1024X600)
extern "C" { LV_FONT_DECLARE(weather_icons_40) }
#define WEATHER_ICON_FONT (&weather_icons_40)
#elif defined(DEVICE_LAYOUT_480X480)
extern "C" { LV_FONT_DECLARE(weather_icons_32) }
#define WEATHER_ICON_FONT (&weather_icons_32)
#else
extern "C" { LV_FONT_DECLARE(weather_icons_48) }
#define WEATHER_ICON_FONT (&weather_icons_48)
#endif

namespace weather_icons {
namespace {

const weather_icon_table::Entry* find(const String& icon_name) {
  const String name = normalizeMdiIconName(icon_name);
  for (const auto& entry : weather_icon_table::kEntries) {
    if (name == entry.name) return &entry;
  }
  return nullptr;
}

// The value after "key": (spaces allowed) inside [p, end), or nullptr.
const char* value_of(const char* p, const char* end, const char* key) {
  const size_t length = strlen(key);
  for (; p + length + 2 < end; ++p) {
    if (p[0] != '"' || strncmp(p + 1, key, length) != 0 || p[length + 1] != '"') continue;
    const char* value = p + length + 2;
    while (value < end && *value == ' ') ++value;
    if (value >= end || *value != ':') continue;
    ++value;
    while (value < end && *value == ' ') ++value;
    return value < end ? value : nullptr;
  }
  return nullptr;
}

bool int_of(const char* p, const char* end, const char* key, int& out) {
  const char* value = value_of(p, end, key);
  if (!value || !(isdigit(static_cast<unsigned char>(*value)) || *value == '-')) return false;
  out = atoi(value);
  return true;
}

}  // namespace

const lv_font_t* font() { return WEATHER_ICON_FONT; }

void style_label(lv_obj_t* label) {
  if (!label) return;
  lv_obj_set_style_text_font(label, WEATHER_ICON_FONT, 0);
  lv_label_set_recolor(label, true);
}

String text(const String& icon_name, Style style) {
  if (style != Style::Outline) {
    if (const auto* entry = find(icon_name)) {
      return String(style == Style::Single ? entry->plain : entry->text);
    }
  }
  return getMdiChar(icon_name);
}

uint32_t tint(const String& icon_name) {
  const auto* entry = find(icon_name);
  return entry ? entry->tint : 0;
}

String at_night(const String& icon_name) {
  const String name = normalizeMdiIconName(icon_name);
  if (name == "weather-partly-cloudy") return String("weather-night-partly-cloudy");
  if (name == "weather-sunny") return String("weather-night");
  return icon_name;
}

bool parse_sun(const char* json, SunTimes& out) {
  out = SunTimes{};
  if (!json) return false;
  const char* end = json + strlen(json);
  const char* list = value_of(json, end, "sun");
  if (!list || *list != '[') return false;
  const char* list_end = static_cast<const char*>(memchr(list, ']', static_cast<size_t>(end - list)));
  if (!list_end) return false;
  const char* object = list;
  while (out.count < SunTimes::kMaxDays &&
         (object = static_cast<const char*>(
              memchr(object, '{', static_cast<size_t>(list_end - object)))) != nullptr) {
    const char* object_end =
        static_cast<const char*>(memchr(object, '}', static_cast<size_t>(list_end - object)));
    if (!object_end) break;
    const char* date = value_of(object, object_end, "d");
    SunDay& day = out.days[out.count];
    if (date && *date == '"' && object_end - date > 11 && date[11] == '"') {
      memcpy(day.date, date + 1, 10);
      day.date[10] = '\0';
      int rise = -1;
      int set = -1;
      // Polar day or night has no sunrise and sunset.
      if (const char* up = value_of(object, object_end, "up")) {
        day.up = strncmp(up, "true", 4) == 0 ? 1 : 0;
        ++out.count;
      } else if (int_of(object, object_end, "r", rise) && int_of(object, object_end, "s", set) &&
                 rise >= 0 && rise < set && set <= 24 * 60) {
        day.rise = static_cast<int16_t>(rise);
        day.set = static_cast<int16_t>(set);
        ++out.count;
      }
    }
    object = object_end + 1;
  }
  return out.count > 0;
}

bool is_night(const SunTimes& sun, const char* date, int minute) {
  if (!date) return false;
  for (uint8_t i = 0; i < sun.count; ++i) {
    const SunDay& day = sun.days[i];
    if (strncmp(day.date, date, 10) != 0 || date[10] != '\0') continue;
    if (day.up >= 0) return day.up == 0;
    return minute < day.rise || minute >= day.set;
  }
  return false;
}

String for_now(const String& icon_name, const SunTimes& sun) {
  if (!sun.count) return icon_name;
  struct tm now {};
  if (!getLocalTime(&now, 0)) return icon_name;
  char date[16];
  strftime(date, sizeof(date), "%Y-%m-%d", &now);
  return is_night(sun, date, now.tm_hour * 60 + now.tm_min) ? at_night(icon_name) : icon_name;
}

void follow_icon_color(lv_obj_t* label, bool forced) {
  if (!label) return;
  const char* current = lv_label_get_text(label);
  if (!current) return;
  for (const auto& entry : weather_icon_table::kEntries) {
    if (strcmp(current, forced ? entry.text : entry.plain) != 0) continue;
    lv_label_set_text(label, forced ? entry.plain : entry.text);
    return;
  }
}

}  // namespace weather_icons
