#include "src/types/weather/web_html.h"
#include "src/web/server/web_admin_utils.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

void append_weather_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <!-- Weather Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_weather_fields" class="type-fields">
              )html";
  appendEntityPickerField(html, tab_id, "weather_entity", tr.weather_entity, "weathers");
  html += R"html(
)html";
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
