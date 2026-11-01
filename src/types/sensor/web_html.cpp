#include "src/types/sensor/web_html.h"
#include "src/web/server/web_admin_utils.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/web/server/render/value_size_attrs.h"
#include "src/types/sensor/sensor_look.h"
#include "src/types/switch/web_html.h"

// The Sensor fields like the Switch's (mockup build/design-mockups/sensor-menu,
// user 2026-10-28): segmented choices over hidden selects instead of one long
// list. Layout (value, graph, bar, ring, the old gauge only for tiles that
// have it), then only what the layout uses: the form, a bar's orientation, a
// ring open or closed and where its value sits. The choices map onto the
// stored Tile::sensor_display_mode (sensor_look.h, admin-editor.js
// sensorModeFromChoices).
void append_sensor_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);

  html += R"html(
            <!-- Sensor Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_sensor_fields" class="type-fields">
              )html";
  appendEntityPickerField(html, tab_id, "sensor_entity", tr.sensor_entity, "sensors");
  html += "\n";
  // The value size first under Advanced (user 2026-10-28), like the
  // Switch's: one row high the half-height sizes, taller the full-size ones
  // (syncCompactValueFontOptions).
  const SwitchChoice sizes[] = {{"0", tr.sensor_value_size_default}, {"1", "20"}, {"2", "24"},
                                {"5", "28"}, {"3", "32"}, {"4", "40"}};
  String size_attrs;
  append_value_size_attrs(size_attrs, FONT_VALUE, tr.sensor_value_size_default);
  append_switch_choice(html, tab_id, "sensor_value_font", tr.sensor_value_size, sizes, 6, size_attrs);
  html += R"html(              <label>)html";
  html += tr.sensor_unit;
  html += R"html(</label>
              <input type="text" id=")html";
  html += tab_id;
  html += R"html(_sensor_unit" placeholder="°C">
                <label>)html";
  html += tr.sensor_decimals;
  html += R"html(</label>
                <input type="number" id=")html";
  html += tab_id;
  html += R"html(_sensor_decimals" min="0" max="6" step="1" placeholder="1">
)html";
  // Screensaver tiles never receive graph history (and gauges would turn the
  // compact overlay tile into a different presentation). Keep sensors there
  // as the normal value tile instead of offering controls that cannot work.
  if (tab_id != "screensaver") {
    // The stored display mode; the choices below set it.
    html += R"html(                <select class="hidden" id=")html";
    html += tab_id;
    html += R"html(_sensor_display_mode">)html";
    for (int mode = 0; mode <= sensor_look::kLastMode; ++mode) {
      html += "<option value=\"";
      html += String(mode);
      html += "\">";
      html += String(mode);
      html += "</option>";
    }
    html += "</select>\n";
    const SwitchChoice layouts[] = {{"0", tr.sensor_layout_value},
                                    {"1", tr.sensor_display_graph},
                                    {"2", tr.sensor_layout_bar},
                                    {"3", tr.sensor_layout_ring},
                                    {"4", tr.sensor_display_gauge}};
    append_switch_choice(html, tab_id, "sensor_layout", tr.switch_display, layouts, 5);
    // The first form is a bar's fill or a ring's line (syncGaugeUi).
    String form_attrs = " data-level=\"";
    appendHtmlEscaped(form_attrs, tr.sensor_form_level);
    form_attrs += "\" data-line=\"";
    appendHtmlEscaped(form_attrs, tr.sensor_form_line);
    form_attrs += '"';
    const SwitchChoice forms[] = {{"0", tr.sensor_form_level}, {"1", tr.sensor_form_stripes}};
    append_switch_choice(html, tab_id, "sensor_form", tr.sensor_form, forms, 2, form_attrs);
    const SwitchChoice orientations[] = {{"0", tr.bar_horizontal}, {"1", tr.bar_upright}};
    append_switch_choice(html, tab_id, "sensor_orientation", tr.bar_orientation, orientations, 2);
    const SwitchChoice rings[] = {{"0", tr.sensor_ring_open}, {"1", tr.sensor_ring_closed}};
    append_switch_choice(html, tab_id, "sensor_ring", tr.sensor_layout_ring, rings, 2);
    const SwitchChoice places[] = {{"0", tr.sensor_value_top}, {"1", tr.sensor_value_inside}};
    append_switch_choice(html, tab_id, "sensor_place", tr.sensor_layout_value, places, 2);
    // The range of the gauge and of every look, both fields in one row; the
    // old gauge's own shape below it.
    html += R"html(                <div id=")html";
    html += tab_id;
    html += R"html(_sensor_gauge_fields" class="gauge-fields hidden">
                  <label for=")html";
    html += tab_id;
    html += R"html(_sensor_gauge_min">)html";
    html += tr.sensor_gauge_range;
    html += R"html(</label>
                  <div class="sensor-range">
                    <input type="number" id=")html";
    html += tab_id;
    html += R"html(_sensor_gauge_min" step="any" placeholder="0" aria-label="Min">
                    <input type="number" id=")html";
    html += tab_id;
    html += R"html(_sensor_gauge_max" step="any" placeholder="100" aria-label="Max">
                  </div>
                  <div id=")html";
    html += tab_id;
    html += R"html(_sensor_gauge_shape_fields" class="gauge-shape-fields">
                  <label>)html";
    html += tr.sensor_arc_degree;
    html += R"html(</label>
                  <input type="number" id=")html";
    html += tab_id;
    html += R"html(_sensor_gauge_arc" min="90" max="359" step="1" placeholder="100">
                  <label>)html";
    html += tr.sensor_gauge_size;
    html += R"html(</label>
                  <input type="number" id=")html";
    html += tab_id;
    html += R"html(_sensor_gauge_size" min="100" max="800" step="1" placeholder="350">
                  <label>)html";
    html += tr.sensor_y_offset;
    html += R"html(</label>
                  <input type="number" id=")html";
    html += tab_id;
    html += R"html(_sensor_gauge_y_offset" min="-100" max="200" step="1" placeholder="12">
                  </div>
                </div>
                <div id=")html";
    html += tab_id;
    // The graph fills its box (sensor renderer); its old height stays stored.
    html += R"html(_sensor_graph_fields" class="graph-fields hidden">
                  <label>)html";
    html += tr.sensor_graph_height;
    html += R"html(</label>
                  <input type="number" id=")html";
    html += tab_id;
    html += R"html(_sensor_graph_height" min="20" max="200" step="1" placeholder="60">
                </div>
)html";
  }
  html += R"html(                <label for=")html";
  html += tab_id;
  html += R"html(_sensor_value_y_offset">)html";
  html += tr.sensor_value_y_offset;
  html += R"html(</label>
                <input type="number" id=")html";
  html += tab_id;
  html += R"html(_sensor_value_y_offset" min="-100" max="200" step="1" placeholder="0">
)html";
  if (tab_id != "screensaver") {
    const SwitchChoice presses[] = {{"0", tr.long_press}, {"1", tr.short_press}};
    append_switch_choice(html, tab_id, "sensor_popup_open_mode", tr.popup_open, presses, 2);
  }
  html += R"html(            </div>
)html";
}
