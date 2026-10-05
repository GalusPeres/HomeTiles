#include "src/types/number/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/web/server/web_admin_utils.h"

void append_number_fields_html(String& html, const String& tab_id) {
  const char* language = configManager.getConfig().language;
  const auto& tr = i18n::strings(language);

  html += "<div id=\"";
  html += tab_id;
  html += "_number_fields\" class=\"type-fields\">";
  appendEntityPickerField(html, tab_id, "number_entity",
                          i18n::locale(language).editable_labels[3], "numbers");

  html += "<label>";
  html += tr.sensor_value_size;
  html += "</label><select id=\"";
  html += tab_id;
  html += "_number_value_font\"><option value=\"1\">20</option>"
          "<option value=\"2\" selected>24</option><option value=\"0\">28</option>"
          "<option value=\"3\">32</option><option value=\"4\">40</option></select>";

  if (tab_id != "screensaver") {
    html += "<label>";
    html += tr.popup_open;
    html += "</label><select id=\"";
    html += tab_id;
    html += "_number_popup_open_mode\"><option value=\"1\">";
    html += tr.short_press;
    html += "</option><option value=\"0\">";
    html += tr.long_press;
    html += "</option></select>";
  }
  html += "</div>\n";
}
