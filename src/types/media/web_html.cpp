#include "src/types/media/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
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
  html += R"html(
            </div>
)html";
}
