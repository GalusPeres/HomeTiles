#include "src/types/weather/web_html.h"
#include "src/web/server/web_admin_utils.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

void append_weather_fields_html(String& html, const String& tab_id, const std::vector<String>& weatherOptions,
                                const std::vector<String>& sensorOptions) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  const char* language = configManager.getConfig().language;
  html += R"html(
            <!-- Weather Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_weather_fields" class="type-fields">
              <label>)html";
  html += tr.weather_entity;
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  html += R"html(_weather_entity">
                <option value="">)html";
  html += tr.no_selection;
  html += R"html(</option>
)html";

  for (const auto& opt : weatherOptions) {
    html += "<option value=\"";
    appendHtmlEscaped(html, opt);
    html += "\">";
    String label = humanizeIdentifier(opt, true) + " - " + opt;
    appendHtmlEscaped(html, label);
    html += "</option>";
  }

  html += R"html(
              </select>
)html";
  // Optional sensors shown instead of the weather entity's current
  // temperature, plus a humidity value. The empty option keeps the weather
  // entity's own value; the forecast always comes from the weather entity.
  const char* const sensor_fields[] = {"weather_temperature_sensor",
                                       "weather_humidity_sensor"};
  for (uint8_t field = 0; field < 2; ++field) {
    html += "              <label>";
    html += i18n::weather_sensor_label(language, field);
    html += "</label><select id=\"";
    html += tab_id;
    html += "_";
    html += sensor_fields[field];
    html += "\"><option value=\"\">";
    html += i18n::weather_sensor_label(language, 2);
    html += "</option>";
    for (const auto& entity : sensorOptions) {
      html += "<option value=\"";
      appendHtmlEscaped(html, entity);
      html += "\">";
      appendHtmlEscaped(html, humanizeIdentifier(entity, true) + " - " + entity);
      html += "</option>";
    }
    html += "</select>\n";
  }
  if (tab_id != "screensaver") {
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
  }
  html += R"html(            </div>
)html";
}
