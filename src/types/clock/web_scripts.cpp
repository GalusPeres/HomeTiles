#include "src/types/clock/web_scripts.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

namespace {

void append_clock_js_string(String& html, const char* value) {
  html += "'";
  for (const char* c = value ? value : ""; *c; ++c) {
    switch (*c) {
      case '\\': html += "\\\\"; break;
      case '\'': html += "\\'"; break;
      case '\r': break;
      case '\n': html += "\\n"; break;
      case '<': html += "\\x3c"; break;
      default: html += *c; break;
    }
  }
  html += "'";
}

void append_clock_js_list(String& html, const char* key, const char* const* values, size_t count) {
  html += "    ";
  html += key;
  html += ": [";
  for (size_t i = 0; i < count; ++i) {
    if (i) html += ", ";
    append_clock_js_string(html, values[i]);
  }
  html += "],\n";
}

}  // namespace

// The editable JavaScript lives in src/web/assets/admin.js and is served as
// one precompressed, cacheable firmware asset. Its texts come from here: the
// clock preview's month names, date order and weekdays in the panel's
// language (clock_tile::format_date_line).
void append_clock_scripts(String& html) {
  const i18n::LocaleProfile& locale = i18n::locale(configManager.getConfig().language);
  html += "\n  <script>\n  const CLOCK_I18N = Object.freeze({\n";
  append_clock_js_list(html, "months", locale.month_names_in_date, 12);
  append_clock_js_list(html, "weekdays", locale.weekday_names, 7);
  append_clock_js_list(html, "weekdaysShort", locale.weather_weekdays_short, 7);
  html += "    pattern: ";
  append_clock_js_string(html, locale.long_date_pattern);
  html += ",\n    patternYear: ";
  append_clock_js_string(html, locale.long_date_year_pattern);
  html += "\n  });\n  </script>\n";
}
