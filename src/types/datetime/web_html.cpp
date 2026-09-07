#include "src/types/datetime/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/web/server/web_admin_utils.h"

void append_datetime_fields_html(
    String& html, const String& tab_id,
    const std::vector<String>& datetime_options) {
  const char* language = configManager.getConfig().language;
  const auto& tr = i18n::strings(language);

  html += "<div id=\"";
  html += tab_id;
  html += "_datetime_fields\" class=\"type-fields\"><label>";
  html += i18n::locale(language).editable_labels[5];
  html += "</label><select id=\"";
  html += tab_id;
  html += "_datetime_entity\"><option value=\"\">";
  html += tr.no_selection;
  html += "</option>";
  for (const String& entity : datetime_options) {
    html += "<option value=\"";
    appendHtmlEscaped(html, entity);
    html += "\">";
    String name = haBridgeConfig.findSensorName(entity);
    if (!name.length()) name = humanizeIdentifier(entity, true);
    appendHtmlEscaped(html, name + " - " + entity);
    html += "</option>";
  }
  html += "</select>";

  html += "<label>";
  html += tr.sensor_value_size;
  html += "</label><select id=\"";
  html += tab_id;
  html += "_datetime_value_font\"><option value=\"1\">20</option>"
          "<option value=\"2\" selected>24</option><option value=\"0\">28</option>"
          "<option value=\"3\">32</option><option value=\"4\">40</option></select>";

  if (tab_id != "screensaver") {
    html += "<label>";
    html += tr.popup_open;
    html += "</label><select id=\"";
    html += tab_id;
    html += "_datetime_popup_open_mode\"><option value=\"1\">";
    html += tr.short_press;
    html += "</option><option value=\"0\">";
    html += tr.long_press;
    html += "</option></select>";
  }
  html += "</div>\n";
}
