#include "src/types/energy_flow/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/web/server/web_admin_utils.h"

namespace {

void append_checkbox(String& html, const String& tab_id, const char* field, const char* label, bool checked) {
  html += R"html(                <label class="inline-checkbox"><input type="checkbox" id=")html";
  html += tab_id;
  html += "_";
  html += field;
  html += checked ? "\" checked> " : "\"> ";
  appendHtmlEscaped(html, label);
  html += "</label>\n";
}

// A group the editor shows elsewhere (data-tile-place; type-selection.js
// placeTileTypeParts): under Title and Icon, at the top of the settings or
// in the Color group.
void open_place(String& html, const char* place) {
  html += R"html(              <div class="tile-place" data-tile-place=")html";
  html += place;
  html += "\">\n";
}

void close_place(String& html) { html += "              </div>\n"; }

}  // namespace

// The node sensors in Home Assistant's order (solar, grid, battery, its
// charge, home) at the top of the settings, each sign option right under its
// sensor; "Show title and icon" under Title and Icon, the card in Color (user
// 2026-10-26). Every field is an entity picker of the sensor list; an empty
// node is left out of the flow. Without an entity of its own the tile's Auto
// title is the type's name and its icon the header's lightning bolt.
void append_energy_flow_fields_html(String& html, const String& tab_id) {
  using i18n::EnergyFlowLabel;
  const char* language = configManager.getConfig().language;
  html += R"html(
            <!-- Energy Flow Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_energy_flow_fields" class="type-fields" data-auto-title=")html";
  appendHtmlEscaped(html, i18n::energy_flow_label(language, EnergyFlowLabel::Type));
  html += R"html(" data-auto-icon="lightning-bolt">
)html";
  open_place(html, "head");
  append_checkbox(html, tab_id, "flow_header", i18n::energy_flow_label(language, EnergyFlowLabel::ShowHeader), false);
  close_place(html);
  const struct {
    const char* field;
    EnergyFlowLabel label;
    const char* invert;
  } nodes[] = {{"flow_pv_entity", EnergyFlowLabel::SolarPower, nullptr},
               {"flow_grid_entity", EnergyFlowLabel::GridPower, "flow_grid_invert"},
               {"flow_battery_entity", EnergyFlowLabel::BatteryPower, "flow_battery_invert"},
               {"flow_battery_charge_entity", EnergyFlowLabel::BatteryCharge, nullptr},
               {"flow_home_entity", EnergyFlowLabel::HomePower, nullptr}};
  open_place(html, "top");
  for (const auto& node : nodes) {
    html += "                ";
    appendEntityPickerField(html, tab_id, node.field, i18n::energy_flow_label(language, node.label), "sensors");
    html += "\n";
    if (node.invert) {
      append_checkbox(html, tab_id, node.invert, i18n::energy_flow_label(language, EnergyFlowLabel::InvertSign), false);
    }
  }
  close_place(html);
  open_place(html, "color");
  append_checkbox(html, tab_id, "flow_card", i18n::energy_flow_label(language, EnergyFlowLabel::ShowCard), true);
  close_place(html);
  html += R"html(            </div>
)html";
}
