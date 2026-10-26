#include "src/types/timer/web_scripts.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

namespace {

void append_timer_js_string(String& html, const char* value) {
  html += "'";
  for (const char* c = value ? value : ""; *c; ++c) {
    switch (*c) {
      case '\\': html += "\\\\"; break;
      case '\'': html += "\\'"; break;
      case '\r': break;
      case '\n': html += "\\n"; break;
      case '<': html += "\\x3c"; break;
      default: html += *c; break;
    }
  }
  html += "'";
}

}  // namespace

// The Timer tile's preview texts (types/timer/admin.js) from the central
// translations; the editor's JavaScript lives in src/web/assets/admin.js.
void append_timer_scripts(String& html) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += "\n  <script>\n  const TIMER_I18N = Object.freeze({\n";
  auto append_i18n = [&](const char* key, const char* value) {
    html += "    ";
    html += key;
    html += ": ";
    append_timer_js_string(html, value);
    html += ",\n";
  };
  append_i18n("defaultName", tr.timer_default_name);
  append_i18n("tapStart", tr.timer_tap_start);
  append_i18n("none", tr.timer_none);
  append_i18n("minutes", tr.timer_minutes);
  html += "  });\n  </script>\n";
}
