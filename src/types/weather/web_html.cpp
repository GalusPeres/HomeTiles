#include "src/types/weather/web_html.h"
#include "src/web/server/web_admin_utils.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

void append_weather_fields_html(String& html, const String& tab_id, const std::vector<String>& weatherOptions) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
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
