#include "src/types/scene/web_html.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

void append_scene_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  const char* language = configManager.getConfig().language;
  html += R"html(
            <!-- Scene Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_scene_fields" class="type-fields">
              )html";
  appendEntityPickerField(html, tab_id, "scene_alias", tr.scene_label, "scenes");
  // Picture (scene_picture.h): the pictures on the panel's storage, filled in
  // by the editor (scene/admin.js), and an upload that the browser re-encodes
  // as a baseline JPEG.
  html += R"html(
              <label for=")html";
  html += tab_id;
  html += R"html(_scene_image">)html";
  html += i18n::scene_picture_label(language, 0);
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  html += R"html(_scene_image" class="scene-picture-select">
                <option value="">)html";
  html += i18n::scene_picture_label(language, 1);
  html += R"html(</option>
              </select>
              <div class="scene-picture-control">
                <button type="button" class="btn btn-secondary" id=")html";
  html += tab_id;
  html += R"html(_scene_image_upload">)html";
  html += i18n::scene_picture_label(language, 2);
  html += R"html(</button>
                <input type="file" id=")html";
  html += tab_id;
  html += R"html(_scene_image_file" accept="image/*" hidden>
                <span class="settings-note scene-picture-status" id=")html";
  html += tab_id;
  html += R"html(_scene_image_status"></span>
              </div>
              <p class="hint scene-picture-hint">)html";
  html += i18n::scene_picture_label(language, 5);
  html += R"html(</p>
            </div>
)html";
}
