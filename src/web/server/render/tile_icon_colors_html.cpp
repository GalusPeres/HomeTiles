#include "src/web/server/render/tile_icon_colors_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/config/tile_icon_colors.h"
#include "src/web/server/web_admin_utils.h"

void append_tile_icon_color_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <!-- Icon colors -->
            <div id=")html";
  html += tab_id;
  html += R"html(_tile_icon_color_fields" class="tile-icon-color-fields hidden" data-tab=")html";
  html += tab_id;
  html += R"html(">
              <div class="tile-color-label-row"><span>)html";
  appendHtmlEscaped(html, tr.tile_icon_color);
  html += R"html(</span></div>
              <div class="tile-color-row">
                <input type="color" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_color" value="#FFFFFF" data-unset="1" data-icon-color="color">
                <button type="button" class="tile-color-reset-btn" data-icon-color="clear" title=")html";
  appendHtmlEscaped(html, tr.tile_icon_color_remove);
  html += R"html("><i class="mdi mdi-restore"></i></button>
              </div>
              <div class="tile-color-label-row"><span>)html";
  appendHtmlEscaped(html, tr.tile_icon_color_rules);
  html += R"html(</span></div>
)html";
  for (size_t rule = 0; rule < tile_icon_colors::kMaxRules; ++rule) {
    const String id = tab_id + "_tile_icon_rule_" + String(rule);
    html += R"html(              <div class="tile-icon-rule hidden" id=")html";
    html += id;
    html += R"html(">
                <select id=")html";
    html += id;
    // The numeric operators are symbols and stay untranslated.
    html += R"html(_op" data-icon-color="op"><option value="ge">&ge;</option><option value="le">&le;</option><option value="eq">=</option><option value="is">)html";
    appendHtmlEscaped(html, tr.tile_icon_color_equals);
    html += R"html(</option><option value="has">)html";
    appendHtmlEscaped(html, tr.tile_icon_color_contains);
    html += R"html(</option></select>
                <input type="text" id=")html";
    html += id;
    html += R"html(_value" maxlength=")html";
    html += String(tile_icon_colors::kMaxValueBytes);
    html += R"html(" data-icon-color="value" placeholder=")html";
    appendHtmlEscaped(html, tr.tile_icon_color_value);
    html += R"html(">
                <input type="color" id=")html";
    html += id;
    html += R"html(_color" value="#F44336" data-icon-color="rule-color">
                <button type="button" class="tile-color-reset-btn" data-icon-color="remove" data-rule=")html";
    html += String(rule);
    html += R"html(" title=")html";
    appendHtmlEscaped(html, tr.tile_icon_color_remove);
    html += R"html("><i class="mdi mdi-close"></i></button>
              </div>
)html";
  }
  html += R"html(              <button type="button" class="btn btn-secondary tile-icon-rule-add" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_rule_add" data-icon-color="add"><i class="mdi mdi-plus"></i> )html";
  appendHtmlEscaped(html, tr.tile_icon_color_add_rule);
  html += R"html(</button>
              <p class="hint">)html";
  appendHtmlEscaped(html, tr.tile_icon_color_hint);
  html += R"html(</p>
            </div>
)html";
}
