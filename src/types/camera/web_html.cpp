#include "src/types/camera/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/web/server/web_admin_utils.h"

void append_camera_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <div id=")html";
  html += tab_id;
  html += R"html(_camera_fields" class="type-fields">
              )html";
  appendEntityPickerField(html, tab_id, "camera_entity", tr.camera_entity, "cameras");
  html += R"html(
            </div>
)html";
}
