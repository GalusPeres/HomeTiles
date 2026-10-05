#include "src/types/scene/web_html.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

void append_scene_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <!-- Scene Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_scene_fields" class="type-fields">
              )html";
  appendEntityPickerField(html, tab_id, "scene_alias", tr.scene_label, "scenes");
  html += R"html(
            </div>
)html";
}
