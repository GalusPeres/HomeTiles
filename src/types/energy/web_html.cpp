#include "src/types/energy/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/switch/web_html.h"
#include "src/web/server/render/value_size_attrs.h"
#include "src/web/server/web_admin_utils.h"

// The Energy fields like the Sensor's (types/sensor/web_html.cpp, user
// 2026-10-29): segmented choices over hidden selects, the value size first
// under Advanced, then unit and decimals, the display (the value alone or
// today's hours as bars, renderer.cpp), the value's offset and the popup's
// press last. The selects keep their ids, so admin.js, drafts, copy/paste and
// import read them as before.
void append_energy_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);

  html += R"html(
            <!-- Energy Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_energy_fields" class="type-fields">
              )html";
  appendEntityPickerField(html, tab_id, "energy_entity", tr.energy_entity, "energy");
  html += "\n";
  // One row high the half-height sizes, taller the full-size ones
  // (syncCompactValueFontOptions), like the Sensor's.
  const SwitchChoice sizes[] = {{"0", tr.sensor_value_size_default}, {"1", "20"}, {"2", "24"},
                                {"5", "28"}, {"3", "32"}, {"4", "40"}};
  String size_attrs;
  append_value_size_attrs(size_attrs, FONT_VALUE, tr.sensor_value_size_default);
  append_switch_choice(html, tab_id, "energy_value_font", tr.sensor_value_size, sizes, 6, size_attrs);
  html += R"html(              <label>)html";
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
)html";
  // The screensaver shows the value alone (renderer.cpp).
  if (tab_id != "screensaver") {
    const SwitchChoice displays[] = {{"0", tr.sensor_layout_value}, {"2", tr.sensor_display_graph}};
    append_switch_choice(html, tab_id, "energy_display_mode", tr.switch_display, displays, 2);
  }
  html += R"html(                <label for=")html";
  html += tab_id;
  html += R"html(_energy_value_y_offset">)html";
  html += tr.sensor_value_y_offset;
  html += R"html(</label>
                <input type="number" id=")html";
  html += tab_id;
  html += R"html(_energy_value_y_offset" min="-100" max="200" step="1" placeholder="0">
)html";
  if (tab_id != "screensaver") {
    const SwitchChoice presses[] = {{"0", tr.long_press}, {"1", tr.short_press}};
    append_switch_choice(html, tab_id, "energy_popup_open_mode", tr.popup_open, presses, 2);
  }
  html += R"html(            </div>
)html";
}
