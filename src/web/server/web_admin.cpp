#include "src/web/server/web_admin.h"
#include "src/web/server/assets/web_admin_assets.h"
#include "src/web/server/assets/web_admin_fonts.h"
#include "src/web/server/web_admin_utils.h"
#include "src/web/server/auth/web_admin_auth.h"
#include "src/core/diagnostics/loop_stall.h"
#include "src/network/transport/network_transport.h"
#include "src/video/local_camera/local_camera.h"

WebAdminServer webAdminServer;

namespace {
// Routes that read and write the flash (LittleFS, NVS) hold the built-in
// camera off while they run: flash operations stall both cores, and with the
// camera streaming this tripped the interrupt watchdog during tile saves.
template <typename Handler>
auto withStorageHold(Handler handler) {
  return [handler]() {
    local_camera::ScopedStorageHold hold;
    handler();
  };
}
}  // namespace
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

  web_admin_auth::begin();
  // WebServer::stop() retains handlers. Register once so repeated Wi-Fi/AP
  // cycles do not allocate duplicate routes in internal heap.
  if (!routes_registered) {
    static const char* request_headers[] = {
        "Content-Length",
        "Content-Type",
        "X-HomeTiles-OTA-Filename",
        // Optional Web Admin password: session cookie and CSRF header.
        "Cookie",
        "X-HomeTiles-CSRF",
    };
    server.collectHeaders(request_headers,
                          sizeof(request_headers) / sizeof(request_headers[0]));
#if HOMETILES_LOOP_STALL_DIAGNOSTICS
    // Names the request whose handler runs in the loop stall diagnostics.
    server.addMiddleware([](WebServer& web, Middleware::Callback next) {
      const HTTPMethod method = web.method();
      loop_stall::webRequestBegin(method == HTTP_GET    ? "GET"
                                  : method == HTTP_POST ? "POST"
                                                        : "OTHER",
                                  web.uri().c_str());
      const bool handled = next();
      loop_stall::webRequestEnd();
      return handled;
    });
#endif

    // Every route except the login page, the login endpoints and static
    // assets passes the optional password check first. Without a password
    // the check always passes, so the routes behave exactly as before.
    auto guarded = [this](auto handler) {
      return [this, handler]() {
        if (!this->authorizeRequest()) return;
        handler();
      };
    };
    // Upload chunks arrive before the final handler runs. They are dropped
    // unless the request carries a valid session and CSRF token.
    auto guardedUpload = [this](auto handler) {
      return [this, handler]() {
        const bool first_chunk = this->server.upload().status == UPLOAD_FILE_START;
#if HOMETILES_LOOP_STALL_DIAGNOSTICS
        if (first_chunk) loop_stall::webUploadBegin(this->server.uri().c_str());
#endif
        if (!this->authorizeUploadChunk(first_chunk)) {
          return;
        }
        handler();
      };
    };
    auto guardedRaw = [this](auto handler) {
      return [this, handler]() {
        const bool first_chunk = this->server.raw().status == RAW_START;
#if HOMETILES_LOOP_STALL_DIAGNOSTICS
        if (first_chunk) loop_stall::webUploadBegin(this->server.uri().c_str());
#endif
        if (!this->authorizeUploadChunk(first_chunk)) {
          return;
        }
        handler();
      };
    };
    auto guardedUploadDone = [this](auto handler) {
      return [this, handler]() {
        this->finishUploadRequest();
        if (!this->authorizeRequest()) return;
        handler();
      };
    };

    server.on("/", [this]() { this->handleRoot(); });
    server.on("/assets/inter-4.1-regular.woff2", HTTP_GET,
              [this]() { sendWebFontRegular(this->server); });
    server.on("/assets/inter-4.1-semibold.woff2", HTTP_GET,
              [this]() { sendWebFontSemibold(this->server); });
    // The stylesheet and the login script are needed by the login page and
    // hold no device data.
    server.on(adminCssAssetPath(), HTTP_GET,
              [this]() { sendAdminCssAsset(this->server); });
    server.on(authJsAssetPath(), HTTP_GET,
              [this]() { sendAuthJsAsset(this->server); });
    server.on(adminJsAssetPath(), HTTP_GET,
              guarded([this]() { sendAdminJsAsset(this->server); }));
    server.on("/api/auth/challenge", HTTP_GET,
              [this]() { this->handleAuthChallenge(); });
    server.on("/api/auth/login", HTTP_POST,
              [this]() { this->handleAuthLogin(); });
    server.on("/api/auth/logout", HTTP_POST,
              [this]() { this->handleAuthLogout(); });
    server.on("/api/auth/password", HTTP_POST,
              withStorageHold([this]() { this->handleAuthPassword(); }));
    server.on("/mqtt", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleSaveMQTT(); })));
    server.on("/status", guarded([this]() { this->handleStatus(); }));
    server.on("/bridge_refresh", HTTP_POST,
              guarded([this]() { this->handleBridgeRefresh(); }));
    server.on("/bridge", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleSaveBridge(); })));
    server.on("/restart", HTTP_POST,
              guarded([this]() { this->handleRestart(); }));
    // No password check: the handler accepts the Bridge address only within
    // two minutes after Pair was pressed on the panel itself.
    server.on("/api/link", HTTP_POST,
              withStorageHold([this]() { this->handleLinkSetup(); }));
    server.on("/api/status", guarded([this]() { this->handleStatus(); }));
    server.on("/api/tiles", HTTP_GET,
              guarded([this]() { this->handleGetTiles(); }));
    server.on("/api/tiles", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleSaveTiles(); })));
    server.on("/api/tiles/reorder", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleReorderTiles(); })));
    server.on("/api/folders", HTTP_GET,
              guarded([this]() { this->handleGetFolders(); }));
    server.on("/api/folders/tab", HTTP_GET,
              guarded([this]() { this->handleGetFolderTab(); }));
    server.on("/api/folders/access", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleSaveFolderAccess(); })));
    server.on("/api/folders/delete", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleDeleteFolder(); })));
    server.on("/api/sensor_values", HTTP_GET,
              guarded([this]() { this->handleGetSensorValues(); }));
    server.on("/api/entity_options", HTTP_GET,
              guarded([this]() { this->handleGetEntityOptions(); }));
    server.on("/api/mdi_icons", HTTP_GET,
              guarded([this]() { this->handleGetMdiIcons(); }));
    server.on("/api/entity_search", HTTP_POST,
              guarded([this]() { this->handleStartEntitySearch(); }));
    server.on("/api/entity_search", HTTP_GET,
              guarded([this]() { this->handleGetEntitySearch(); }));
    server.on("/api/screensaver", HTTP_GET,
              guarded([this]() { this->handleGetScreensaver(); }));
    server.on("/api/screensaver", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleSaveScreensaver(); })));
    server.on("/api/hardware-io", HTTP_GET,
              guarded([this]() { this->handleGetHardwareIo(); }));
    server.on("/api/hardware-io", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleSaveHardwareIo(); })));
    server.on("/api/display/tile-radius", HTTP_GET,
              guarded([this]() { handleTileRadius(); }));
    server.on("/api/display/tile-radius", HTTP_POST, guarded([this]() {
      // The live preview (preview=1) only changes the style; saving writes NVS.
      local_camera::ScopedStorageHold hold(server.arg("preview") != "1");
      handleTileRadius();
    }));
    server.on("/api/display/tile-borders", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleSaveTileBorders(); })));
    server.on("/api/display/icon-discs", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleSaveIconDiscs(); })));
    server.on("/api/display/icon-glow", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleSaveIconGlow(); })));
    server.on("/api/display/tile-color", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleSaveDefaultTileColor(); })));
    server.on("/api/local-camera", HTTP_GET,
              guarded([this]() { this->handleLocalCamera(); }));
    server.on("/api/local-camera", HTTP_POST,
              guarded([this]() { this->handleLocalCamera(); }));
    server.on("/api/screensaver/wallpaper", HTTP_GET,
              guarded([this]() { this->handleGetScreensaverWallpaper(); }));
    server.on("/api/sd_images", HTTP_GET,
              guarded([this]() { this->handleGetSdImages(); }));
    server.on("/api/sd_icons", HTTP_GET,
              guarded([this]() { this->handleGetSdIcons(); }));
    server.on("/api/screenshot", HTTP_POST,
              guarded([this]() { this->handleCreateScreenshot(); }));
    server.on("/api/screenshot/download", HTTP_GET,
              guarded([this]() { this->handleDownloadScreenshot(); }));
    server.on("/api/ota/prepare", HTTP_POST,
              guarded([this]() { this->handlePrepareOtaUpload(); }));
    server.on(
        "/api/ota/upload", HTTP_POST,
        guardedUploadDone([this]() { this->handleOtaUploadDone(); }),
        guardedUpload([this]() { this->handleOtaUpdate(); }));
    server.on(
        "/api/ota/upload/raw", HTTP_POST,
        guardedUploadDone([this]() { this->handleOtaUploadDone(); }),
        guardedRaw([this]() { this->handleOtaRawUpdate(); }));
    server.on("/api/ota/install", HTTP_POST,
              guarded([this]() { this->handleStartOtaInstall(); }));
    server.on("/api/ota/status", HTTP_GET,
              guarded([this]() { this->handleGetOtaStatus(); }));
    server.on("/api/ota/github/check", HTTP_POST,
              guarded([this]() { this->handleGithubUpdateCheck(); }));
    server.on("/api/ota/github/install", HTTP_POST,
              guarded([this]() { this->handleGithubUpdateInstall(); }));
    server.on("/api/ota/github/status", HTTP_GET,
              guarded([this]() { this->handleGetGithubUpdateStatus(); }));
    server.on(
        "/api/upload_icon", HTTP_POST,
        guardedUploadDone([this]() { this->handleUploadIconDone(); }),
        guardedUpload([this]() { this->handleUploadIcon(); }));
    server.on("/api/files/list", HTTP_GET,
              guarded([this]() { this->handleFileManagerList(); }));
    server.on("/api/files/download", HTTP_GET,
              guarded([this]() { this->handleFileManagerDownload(); }));
    server.on("/api/files/delete", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleFileManagerDelete(); })));
    server.on("/api/files/rename", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleFileManagerRename(); })));
    server.on("/api/files/mkdir", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleFileManagerMkdir(); })));
    server.on(
        "/api/files/upload", HTTP_POST,
        guardedUploadDone([this]() { this->handleFileManagerUploadDone(); }),
        guardedUpload([this]() { this->handleFileManagerUpload(); }));
    server.on("/api/coredump", HTTP_GET,
              guarded([this]() { this->handleCoreDumpDownload(); }));
    server.on("/api/coredump/erase", HTTP_POST,
              guarded(withStorageHold([this]() { this->handleCoreDumpErase(); })));
    server.on("/api/crashlog", HTTP_GET,
              guarded([this]() { this->handleCrashLogDownload(); }));
    server.on("/api/sd-diagnostics", HTTP_GET,
              guarded([this]() { this->handleSdDiagnosticsDownload(); }));
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
  loop_stall::webIdle();
  webAdminServiceOta();
}

void WebAdminServer::handleRoot() {
  webAdminMarkActivity();
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  if (web_admin_auth::enabled()) {
    const String cookie = server.header("Cookie");
    if (web_admin_auth::checkRequest(cookie.c_str(), nullptr, false) !=
        web_admin_auth::AccessResult::Allowed) {
      sendChunkedResponse(server, 200, "text/html; charset=utf-8", getLoginPage());
      return;
    }
  }
  sendChunkedResponse(server, 200, "text/html; charset=utf-8", getAdminPage());
  webAdminMarkActivity();
}
