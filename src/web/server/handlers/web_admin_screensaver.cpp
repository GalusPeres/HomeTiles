#include "src/web/server/web_admin.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/screensaver/screensaver_config.h"
#include "src/web/server/web_admin_utils.h"
#include <vector>
#include "src/web/server/handlers/web_admin_handler_utils.h"

using namespace web_admin_handlers;

void WebAdminServer::handleGetScreensaver() {
  webAdminMarkActivity();
  String json = screensaverConfig.toJson(true);
  if (!json.endsWith("}")) {
    server.send(500, "application/json", "{\"success\":false}");
    return;
  }
  json.remove(json.length() - 1);
  // sd_ready tells the Web Admin that available_wallpapers is the complete
  // card listing, so entries of deleted files can be dropped from the list.
  const bool sd_ready = Device::sdReady();
  json += ",\"success\":true,\"sd_ready\":";
  json += sd_ready ? "true" : "false";
  json += ",\"available_wallpapers\":[";
  bool first = true;
  if (sd_ready) {
    std::vector<String> names;
    const char* directories[] = {"/images", "/wallpapers"};
    for (const char* directory : directories) {
      fs::File root = Device::sdFS().open(directory, FILE_READ);
      if (!root) continue;
      for (fs::File entry = root.openNextFile(); entry;
           entry = root.openNextFile()) {
        if (!entry.isDirectory()) {
          String name = entry.name();
          const int slash = max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
          if (slash >= 0) name = name.substring(slash + 1);
          if (endsWithIgnoreCase(name, ".jpg") ||
              endsWithIgnoreCase(name, ".jpeg")) {
            bool duplicate = false;
            for (const auto& existing : names) {
              if (existing.equalsIgnoreCase(name)) {
                duplicate = true;
                break;
              }
            }
            if (!duplicate) names.push_back(name);
          }
        }
        entry.close();
      }
      root.close();
    }
    for (const auto& name : names) {
      if (!first) json += ',';
      first = false;
      json += '"';
      appendJsonEscaped(json, name);
      json += '"';
    }
  }
  json += "]}";
  sendChunkedResponse(server, 200, "application/json", json);
}

void WebAdminServer::handleSaveScreensaver() {
  webAdminMarkActivity();
  const String payload = server.arg("plain");
  String preview_wallpaper;
  String error;
  const ScreensaverConfigData& before = screensaverConfig.get();
  const bool had_picture = before.use_wallpapers && screensaver_uses_ha_picture(before);
  const String old_entity = had_picture ? before.picture_entity : String();
  const uint8_t old_fit = before.picture_fit;
  if (!screensaverConfig.replaceFromJson(payload, error, &preview_wallpaper)) {
    String json = "{\"success\":false,\"error\":\"";
    appendJsonEscaped(json, error);
    json += "\"}";
    server.send(400, "application/json", json);
    return;
  }
  // A new Home Assistant picture source or placement changes the
  // subscriptions and what the panel tells the Bridge it uses
  // (docs-dev/images.md). A camera's interval follows when it is shown.
  const ScreensaverConfigData& after = screensaverConfig.get();
  const bool has_picture = after.use_wallpapers && screensaver_uses_ha_picture(after);
  if (had_picture != has_picture ||
      (has_picture && (old_entity != after.picture_entity || old_fit != after.picture_fit))) {
    mqttRequestDynamicSlotsReload(1000);
  }
  image_screensaver_config_changed(preview_wallpaper);
  server.send(200, "application/json", "{\"success\":true}");
}

void WebAdminServer::handleGetScreensaverWallpaper() {
  webAdminMarkActivity();
  if (!Device::sdReady() || !server.hasArg("name")) {
    server.send(404, "text/plain", "Wallpaper unavailable");
    return;
  }
  String name = server.arg("name");
  name.trim();
  if (!name.length() || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 ||
      name.indexOf("..") >= 0 ||
      (!endsWithIgnoreCase(name, ".jpg") &&
       !endsWithIgnoreCase(name, ".jpeg"))) {
    server.send(400, "text/plain", "Invalid wallpaper");
    return;
  }
  String path = String("/images/") + name;
  if (!Device::sdFS().exists(path)) path = String("/wallpapers/") + name;
  fs::File file = Device::sdFS().open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    server.send(404, "text/plain", "Wallpaper not found");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(file, "image/jpeg");
  file.close();
}
