#include "src/types/energy/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/web/server/web_admin_utils.h"

void append_energy_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);

  html += R"html(
            <!-- Energy Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_energy_fields" class="type-fields">
              )html";
  appendEntityPickerField(html, tab_id, "energy_entity", tr.energy_entity, "energy");
  html += R"html(
              <label>)html";
  html += tr.sensor_unit;
  html += R"html(</label>
              <input type="text" id=")html";
  html += tab_id;
  html += R"html(_energy_unit" placeholder="kWh">
              <label>)html";
  html += tr.sensor_decimals;
  html += R"html(</label>
              <input type="number" id=")html";
  html += tab_id;
  html += R"html(_energy_decimals" min="0" max="6" step="1" placeholder="1">
              <label>)html";
  html += tr.sensor_value_size;
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  html += R"html(_energy_value_font">
                <option value="0">28 (Default)</option>
                <option value="1">20</option>
                <option value="2">24</option>
                <option value="3">32</option>
                <option value="4">40</option>
                <option value="5" hidden disabled>28</option>
              </select>
)html";
  if (tab_id != "screensaver") {
    html += R"html(              <label>)html";
    html += tr.popup_open;
    html += R"html(</label>
              <select id=")html";
    html += tab_id;
    html += R"html(_energy_popup_open_mode">
                <option value="0">)html";
    html += tr.long_press;
    html += R"html(</option>
                <option value="1">)html";
    html += tr.short_press;
    html += R"html(</option>
              </select>
)html";
  }
  html += R"html(              <label>)html";
  html += tr.sensor_value_y_offset;
  html += R"html(</label>
              <input type="number" id=")html";
  html += tab_id;
  html += R"html(_energy_value_y_offset" min="-100" max="200" step="1" placeholder="0">
            </div>
)html";
}
