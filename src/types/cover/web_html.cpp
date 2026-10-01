#include "src/types/cover/web_html.h"
#include "src/types/switch/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/web/server/web_admin_utils.h"

void append_cover_fields_html(String& html, const String& tab_id,
                              const std::vector<String>& cover_options) {
  const char* language = configManager.getConfig().language;
  const auto& tr = i18n::strings(language);
  html += "<div id=\"";
  html += tab_id;
  html += "_cover_fields\" class=\"type-fields\"><label>";
  html += i18n::cover_label(language, 1);
  html += "</label><select id=\"";
  html += tab_id;
  html += "_cover_entity\"><option value=\"\">";
  html += tr.no_selection;
  html += "</option>";
  for (const String& entity : cover_options) {
    html += "<option value=\"";
    appendHtmlEscaped(html, entity);
    html += "\">";
    appendHtmlEscaped(html, humanizeIdentifier(entity, true) + " - " + entity);
    html += "</option>";
  }
  html += "</select>";
  // The state size like the Switch tile (tile_header.h): the half-height
  // Sensor value sizes beside the disc, the Sensor value sizes from 1.5 rows.
  const SwitchChoice sizes[] = {{"0", tr.sensor_value_size_default}, {"1", "20"}, {"2", "24"},
                                {"5", "28"}, {"3", "32"}, {"4", "40"}};
  append_switch_choice(html, tab_id, "cover_value_font", tr.sensor_value_size, sizes, 6);
  if (tab_id != "screensaver") {
    html += "<label>";
    html += tr.popup_open;
    html += "</label><select id=\"";
    html += tab_id;
    html += "_cover_popup_open_mode\"><option value=\"1\">";
    html += tr.short_press;
    html += "</option><option value=\"0\">";
    html += tr.long_press;
    html += "</option></select>";
  }
  html += "</div>\n";
}
