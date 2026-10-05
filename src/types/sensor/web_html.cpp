#include "src/types/sensor/web_html.h"
#include "src/web/server/web_admin_utils.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

void append_sensor_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);

  html += R"html(
            <!-- Sensor Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_sensor_fields" class="type-fields">
              )html";
  appendEntityPickerField(html, tab_id, "sensor_entity", tr.sensor_entity, "sensors");
  html += R"html(
              <label>)html";
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
                <label>)html";
  html += tr.sensor_value_size;
  html += R"html(</label>
                <select id=")html";
  html += tab_id;
  html += R"html(_sensor_value_font">
                  <option value="0">28 (Default)</option>
                  <option value="1">20</option>
                  <option value="2">24</option>
                  <option value="3">32</option>
                  <option value="4">40</option>
                  <option value="5" hidden disabled>28</option>
                </select>
)html";
  // Screensaver tiles never receive graph history (and gauges would turn the
  // compact overlay tile into a different presentation). Keep sensors there
  // as the normal value tile instead of offering controls that cannot work.
  if (tab_id != "screensaver") {
    html += R"html(                <label>)html";
  html += tr.sensor_display_mode;
  html += R"html(</label>
                <select id=")html";
  html += tab_id;
  html += R"html(_sensor_display_mode">
                  <option value="0">)html";
  html += tr.sensor_display_none;
  html += R"html(</option>
                  <option value="1">)html";
  html += tr.sensor_display_gauge;
  html += R"html(</option>
                  <option value="2">)html";
  html += tr.sensor_display_graph;
  html += R"html(</option>
                </select>
                <div id=")html";
  html += tab_id;
  html += R"html(_sensor_gauge_fields" class="gauge-fields hidden">
                  <label>)html";
  html += tr.sensor_gauge_min;
  html += R"html(</label>
                  <input type="number" id=")html";
  html += tab_id;
  html += R"html(_sensor_gauge_min" step="1" placeholder="0">
                  <label>)html";
  html += tr.sensor_gauge_max;
  html += R"html(</label>
                  <input type="number" id=")html";
  html += tab_id;
  html += R"html(_sensor_gauge_max" step="1" placeholder="100">
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
                <div id=")html";
  html += tab_id;
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
  if (tab_id != "screensaver") {
    html += R"html(                <label>)html";
    html += tr.popup_open;
    html += R"html(</label>
                <select id=")html";
    html += tab_id;
    html += R"html(_sensor_popup_open_mode">
                  <option value="0">)html";
    html += tr.long_press;
    html += R"html(</option>
                  <option value="1">)html";
    html += tr.short_press;
    html += R"html(</option>
                </select>
)html";
  }
  html += R"html(                <label>)html";
  html += tr.sensor_value_y_offset;
  html += R"html(</label>
                <input type="number" id=")html";
  html += tab_id;
  html += R"html(_sensor_value_y_offset" min="-100" max="200" step="1" placeholder="0">
            </div>
)html";
}
