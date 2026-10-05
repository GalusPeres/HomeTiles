#include "src/types/binary_sensor/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/web/server/web_admin_utils.h"

void append_binary_sensor_fields_html(String& html, const String& tab_id) {
  const char* language = configManager.getConfig().language;
  const auto& tr = i18n::strings(language);

  html += "<div id=\"";
  html += tab_id;
  html += "_binary_sensor_fields\" class=\"type-fields\">";
  appendEntityPickerField(html, tab_id, "binary_sensor_entity",
                          i18n::binary_sensor_label(language, 1), "binary_sensors");

  html += "<label>";
  html += tr.sensor_value_size;
  html += "</label><select id=\"";
  html += tab_id;
  html += "_binary_sensor_value_font\"><option value=\"0\">";
  html += tr.sensor_value_size_default;
  html += "</option><option value=\"1\">20</option><option value=\"2\">24</option>";
  html += "<option value=\"3\">32</option><option value=\"4\">40</option>";
  html += "<option value=\"5\" hidden disabled>28</option></select>";

  if (tab_id != "screensaver") {
    html += "<label>";
    html += tr.popup_open;
    html += "</label><select id=\"";
    html += tab_id;
    html += "_binary_sensor_popup_open_mode\"><option value=\"1\">";
    html += tr.short_press;
    html += "</option><option value=\"0\">";
    html += tr.long_press;
    html += "</option></select>";
  }
  html += "</div>\n";
}
