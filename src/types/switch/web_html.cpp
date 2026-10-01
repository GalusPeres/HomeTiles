#include "src/types/switch/web_html.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

namespace {

struct SwitchChoice {
  const char* value;
  const char* label;
};

// One choice like Tile color: the label on its own row, then one line of
// segmented buttons (setSwitchChoice in types/switch/admin.js). The hidden
// select keeps the value, so loading, saving, drafts and previews use the
// same field as before.
void append_switch_choice(String& html, const String& tab_id, const char* field, const char* label,
                          const SwitchChoice* choices, size_t count) {
  html += R"html(              <label class="switch-choice-label" for=")html";
  html += tab_id;
  html += "_";
  html += field;
  html += "\">";
  appendHtmlEscaped(html, label);
  html += "</label>\n              <select class=\"hidden\" id=\"";
  html += tab_id;
  html += "_";
  html += field;
  html += "\">";
  for (size_t i = 0; i < count; ++i) {
    html += "<option value=\"";
    html += choices[i].value;
    html += "\">";
    appendHtmlEscaped(html, choices[i].label);
    html += "</option>";
  }
  html += "</select>\n              <div class=\"icon-color-segmented switch-choices\" role=\"group\" id=\"";
  html += tab_id;
  html += "_";
  html += field;
  html += "_choices\">";
  for (size_t i = 0; i < count; ++i) {
    html += "<button type=\"button\" data-value=\"";
    html += choices[i].value;
    html += "\" onclick=\"setSwitchChoice('";
    html += tab_id;
    html += "', '";
    html += field;
    html += "', '";
    html += choices[i].value;
    html += "')\">";
    appendHtmlEscaped(html, choices[i].label);
    html += "</button>";
  }
  html += "</div>\n";
}

}  // namespace

void append_switch_fields_html(String& html, const String& tab_id, const std::vector<String>& switchOptions) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <!-- Switch Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_switch_fields" class="type-fields">
              <label>)html";
  html += tr.switch_light;
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  html += R"html(_switch_entity">
                <option value="">)html";
  html += tr.no_selection;
  html += R"html(</option>
)html";

  for (const auto& opt : switchOptions) {
    html += "<option value=\"";
    appendHtmlEscaped(html, opt);
    html += "\">";
    String label = humanizeIdentifier(opt, true) + " - " + opt;
    appendHtmlEscaped(html, label);
    html += "</option>";
  }

  html += R"html(
              </select>
)html";
  // Values are switch_layout::Layout; Automatic first, the default of new
  // tiles.
  const SwitchChoice layouts[] = {{"3", tr.switch_layout_automatic},
                                  {"2", tr.switch_layout_dimmer},
                                  {"1", tr.switch_layout_switch},
                                  {"0", tr.switch_icon_button}};
  append_switch_choice(html, tab_id, "switch_style", tr.switch_display, layouts, 4);
  // The state line takes the half-height Sensor value sizes
  // (compact_sensor_layout::value_font): Default (title size), 24, 28.
  const SwitchChoice sizes[] = {{"0", tr.sensor_value_size_default}, {"2", "24"}, {"5", "28"}};
  append_switch_choice(html, tab_id, "switch_value_font", tr.sensor_value_size, sizes, 3);
  if (tab_id != "screensaver") {
    const SwitchChoice presses[] = {{"0", tr.long_press}, {"1", tr.short_press}};
    append_switch_choice(html, tab_id, "switch_popup_open_mode", tr.popup_open, presses, 2);
  }
  html += R"html(            </div>
)html";
}
