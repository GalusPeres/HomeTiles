#ifndef WEB_ADMIN_ASSETS_H
#define WEB_ADMIN_ASSETS_H

#include <Arduino.h>
#include <WebServer.h>

// Static admin UI assets are edited in src/web/assets and generated into
// deterministic gzip byte arrays by tools/generate-web-assets.mjs.
const char* adminCssAssetPath();
const char* adminJsAssetPath();
// Login and password helpers; public so the login page can load it.
const char* authJsAssetPath();

void sendAdminCssAsset(WebServer& server);
void sendAdminJsAsset(WebServer& server);
void sendAuthJsAsset(WebServer& server);

#endif  // WEB_ADMIN_ASSETS_H
