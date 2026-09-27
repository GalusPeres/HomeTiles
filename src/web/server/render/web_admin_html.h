#ifndef WEB_ADMIN_HTML_H
#define WEB_ADMIN_HTML_H

#include <Arduino.h>
#include <WebServer.h>

namespace i18n {
struct Strings;
}

bool buildAdminFolderTabFragments(uint16_t folder_id, String& button_html, String& tab_html, String& tab_id);

// Optional Web Admin password (web_admin_security_html.cpp): the settings
// section and the per-session CSRF meta tag of the admin page.
void appendWebAdminPasswordSettingsHtml(String& html, const i18n::Strings& tr);
void appendWebAdminCsrfMeta(String& html, WebServer& server);
// Writes the value of a password input: the stored secret, or nothing plus a
// placeholder while a Web Admin password hides stored secrets. Call it right
// after value=" and close the attribute with ".
void appendStoredSecretValue(String& html, const char* secret,
                             const i18n::Strings& tr);

// Forward declaration to avoid circular dependency
class WebAdminServer;

// HTML generation methods for WebAdminServer class
// These are implemented as methods of WebAdminServer and generate:
// - Main admin page with tab navigation (Network, Home, Game Controls)
// - Success pages for various operations
// - Status JSON response

// Note: All HTML generation implementations are in web_admin_html.cpp
// and are methods of the WebAdminServer class defined in web_admin.h

#endif // WEB_ADMIN_HTML_H
