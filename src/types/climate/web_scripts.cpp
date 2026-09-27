#include "src/types/climate/web_scripts.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

namespace {

void append_climate_js_string(String& html, const String& value) {
  html += "'";
  for (size_t index = 0; index < value.length(); ++index) {
    const char c = value[index];
    switch (c) {
      case '\\': html += "\\\\"; break;
      case '\'': html += "\\'"; break;
      case '\r': break;
      case '\n': html += "\\n"; break;
      case '<': html += "\\x3c"; break;
      default: html += c; break;
    }
  }
  html += "'";
}
}  // namespace

void append_climate_scripts(String& html) {
  html += R"html(
  <script>
)html";

  const char* language = configManager.getConfig().language;
  html += "  const CLIMATE_I18N = Object.freeze({\n";
  auto append_i18n = [&](const char* key, const String& value) {
    html += "    ";
    html += key;
    html += ": ";
    append_climate_js_string(html, value);
    html += ",\n";
  };
  append_i18n("automatic", i18n::climate_mini_label(language, 0));
  append_i18n("empty", i18n::climate_mini_label(language, 1));
  append_i18n("emptyRemove", i18n::climate_mini_label(language, 2));
  append_i18n("emptyField", i18n::climate_mini_label(language, 3));
  append_i18n("selectedField", i18n::climate_mini_label(language, 4));
  append_i18n("horizontal", i18n::climate_mini_label(language, 5));
  append_i18n("vertical", i18n::climate_mini_label(language, 6));
  append_i18n("currentTemperature", i18n::climate_value_label(language, 0));
  append_i18n("currentHumidity", i18n::climate_value_label(language, 2));
  append_i18n(
      "targetTemperature",
      i18n::climate_target_temperature_label(language));
  append_i18n(
      "targetHumidity",
      i18n::climate_target_humidity_label(language));
  append_i18n(
      "humidityCaption",
      i18n::climate_humidity_caption_label(language));
  append_i18n("mode", i18n::climate_control_label(language, 0));
  append_i18n("off", i18n::climate_state_label(language, "off", ""));
  append_i18n("heat", i18n::climate_state_label(language, "heat", ""));
  append_i18n("cool", i18n::climate_state_label(language, "cool", ""));
  append_i18n(
      "heatCool",
      i18n::climate_state_label(language, "heat_cool", ""));
  append_i18n("autoMode", i18n::climate_state_label(language, "auto", ""));
  append_i18n("dry", i18n::climate_state_label(language, "dry", ""));
  append_i18n(
      "fanOnly",
      i18n::climate_state_label(language, "fan_only", ""));
  append_i18n(
      "genericClimate",
      i18n::climate_state_label(language, "", ""));
  append_i18n(
      "unavailable",
      i18n::entity_state_label(language, "unavailable"));
  append_i18n(
      "unknown",
      i18n::entity_state_label(language, "unknown"));
  html += "  });\n  </script>\n";
}
