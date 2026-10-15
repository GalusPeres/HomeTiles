#include "src/types/scene/web_scripts.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

namespace {

void append_scene_js_string(String& html, const String& value) {
  html += "'";
  for (size_t index = 0; index < value.length(); ++index) {
    const char c = value[index];
    switch (c) {
      case '\\': html += "\\\\"; break;
      case '\'': html += "\\'"; break;
      case '\r': break;
      case '\n': html += "\\n"; break;
      case '<': html += "\\x3c"; break;
      default: html += c; break;
    }
  }
  html += "'";
}

}  // namespace

// The editable JavaScript lives in src/web/assets/admin.js and is served as
// one precompressed, cacheable firmware asset; only the picture texts of the
// panel's language come with the page (scene/admin.js).
void append_scene_scripts(String& html) {
  const char* language = configManager.getConfig().language;
  html += R"html(
  <script>
  const SCENE_I18N = Object.freeze({
)html";
  const char* const keys[] = {"pictureLabel", "noPicture", "uploadButton", "uploadingStatus", "uploadFailed"};
  for (uint8_t index = 0; index < 5; ++index) {
    html += "    ";
    html += keys[index];
    html += ": ";
    append_scene_js_string(html, i18n::scene_picture_label(language, index));
    html += ",\n";
  }
  html += R"html(  });
  </script>
)html";
}
