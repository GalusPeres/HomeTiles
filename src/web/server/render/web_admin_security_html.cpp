#include "src/web/server/web_admin.h"
#include "src/web/server/render/web_admin_html.h"
#include "src/web/server/assets/web_admin_assets.h"
#include "src/web/server/render/web_admin_styles.h"
#include "src/web/server/auth/web_admin_auth.h"
#include "src/web/server/web_admin_utils.h"
#include "src/core/i18n/i18n.h"
#include "src/devices/device.h"

namespace {

void appendAttribute(String& html, const char* name, const char* value) {
  html += ' ';
  html += name;
  html += "=\"";
  appendHtmlEscaped(html, String(value ? value : ""));
  html += '"';
}

}  // namespace

// Shown instead of the admin page while a Web Admin password is set and the
// browser has no valid session. It carries no device configuration; the
// visual base is the admin card with its brand header.
String WebAdminServer::getLoginPage() {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  String html;
  html.reserve(4096);
  html += "<!DOCTYPE html>\n<html lang=\"";
  html += tr.html_lang;
  html += R"html(">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta name="robots" content="noindex">
  <title>HomeTiles - )html";
  appendHtmlEscaped(html, String(tr.web_auth_login_title));
  html += "</title>\n";
  appendAdminStyles(html);
  html += "  <script defer src=\"";
  html += authJsAssetPath();
  html += R"html("></script>
</head>
<body>
  <div class="wrapper login-wrapper">
    <div class="card">
      <div class="brand">
        <svg width="44" height="44" viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
          <rect x="4" y="4" width="17" height="17" rx="4" fill="#ffffff"/>
          <rect x="27" y="4" width="17" height="17" rx="4" fill="#ffffff"/>
          <rect x="4" y="27" width="17" height="17" rx="4" fill="#ffffff"/>
          <path d="M33 26h5v6.5h6.5v5H38V44h-5v-6.5h-6.5v-5H33z" fill="#26a69a"/>
        </svg>
        <div>
          <h1>HomeTiles )html";
  appendHtmlEscaped(html, String(tr.admin_panel_word));
  html += R"html(</h1>
          <div class="device">)html";
  appendHtmlEscaped(html, String(Device::displayName()));
  html += R"html(</div>
        </div>
      </div>
      <form id="ht_login_form" class="login-form")html";
  appendAttribute(html, "data-checking", tr.web_auth_checking);
  appendAttribute(html, "data-invalid", tr.web_auth_wrong_password);
  appendAttribute(html, "data-locked", tr.web_auth_wait_fmt);
  appendAttribute(html, "data-failed", tr.web_auth_login_failed);
  html += R"html(>
        <div class="section-title">)html";
  appendHtmlEscaped(html, String(tr.web_auth_login_title));
  html += R"html(</div>
        <div>
          <label for="ht_login_password">)html";
  appendHtmlEscaped(html, String(tr.web_auth_password_label));
  html += R"html(</label>
          <input type="password" id="ht_login_password" autocomplete="current-password" required autofocus>
        </div>
        <button id="ht_login_submit" class="btn btn-go" type="submit">)html";
  appendHtmlEscaped(html, String(tr.web_auth_login_button));
  html += R"html(</button>
        <div id="ht_login_message" class="login-message" role="status" aria-live="polite"></div>
        <div class="settings-note">)html";
  appendHtmlEscaped(html, String(tr.web_auth_forgot));
  html += R"html(</div>
      </form>
    </div>
  </div>
</body>
</html>
)html";
  return html;
}

// Settings section inside the admin page. The password inputs have no name
// attribute, so the surrounding /mqtt form never submits them; the browser
// sends only the derived salt/key pair to /api/auth/password.
void appendWebAdminPasswordSettingsHtml(String& html, const i18n::Strings& tr) {
  const bool enabled = web_admin_auth::enabled();
  html += R"html(
          <div class="settings-section" id="web_auth_section" data-enabled=")html";
  html += enabled ? "1" : "0";
  html += R"html(">
            <div class="section-title">)html";
  appendHtmlEscaped(html, String(tr.web_auth_section));
  html += R"html(</div>
            <div class="settings-note web-auth-status" id="web_auth_status">)html";
  appendHtmlEscaped(html, String(enabled ? tr.web_auth_status_on
                                         : tr.web_auth_status_off));
  html += R"html(</div>
            <div class="settings-grid">
              <div>
                <label for="web_auth_password">)html";
  appendHtmlEscaped(html, String(tr.web_auth_new_password));
  html += R"html(</label>
                <input type="password" id="web_auth_password" autocomplete="new-password" minlength="8">
              </div>
              <div>
                <label for="web_auth_password_repeat">)html";
  appendHtmlEscaped(html, String(tr.web_auth_repeat_password));
  html += R"html(</label>
                <input type="password" id="web_auth_password_repeat" autocomplete="new-password" minlength="8">
              </div>
            </div>
            <div class="settings-actions web-auth-actions">
              <button type="button" class="btn btn-go" id="web_auth_set">)html";
  appendHtmlEscaped(html, String(tr.web_auth_set));
  html += R"html(</button>
              <button type="button" class="btn btn-secondary)html";
  if (!enabled) html += " is-hidden";
  html += R"html(" id="web_auth_remove">)html";
  appendHtmlEscaped(html, String(tr.web_auth_remove));
  html += R"html(</button>
              <button type="button" class="btn btn-secondary)html";
  if (!enabled) html += " is-hidden";
  html += R"html(" id="web_auth_logout">)html";
  appendHtmlEscaped(html, String(tr.web_auth_logout));
  html += R"html(</button>
            </div>
            <div class="settings-note">)html";
  appendHtmlEscaped(html, String(tr.web_auth_note));
  html += R"html(</div>
          </div>
)html";
}

void appendWebAdminCsrfMeta(String& html, WebServer& server) {
  char csrf[web_admin_auth::kTokenHexSize];
  const String cookie = server.header("Cookie");
  if (!web_admin_auth::csrfForCookie(cookie.c_str(), csrf)) return;
  html += "  <meta name=\"hometiles-csrf\" content=\"";
  html += csrf;
  html += "\">\n";
  ht_crypto::secureZero(csrf, sizeof(csrf));
}
