#include "src/types/weather/graph_web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/switch/web_html.h"
#include "src/web/server/web_admin_utils.h"

void append_weather_graph_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  const auto& loc = i18n::locale(configManager.getConfig().language);
  html += R"html(
            <div id=")html";
  html += tab_id;
  html += R"html(_weather_graph_fields" class="type-fields">
              )html";
  appendEntityPickerField(html, tab_id, "weather_graph_entity", tr.weather_entity, "weathers");
  html += "\n";
  // The two days are the pair of buttons the editor offers. The hidden select
  // keeps the stored value, so the normal save, draft and copy paths apply.
  const SwitchChoice days[] = {{"0", loc.weather_today}, {"1", loc.weather_tomorrow}};
  append_switch_choice(html, tab_id, "weather_graph_day", tr.tile_type_weather_graph, days, 2);
  html += R"html(              <input type="hidden" id=")html";
  html += tab_id;
  html += R"html(_weather_graph_pair_id" value="0">
            </div>
)html";
}
