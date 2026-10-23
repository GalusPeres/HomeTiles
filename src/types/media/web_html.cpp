#include "src/types/media/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/switch/web_html.h"
#include "src/web/server/web_admin_utils.h"

void append_media_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);

  html += R"html(
            <!-- Media Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_media_fields" class="type-fields">
              )html";
  appendEntityPickerField(html, tab_id, "media_entity", tr.media_entity, "media");
  // The popup's layout like the Climate tile's (media_popup_layout,
  // sensor_display_mode): the cover left (0) or centred (1).
  const SwitchChoice layouts[] = {{"0", tr.media_layout_cover_left}, {"1", tr.media_layout_cover_center}};
  append_switch_choice(html, tab_id, "media_layout", tr.media_popup_layout, layouts, 2);
  html += R"html(
            </div>
)html";
}
