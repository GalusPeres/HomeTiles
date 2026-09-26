#include "src/types/energy/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/types/energy/energy_data.h"
#include "src/web/server/web_admin_utils.h"

namespace {

bool label_already_has_unit_suffix(const String& name, const String& unit) {
  String trimmed_name = name;
  trimmed_name.trim();
  String trimmed_unit = unit;
  trimmed_unit.trim();
  if (!trimmed_name.length() || !trimmed_unit.length()) return false;

  String suffix = "(" + trimmed_unit + ")";
  trimmed_name.toLowerCase();
  suffix.toLowerCase();
  return trimmed_name.endsWith(suffix);
}

}  // namespace

void append_energy_fields_html(String& html,
                               const String& tab_id,
                               const std::vector<String>& energyOptions) {
  const auto& tr = i18n::strings(configManager.getConfig().language);

  html += R"html(
            <!-- Energy Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_energy_fields" class="type-fields">
              <label>)html";
  html += tr.energy_entity;
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  html += R"html(_energy_entity">
                <option value="">)html";
  html += tr.no_selection;
  html += R"html(</option>
)html";

  for (const auto& opt : energyOptions) {
    html += "<option value=\"";
    appendHtmlEscaped(html, opt);
    html += "\">";
    String name = haBridgeConfig.findSensorName(opt);
    if (!name.length()) {
      name = humanizeIdentifier(opt, true);
    }
    String unit = haBridgeConfig.findSensorUnit(opt);
    if (!unit.length()) unit = energy_find_cached_unit(opt);
    String label = name;
    if (unit.length() && !label_already_has_unit_suffix(label, unit)) {
      label += " (";
      label += unit;
      label += ")";
    }
    label += " - ";
    label += opt;
    appendHtmlEscaped(html, label);
    html += "</option>";
  }

  html += R"html(
              </select>
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
