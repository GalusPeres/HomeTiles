#include "src/types/timer/web_html.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

void append_timer_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <!-- Timer Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_timer_tile_fields" class="type-fields">
              <div class="settings-note">)html";
  html += tr.timer_tile_note;
  html += R"html(</div>
            </div>
)html";
}
