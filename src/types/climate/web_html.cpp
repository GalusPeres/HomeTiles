#include "src/types/climate/web_html.h"
#include "src/types/switch/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/config/tile_config.h"
#include "src/web/server/web_admin_utils.h"

void append_climate_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  const char* language = configManager.getConfig().language;
  html += "<div id=\"";
  html += tab_id;
  html += "_climate_fields\" class=\"type-fields\">";
  appendEntityPickerField(html, tab_id, "climate_entity",
                          i18n::climate_entity_label(language), "climates");
  html += "<label>";
  html += tr.popup_open;
  html += "</label><select id=\"";
  html += tab_id;
  html += "_climate_popup_open_mode\"><option value=\"1\">";
  html += tr.short_press;
  html += "</option><option value=\"0\">";
  html += tr.long_press;
  html += "</option></select>";
  // Layout like the Switch tile (climateTileShowsValue, sensor_display_mode).
  const SwitchChoice layouts[] = {{"0", tr.climate_layout_title_only}, {"1", tr.climate_layout_with_value}};
  append_switch_choice(html, tab_id, "climate_view", tr.switch_display, layouts, 2);

  html += "<div class=\"climate-content-config hidden\"><input type=\"hidden\" id=\"";
  html += tab_id;
  html += "_climate_geometry\" value=\"\"><div id=\"";
  html += tab_id;
  html += "_climate_editor_stash\" class=\"climate-editor-stash\"><div id=\"";
  html += tab_id;
  html += "_climate_editor_shell\" class=\"climate-mini-editor-shell\"><div id=\"";
  html += tab_id;
  html += "_climate_content_grid\" class=\"climate-content-grid\">";
  for (uint8_t cell = 0; cell < CLIMATE_TILE_MAX_GRID_CELLS; ++cell) {
    html += "<button type=\"button\" id=\"";
    html += tab_id;
    html += "_climate_cell_";
    html += String(cell);
    html += "\" class=\"climate-mini-cell\" data-climate-cell=\"";
    html += String(cell);
    html += "\" aria-label=\"";
    appendHtmlEscaped(
        html, i18n::climate_mini_label(language, 3));
    html += "\"></button>";
  }
  for (uint8_t slot = 0; slot < CLIMATE_TILE_MAX_CONTENT_SLOTS; ++slot) {
    html += "<div id=\"";
    html += tab_id;
    html += "_climate_slot_row_";
    html += String(slot);
    html += "\" class=\"climate-mini-tile hidden\" data-climate-item=\"";
    html += String(slot);
    html += "\"><div id=\"";
    html += tab_id;
    html += "_climate_preview_";
    html += String(slot);
    html += "\" class=\"climate-mini-preview\"></div>";
    html += "<span class=\"tile-resize-handle tile-resize-handle-e\" data-climate-resize=\"e\"></span>";
    html += "<span class=\"tile-resize-handle tile-resize-handle-s\" data-climate-resize=\"s\"></span>";
    html += "<span class=\"tile-resize-handle tile-resize-handle-se\" data-climate-resize=\"se\"></span>";
    html += "</div><select id=\"";
    html += tab_id;
    html += "_climate_slot_";
    html += String(slot);
    html += "\" class=\"climate-slot-storage\" tabindex=\"-1\" aria-hidden=\"true\">";

    auto append_option = [&](uint8_t value,
                             const String& label) {
      html += "<option value=\"";
      html += String(value);
      html += "\">";
      appendHtmlEscaped(html, label);
      html += "</option>";
    };
    append_option(CLIMATE_TILE_CONTENT_AUTO,
                  i18n::climate_mini_label(language, 0));
    append_option(CLIMATE_TILE_CONTENT_EMPTY,
                  i18n::climate_mini_label(language, 1));
    append_option(CLIMATE_TILE_CONTENT_CURRENT_TEMPERATURE,
                  i18n::climate_value_label(language, 0));
    append_option(CLIMATE_TILE_CONTENT_CURRENT_HUMIDITY,
                  i18n::climate_value_label(language, 2));
    append_option(CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE,
                  i18n::climate_target_temperature_label(language));
    append_option(CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE_LOW,
                  i18n::climate_heating_target_label(language));
    append_option(CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE_HIGH,
                  i18n::climate_cooling_target_label(language));
    append_option(CLIMATE_TILE_CONTENT_TARGET_HUMIDITY,
                  i18n::climate_target_humidity_label(language));
    append_option(CLIMATE_TILE_CONTENT_HVAC_MODE,
                  i18n::climate_control_label(language, 0));
    html += "</select><select id=\"";
    html += tab_id;
    html += "_climate_layout_";
    html += String(slot);
    html += "\" class=\"climate-layout-storage\" tabindex=\"-1\" aria-hidden=\"true\"><option value=\"0\">";
    html += i18n::climate_mini_label(language, 0);
    html += "</option><option value=\"1\">";
    html += i18n::climate_mini_label(language, 5);
    html += "</option><option value=\"2\">";
    html += i18n::climate_mini_label(language, 6);
    html += "</option></select>";
  }
  html += "</div></div></div><div id=\"";
  html += tab_id;
  html += "_climate_selected_fields\" class=\"climate-selected-fields\"><label for=\"";
  html += tab_id;
  html += "_climate_selected_content\">";
  html += i18n::climate_mini_label(language, 4);
  html += "</label><select id=\"";
  html += tab_id;
  html += "_climate_selected_content\">";
  auto append_selected_option = [&](uint8_t value,
                                    const String& label) {
    html += "<option value=\"";
    html += String(value);
    html += "\">";
    appendHtmlEscaped(html, label);
    html += "</option>";
  };
  append_selected_option(CLIMATE_TILE_CONTENT_AUTO,
                         i18n::climate_mini_label(language, 0));
  append_selected_option(CLIMATE_TILE_CONTENT_EMPTY,
                         i18n::climate_mini_label(language, 2));
  append_selected_option(CLIMATE_TILE_CONTENT_CURRENT_TEMPERATURE,
                         i18n::climate_value_label(language, 0));
  append_selected_option(CLIMATE_TILE_CONTENT_CURRENT_HUMIDITY,
                         i18n::climate_value_label(language, 2));
  append_selected_option(CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE,
                         i18n::climate_target_temperature_label(language));
  append_selected_option(CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE_LOW,
                         i18n::climate_heating_target_label(language));
  append_selected_option(CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE_HIGH,
                         i18n::climate_cooling_target_label(language));
  append_selected_option(CLIMATE_TILE_CONTENT_TARGET_HUMIDITY,
                         i18n::climate_target_humidity_label(language));
  append_selected_option(CLIMATE_TILE_CONTENT_HVAC_MODE,
                         i18n::climate_control_label(language, 0));
  html += "</select></div></div>";
  html += "</div>\n";
}
