#include "src/types/navigate/web_html.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

void append_navigate_fields_html(String& html, const String& tab_id, const String& navigateOptionsHtml) {
  (void)navigateOptionsHtml;
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <!-- Navigate Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_navigate_fields" class="type-fields folder-pin-fields is-hidden">
              <label class="folder-pin-toggle">
                <input type="checkbox" id=")html";
  html += tab_id;
  html += R"html(_folder_pin_enabled">
                <span>)html";
  html += tr.folder_pin_enable;
  html += R"html(</span>
              </label>
              <label class="folder-pin-label is-hidden" for=")html";
  html += tab_id;
  html += R"html(_folder_pin">)html";
  html += tr.folder_pin_label;
  html += R"html(</label>
              <div class="folder-pin-control">
                <div class="password-field folder-pin-password">
                  <input type="password" id=")html";
  html += tab_id;
  html += R"html(_folder_pin" inputmode="numeric" pattern="[0-9]{4,8}"
                       maxlength="8" autocomplete="new-password" placeholder=")html";
  html += tr.folder_pin_placeholder;
  html += R"html(">
                  <button type="button" class="password-toggle" data-label-show=")html";
  html += tr.password_show;
  html += R"html(" data-label-hide=")html";
  html += tr.password_hide;
  html += R"html(" onclick="togglePasswordVisibility(')html";
  html += tab_id;
  html += R"html(_folder_pin', this)">)html";
  html += tr.password_show;
  html += R"html(</button>
                </div>
                <button type="button" class="btn btn-secondary" id=")html";
  html += tab_id;
  html += R"html(_folder_pin_apply">)html";
  html += tr.folder_pin_apply;
  html += R"html(</button>
              </div>
              <span class="settings-note folder-pin-status" id=")html";
  html += tab_id;
  html += R"html(_folder_pin_status"></span>
            </div>
)html";
}

// Back tile: the Clock/Text per-tile border checkbox.
void append_back_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <!-- Back Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_back_fields" class="type-fields">
              <label class="inline-checkbox"><input type="checkbox" id=")html";
  html += tab_id;
  html += R"html(_back_tile_border" checked>)html";
  html += tr.screensaver_tile_border;
  html += R"html(</label>
            </div>
)html";
}
