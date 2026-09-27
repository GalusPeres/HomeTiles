#include "src/types/settings/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/web/server/web_admin_utils.h"
#include "src/web/server/auth/web_admin_auth.h"

void append_settings_access_fields_html(String& html, const String& tab_id) {
  const DeviceConfig& cfg = configManager.getConfig();
  const auto& tr = i18n::strings(cfg.language);
  const String prefix = tab_id + "_";
  String stored_pin;
  // A Web Admin password hides stored PINs; the status line still reports
  // whether one is configured.
  if (!web_admin_auth::storedSecretsHidden()) {
    configManager.getSettingsPin(stored_pin);
  }

  html += "<div id=\"" + prefix +
          "settings_access_fields\" class=\"type-fields "
          "settings-access-tile-fields\">";
  html += "<div class=\"settings-access-toggles\">";

  html += "<label class=\"folder-pin-toggle\"><input type=\"checkbox\" id=\"" +
          prefix + "settings_pin_enabled\" data-pin-configured=\"" +
          String(cfg.settings_pin_enabled ? "1" : "0") + "\"";
  if (cfg.settings_pin_enabled) html += " checked";
  html += "><span>" + String(tr.settings_lock_enable) + "</span></label>";

  html += "<label class=\"folder-pin-toggle\"><input type=\"checkbox\" id=\"" +
          prefix + "settings_tile_hidden\"";
  if (cfg.settings_tile_hidden) html += " checked";
  html += "><span>" + String(tr.settings_hide_tile) + "</span></label>";

  html += "<label class=\"folder-pin-toggle\"><input type=\"checkbox\" id=\"" +
          prefix + "settings_swipe_enabled\"";
  if (cfg.settings_swipe_enabled) html += " checked";
  html += "><span>" + String(tr.settings_swipe_enable) + "</span></label>";
  html += "</div>";

  html += "<div class=\"settings-access-columns\">";
  html += "<div id=\"" + prefix +
          "settings_pin_fields\" class=\"settings-access-field settings-pin-fields\">";
  html += "<label for=\"" + prefix + "settings_pin\">" +
          String(tr.settings_pin_label) + "</label>";
  html += "<div class=\"settings-pin-control\"><div class=\"password-field\">";
  html += "<input type=\"password\" id=\"" + prefix +
          "settings_pin\" inputmode=\"numeric\" autocomplete=\"new-password\" "
          "maxlength=\"8\" placeholder=\"" +
          String(tr.settings_pin_placeholder) + "\" value=\"";
  appendHtmlEscaped(html, stored_pin);
  html += "\">";
  html += "<button type=\"button\" class=\"password-toggle\" data-label-show=\"" +
          String(tr.password_show) + "\" data-label-hide=\"" +
          String(tr.password_hide) + "\" onclick=\"togglePasswordVisibility('" +
          prefix + "settings_pin', this)\">" + String(tr.password_show) + "</button>";
  html += "</div><button type=\"button\" class=\"btn btn-secondary settings-pin-apply\" id=\"" +
          prefix + "settings_pin_apply\">" + String(tr.folder_pin_apply) + "</button></div>";
  html += "<div class=\"settings-note\" id=\"" + prefix +
          "settings_pin_status\" data-configured-text=\"" +
          String(tr.settings_pin_configured) + "\" data-not-configured-text=\"" +
          String(tr.settings_pin_not_configured) + "\">" +
          String(cfg.settings_pin_enabled ? tr.settings_pin_configured
                                          : tr.settings_pin_not_configured) +
          "</div></div>";

  html += "<div id=\"" + prefix +
          "settings_reveal_edge_fields\" class=\"settings-access-field settings-edge-fields\">";
  html += "<label for=\"" + prefix + "settings_reveal_edge\">" +
          String(tr.settings_reveal_edge) + "</label><select id=\"" + prefix +
          "settings_reveal_edge\">";
  const char* edge_labels[] = {tr.edge_left, tr.edge_right, tr.edge_top,
                               tr.edge_bottom};
  for (uint8_t edge = 0; edge < 4; ++edge) {
    html += "<option value=\"" + String(edge) + "\"";
    if (cfg.settings_reveal_edge == edge) html += " selected";
    html += ">" + String(edge_labels[edge]) + "</option>";
  }
  html += "</select><div class=\"settings-note\">" +
          String(tr.settings_swipe_note) + "</div></div></div>";

  html += "<div class=\"settings-note settings-factory-pin\"><strong>" +
          String(tr.factory_pin_label) + ": " + String(pin_access::kRecoveryPin) +
          "</strong><br>" + String(tr.factory_pin_note) + "</div></div>";
  stored_pin = "";
}
