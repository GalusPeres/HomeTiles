#include "src/types/weather/web_html.h"
#include "src/web/server/web_admin_utils.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

void append_weather_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  const char* language = configManager.getConfig().language;
  html += R"html(
            <!-- Weather Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_weather_fields" class="type-fields">
              )html";
  appendEntityPickerField(html, tab_id, "weather_entity", tr.weather_entity, "weathers");
  html += R"html(
)html";
  // Optional sensors shown instead of the weather entity's current
  // temperature, plus a humidity value, chosen like every entity (the
  // picker). Empty keeps the weather entity's own value, as the field says;
  // the forecast always comes from the weather entity.
  const char* const sensor_fields[] = {"weather_temperature_sensor",
                                       "weather_humidity_sensor"};
  for (uint8_t field = 0; field < 2; ++field) {
    appendEntityPickerField(html, tab_id, sensor_fields[field], i18n::weather_sensor_label(language, field), "sensors",
                            i18n::weather_sensor_label(language, 2));
    html += "\n";
  }
  html += R"html(              <label>)html";
  html += tr.popup_open;
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  html += R"html(_weather_popup_open_mode">
                <option value="0">)html";
  html += tr.long_press;
  html += R"html(</option>
                <option value="1" selected>)html";
  html += tr.short_press;
  html += R"html(</option>
              </select>
)html";
  html += R"html(            </div>
)html";
}
