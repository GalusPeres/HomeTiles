#include "src/types/switch/web_html.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

void append_switch_fields_html(String& html, const String& tab_id, const std::vector<String>& switchOptions) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <!-- Switch Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_switch_fields" class="type-fields">
              <label>)html";
  html += tr.switch_light;
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  html += R"html(_switch_entity">
                <option value="">)html";
  html += tr.no_selection;
  html += R"html(</option>
)html";

  for (const auto& opt : switchOptions) {
    html += "<option value=\"";
    appendHtmlEscaped(html, opt);
    html += "\">";
    String label = humanizeIdentifier(opt, true) + " - " + opt;
    appendHtmlEscaped(html, label);
    html += "</option>";
  }

  html += R"html(
              </select>
              <label>)html";
  html += tr.switch_display;
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  // Values are switch_layout::Layout; Automatic first, the default of new
  // tiles.
  html += R"html(_switch_style">
                <option value="3">)html";
  html += tr.switch_layout_automatic;
  html += R"html(</option>
                <option value="2">)html";
  html += tr.switch_layout_dimmer;
  html += R"html(</option>
                <option value="1">)html";
  html += tr.switch_layout_switch;
  html += R"html(</option>
                <option value="0">)html";
  html += tr.switch_icon_button;
  html += R"html(</option>
              </select>
)html";
  if (tab_id != "screensaver") {
    html += R"html(              <label>)html";
    html += tr.popup_open;
    html += R"html(</label>
              <select id=")html";
    html += tab_id;
    html += R"html(_switch_popup_open_mode">
                <option value="0">)html";
    html += tr.long_press;
    html += R"html(</option>
                <option value="1">)html";
    html += tr.short_press;
    html += R"html(</option>
              </select>
)html";
  }
  html += R"html(            </div>
)html";
}
