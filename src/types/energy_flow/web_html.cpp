#include "src/types/energy_flow/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/web/server/web_admin_utils.h"

namespace {

void append_checkbox(String& html, const String& tab_id, const char* field, const char* label, bool checked) {
  html += R"html(              <label class="inline-checkbox"><input type="checkbox" id=")html";
  html += tab_id;
  html += "_";
  html += field;
  html += checked ? "\" checked> " : "\"> ";
  appendHtmlEscaped(html, label);
  html += "</label>\n";
}

}  // namespace

// The node sensors in Home Assistant's order (solar, grid, battery, its
// charge, home) and the tile's options. Every field is an entity picker of
// the sensor list; an empty node is left out of the flow.
void append_energy_flow_fields_html(String& html, const String& tab_id) {
  using i18n::EnergyFlowLabel;
  const char* language = configManager.getConfig().language;
  html += R"html(
            <!-- Energy Flow Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_energy_flow_fields" class="type-fields">
)html";
  const struct {
    const char* field;
    EnergyFlowLabel label;
  } nodes[] = {{"flow_pv_entity", EnergyFlowLabel::SolarPower},
               {"flow_grid_entity", EnergyFlowLabel::GridPower},
               {"flow_battery_entity", EnergyFlowLabel::BatteryPower},
               {"flow_battery_charge_entity", EnergyFlowLabel::BatteryCharge},
               {"flow_home_entity", EnergyFlowLabel::HomePower}};
  for (const auto& node : nodes) {
    html += "              ";
    appendEntityPickerField(html, tab_id, node.field, i18n::energy_flow_label(language, node.label), "sensors");
    html += "\n";
  }
  append_checkbox(html, tab_id, "flow_grid_invert", i18n::energy_flow_label(language, EnergyFlowLabel::GridExportPositive),
                  false);
  append_checkbox(html, tab_id, "flow_battery_invert",
                  i18n::energy_flow_label(language, EnergyFlowLabel::BatteryChargePositive), false);
  append_checkbox(html, tab_id, "flow_header", i18n::energy_flow_label(language, EnergyFlowLabel::ShowHeader), false);
  append_checkbox(html, tab_id, "flow_card", i18n::energy_flow_label(language, EnergyFlowLabel::ShowCard), true);
  html += R"html(            </div>
)html";
}
