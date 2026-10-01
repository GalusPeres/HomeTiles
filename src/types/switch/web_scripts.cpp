#include "src/types/switch/web_scripts.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

// The editable JavaScript lives in src/web/assets/admin.js and is served as
// one precompressed, cacheable firmware asset. Only the state labels of the
// preview come from here, in the configured language, the same texts the
// device shows (types/switch/renderer.cpp show_state_text).
namespace {

void append_switch_js_string(String& html, const String& value) {
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

void append_switch_scripts(String& html) {
  html += R"html(
  <script>
)html";

  const char* language = configManager.getConfig().language;
  const auto& tr = i18n::strings(language);
  html += "  const SWITCH_I18N = Object.freeze({\n";
  auto append_i18n = [&](const char* key, const String& value) {
    html += "    ";
    html += key;
    html += ": ";
    append_switch_js_string(html, value);
    html += ",\n";
  };
  append_i18n("on", tr.light_on);
  append_i18n("off", tr.light_off);
  append_i18n("unavailable", i18n::entity_state_label(language, "unavailable"));
  append_i18n("unknown", i18n::entity_state_label(language, "unknown"));
  html += "  });\n  </script>\n";
}
