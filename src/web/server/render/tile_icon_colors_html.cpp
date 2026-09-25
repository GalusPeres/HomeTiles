#include "src/web/server/render/tile_icon_colors_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/config/tile_icon_colors.h"
#include "src/web/server/web_admin_utils.h"

namespace {

void append_label_row(String& html, const char* text) {
  html += R"html(              <div class="tile-color-label-row"><span>)html";
  appendHtmlEscaped(html, text);
  html += "</span></div>\n";
}

// Button with a translated label and a data-icon-color role.
void append_button(String& html, const char* css, const char* role, const char* attribute,
                   const char* value, const char* text) {
  html += R"html(<button type="button" class=")html";
  html += css;
  html += R"html(" data-icon-color=")html";
  html += role;
  html += "\" ";
  html += attribute;
  html += "=\"";
  html += value;
  html += "\">";
  appendHtmlEscaped(html, text);
  html += "</button>";
}

}  // namespace

void append_tile_icon_color_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <!-- Icon colors -->
            <div id=")html";
  html += tab_id;
  html += R"html(_tile_icon_color_fields" class="tile-icon-color-fields hidden" data-tab=")html";
  html += tab_id;
  html += "\">\n";
  append_label_row(html, tr.tile_icon_color);
  html += R"html(              <div class="tile-color-row">
                <input type="color" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_color" value="#FFFFFF" data-unset="1" data-icon-color="color">
                <button type="button" class="tile-color-reset-btn" data-icon-color="clear" title=")html";
  appendHtmlEscaped(html, tr.tile_icon_color_remove);
  html += R"html("><i class="mdi mdi-restore"></i></button>
              </div>
)html";

  // Color bar for numeric states.
  html += R"html(              <div class="icon-color-section hidden" id=")html";
  html += tab_id;
  html += "_tile_icon_bar_section\">\n";
  append_label_row(html, tr.tile_icon_color_by_value);
  html += R"html(                <input type="hidden" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_bar" value="">
                <div class="icon-color-segmented" role="group">)html";
  append_button(html, "", "mode", "data-mode", "off", tr.tile_icon_color_bar_off);
  append_button(html, "", "mode", "data-mode", "smooth", tr.tile_icon_color_smooth);
  append_button(html, "", "mode", "data-mode", "steps", tr.tile_icon_color_steps);
  html += R"html(</div>
                <div class="icon-color-bar-editor hidden" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_bar_editor">
                  <div class="icon-color-presets">)html";
  append_button(html, "icon-color-chip", "preset", "data-preset", "cold_warm", tr.tile_icon_color_preset_cold_warm);
  append_button(html, "icon-color-chip", "preset", "data-preset", "traffic", tr.tile_icon_color_preset_traffic);
  append_button(html, "icon-color-chip", "preset", "data-preset", "battery", tr.tile_icon_color_preset_battery);
  append_button(html, "icon-color-chip", "preset", "data-preset", "humidity", tr.tile_icon_color_preset_humidity);
  append_button(html, "icon-color-chip", "preset", "data-preset", "single", tr.tile_icon_color_preset_single);
  html += R"html(</div>
                  <div class="icon-color-bar" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_bar_strip" data-icon-color="bar"></div>
                  <div class="icon-color-handles" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_bar_handles" data-selected="-1"><button type="button" class="icon-color-stop-remove hidden" data-icon-color="stop-remove" title=")html";
  appendHtmlEscaped(html, tr.tile_icon_color_remove);
  html += R"html("><i class="mdi mdi-close"></i></button></div>
                  <input type="color" class="icon-color-stop-picker" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_stop_color" data-icon-color="stop-color" tabindex="-1" aria-hidden="true">
                  <div class="icon-color-range">
                    <label>)html";
  appendHtmlEscaped(html, tr.tile_icon_color_min);
  html += R"html(<input type="text" inputmode="decimal" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_bar_min" maxlength=")html";
  html += String(tile_icon_colors::kMaxNumberBytes);
  html += R"html(" data-icon-color="min"></label>
                    <label>)html";
  appendHtmlEscaped(html, tr.tile_icon_color_max);
  html += R"html(<input type="text" inputmode="decimal" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_bar_max" maxlength=")html";
  html += String(tile_icon_colors::kMaxNumberBytes);
  html += R"html(" data-icon-color="max"></label>
                  </div>
                  <p class="hint">)html";
  appendHtmlEscaped(html, tr.tile_icon_color_bar_hint);
  html += R"html(</p>
                </div>
              </div>
)html";

  // State colors for text states.
  html += R"html(              <div class="icon-color-section hidden" id=")html";
  html += tab_id;
  html += "_tile_icon_state_section\">\n";
  append_label_row(html, tr.tile_icon_color_by_state);
  html += R"html(                <datalist id=")html";
  html += tab_id;
  html += "_tile_icon_states\"></datalist>\n";
  for (size_t row = 0; row < tile_icon_colors::kMaxRows; ++row) {
    const String id = tab_id + "_tile_icon_rule_" + String(row);
    html += R"html(                <div class="tile-icon-rule hidden" id=")html";
    html += id;
    html += R"html(">
                  <input type="text" id=")html";
    html += id;
    html += R"html(_value" list=")html";
    html += tab_id;
    html += R"html(_tile_icon_states" maxlength=")html";
    html += String(tile_icon_colors::kMaxValueBytes);
    html += R"html(" data-icon-color="value" placeholder=")html";
    appendHtmlEscaped(html, tr.tile_icon_color_state);
    html += R"html(">
                  <label class="icon-color-contains"><input type="checkbox" id=")html";
    html += id;
    html += R"html(_has" data-icon-color="has"> )html";
    appendHtmlEscaped(html, tr.tile_icon_color_contains);
    html += R"html(</label>
                  <input type="color" id=")html";
    html += id;
    html += R"html(_color" value="#22C55E" data-icon-color="rule-color">
                  <button type="button" class="tile-color-reset-btn" data-icon-color="remove" data-rule=")html";
    html += String(row);
    html += R"html(" title=")html";
    appendHtmlEscaped(html, tr.tile_icon_color_remove);
    html += R"html("><i class="mdi mdi-close"></i></button>
                </div>
)html";
  }
  html += R"html(                <button type="button" class="btn btn-secondary tile-icon-rule-add" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_rule_add" data-icon-color="add"><i class="mdi mdi-plus"></i> )html";
  appendHtmlEscaped(html, tr.tile_icon_color_add_state);
  html += R"html(</button>
                <p class="hint">)html";
  appendHtmlEscaped(html, tr.tile_icon_color_state_hint);
  html += R"html(</p>
              </div>
)html";

  // Binary sensor: one color for On and one for Off, stored as state lines.
  html += R"html(              <div class="icon-color-section hidden" id=")html";
  html += tab_id;
  html += "_tile_icon_binary_section\">\n";
  const struct {
    const char* state;
    const char* label;
    const char* color;
  } binary_rows[] = {{"on", tr.tile_icon_color_state_on, "#FFC107"},
                     {"off", tr.tile_icon_color_state_off, "#9E9E9E"}};
  for (const auto& row : binary_rows) {
    append_label_row(html, row.label);
    html += R"html(                <div class="tile-color-row">
                  <input type="color" id=")html";
    html += tab_id;
    html += "_tile_icon_";
    html += row.state;
    html += "\" value=\"";
    html += row.color;
    html += "\" data-default=\"";
    html += row.color;
    html += R"html(" data-unset="1" data-icon-color="binary">
                  <button type="button" class="tile-color-reset-btn" data-icon-color="binary-clear" data-state=")html";
    html += row.state;
    html += R"html(" title=")html";
    appendHtmlEscaped(html, tr.tile_icon_color_remove);
    html += R"html("><i class="mdi mdi-restore"></i></button>
                </div>
)html";
  }
  html += R"html(              </div>
            </div>
)html";
}
