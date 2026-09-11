#include "src/web/server/web_admin.h"
#include "src/web/server/assets/web_admin_assets.h"
#include "src/web/server/assets/web_admin_fonts.h"
#include "src/web/server/web_admin_utils.h"
#include "src/network/transport/network_transport.h"

WebAdminServer webAdminServer;
static volatile uint32_t g_web_admin_last_activity_ms = 0;

WebAdminServer::WebAdminServer()
    : server(80), running(false), routes_registered(false),
      github_check_callback(nullptr), github_install_callback(nullptr),
      last_github_check(), github_check_valid(false),
      github_install_requested(false) {}

void WebAdminServer::setGithubUpdateCallbacks(web_github_check_callback_t check_cb,
                                               web_github_install_callback_t install_cb) {
  github_check_callback = check_cb;
  github_install_callback = install_cb;
}

void WebAdminServer::setGithubUpdateInstallFailed(const char* error) {
  if (!github_install_requested) return;
  github_install_error = error ? error : "Update failed";
}

void webAdminMarkActivity() {
  g_web_admin_last_activity_ms = millis();
}

bool webAdminRecentlyActive(uint32_t quiet_ms) {
  const uint32_t last = g_web_admin_last_activity_ms;
  if (last == 0) return false;
  return static_cast<uint32_t>(millis() - last) < quiet_ms;
}

bool WebAdminServer::start() {
  if (running) {
    Serial.println("[WebAdmin] Server already running");
    return true;
  }
  if (!networkTransport.isConnected()) {
    Serial.println("[WebAdmin] Start aborted - no network connection");
    return false;
  }

  // WebServer::stop() retains handlers. Register once so repeated Wi-Fi/AP
  // cycles do not allocate duplicate routes in internal heap.
  if (!routes_registered) {
    static const char* request_headers[] = {
        "Content-Length",
        "Content-Type",
        "X-HomeTiles-OTA-Filename",
    };
    server.collectHeaders(request_headers,
                          sizeof(request_headers) / sizeof(request_headers[0]));

    server.on("/", [this]() { this->handleRoot(); });
    server.on("/assets/inter-4.1-regular.woff2", HTTP_GET,
              [this]() { sendWebFontRegular(this->server); });
    server.on("/assets/inter-4.1-semibold.woff2", HTTP_GET,
              [this]() { sendWebFontSemibold(this->server); });
    server.on(adminCssAssetPath(), HTTP_GET,
              [this]() { sendAdminCssAsset(this->server); });
    server.on(adminJsAssetPath(), HTTP_GET,
              [this]() { sendAdminJsAsset(this->server); });
    server.on("/mqtt", HTTP_POST, [this]() { this->handleSaveMQTT(); });
    server.on("/status", [this]() { this->handleStatus(); });
    server.on("/bridge_refresh", HTTP_POST,
              [this]() { this->handleBridgeRefresh(); });
    server.on("/bridge", HTTP_POST, [this]() { this->handleSaveBridge(); });
    server.on("/restart", HTTP_POST, [this]() { this->handleRestart(); });
    server.on("/api/status", [this]() { this->handleStatus(); });
    server.on("/api/tiles", HTTP_GET, [this]() { this->handleGetTiles(); });
    server.on("/api/tiles", HTTP_POST, [this]() { this->handleSaveTiles(); });
    server.on("/api/tiles/reorder", HTTP_POST,
              [this]() { this->handleReorderTiles(); });
    server.on("/api/folders", HTTP_GET, [this]() { this->handleGetFolders(); });
    server.on("/api/folders/tab", HTTP_GET,
              [this]() { this->handleGetFolderTab(); });
    server.on("/api/folders/access", HTTP_POST,
              [this]() { this->handleSaveFolderAccess(); });
    server.on("/api/folders/delete", HTTP_POST,
              [this]() { this->handleDeleteFolder(); });
    server.on("/api/sensor_values", HTTP_GET,
              [this]() { this->handleGetSensorValues(); });
    server.on("/api/entity_options", HTTP_GET,
              [this]() { this->handleGetEntityOptions(); });
    server.on("/api/screensaver", HTTP_GET,
              [this]() { this->handleGetScreensaver(); });
    server.on("/api/screensaver", HTTP_POST,
              [this]() { this->handleSaveScreensaver(); });
    server.on("/api/hardware-io", HTTP_GET,
              [this]() { this->handleGetHardwareIo(); });
    server.on("/api/hardware-io", HTTP_POST,
              [this]() { this->handleSaveHardwareIo(); });
    server.on("/api/display/tile-radius", HTTP_GET, [this]() { handleTileRadius(); });
  server.on("/api/display/tile-radius", HTTP_POST, [this]() { handleTileRadius(); });
  server.on("/api/display/tile-borders", HTTP_POST,
              [this]() { this->handleSaveTileBorders(); });
    server.on("/api/screensaver/wallpaper", HTTP_GET,
              [this]() { this->handleGetScreensaverWallpaper(); });
    server.on("/api/sd_images", HTTP_GET,
              [this]() { this->handleGetSdImages(); });
    server.on("/api/sd_icons", HTTP_GET,
              [this]() { this->handleGetSdIcons(); });
    server.on("/api/screenshot", HTTP_POST,
              [this]() { this->handleCreateScreenshot(); });
    server.on("/api/screenshot/download", HTTP_GET,
              [this]() { this->handleDownloadScreenshot(); });
    server.on("/api/ota/prepare", HTTP_POST,
              [this]() { this->handlePrepareOtaUpload(); });
    server.on(
        "/api/ota/upload", HTTP_POST, [this]() { this->handleOtaUploadDone(); },
        [this]() { this->handleOtaUpdate(); });
    server.on(
        "/api/ota/upload/raw", HTTP_POST,
        [this]() { this->handleOtaUploadDone(); },
        [this]() { this->handleOtaRawUpdate(); });
    server.on("/api/ota/install", HTTP_POST,
              [this]() { this->handleStartOtaInstall(); });
    server.on("/api/ota/status", HTTP_GET,
              [this]() { this->handleGetOtaStatus(); });
    server.on("/api/ota/github/check", HTTP_POST,
              [this]() { this->handleGithubUpdateCheck(); });
    server.on("/api/ota/github/install", HTTP_POST,
              [this]() { this->handleGithubUpdateInstall(); });
    server.on("/api/ota/github/status", HTTP_GET,
              [this]() { this->handleGetGithubUpdateStatus(); });
    server.on(
        "/api/upload_icon", HTTP_POST,
        [this]() { this->handleUploadIconDone(); },
        [this]() { this->handleUploadIcon(); });
    server.on("/api/files/list", HTTP_GET,
              [this]() { this->handleFileManagerList(); });
    server.on("/api/files/download", HTTP_GET,
              [this]() { this->handleFileManagerDownload(); });
    server.on("/api/files/delete", HTTP_POST,
              [this]() { this->handleFileManagerDelete(); });
    server.on("/api/files/rename", HTTP_POST,
              [this]() { this->handleFileManagerRename(); });
    server.on("/api/files/mkdir", HTTP_POST,
              [this]() { this->handleFileManagerMkdir(); });
    server.on(
        "/api/files/upload", HTTP_POST,
        [this]() { this->handleFileManagerUploadDone(); },
        [this]() { this->handleFileManagerUpload(); });
    server.on("/api/coredump", HTTP_GET,
              [this]() { this->handleCoreDumpDownload(); });
    server.on("/api/coredump/erase", HTTP_POST,
              [this]() { this->handleCoreDumpErase(); });
    server.on("/api/crashlog", HTTP_GET,
              [this]() { this->handleCrashLogDownload(); });
    server.on("/api/sd-diagnostics", HTTP_GET,
              [this]() { this->handleSdDiagnosticsDownload(); });
    routes_registered = true;
  }

  server.begin();
  running = true;
  IPAddress ip = networkTransport.localIP();
  Serial.printf("[WebAdmin] Available at http://%s\n", ip.toString().c_str());
  return true;
}

void WebAdminServer::stop() {
  if (!running) return;
  server.stop();
  running = false;
  Serial.println("[WebAdmin] Server stopped");
}

void WebAdminServer::handle() {
  if (!running) return;
  server.handleClient();
  webAdminServiceOta();
}

void WebAdminServer::handleRoot() {
  webAdminMarkActivity();
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  sendChunkedResponse(server, 200, "text/html; charset=utf-8", getAdminPage());
  webAdminMarkActivity();
}
