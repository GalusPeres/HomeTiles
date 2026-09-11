#include "src/web/server/web_admin.h"
#include "src/core/i18n/i18n.h"
#include "src/core/config/pin_access.h"
#include "src/network/bridge/device_entities.h"
#include "src/network/network_manager.h"
#include "src/network/transport/network_transport.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/ui/tabs/settings/tab_settings.h"
#include "src/tiles/config/tile_config.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/ui/ui_manager.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/types/clock/clock_format.h"
#include "src/web/server/web_admin_utils.h"
#include <stdlib.h>
#include <string.h>
#include "src/web/server/handlers/web_admin_handler_utils.h"

using namespace web_admin_handlers;

void WebAdminServer::handleSaveMQTT() {
  const bool ajax_save =
      server.hasArg("_ajax") && server.arg("_ajax") == "1";
  const bool access_only =
      server.hasArg("_access_only") && server.arg("_access_only") == "1";
  DeviceConfig cfg{};
  if (configManager.isConfigured()) {
    cfg = configManager.getConfig();
  } else {
    cfg.mqtt_port = 1883;
    strncpy(cfg.mqtt_base_topic, "hometiles", CONFIG_MQTT_BASE_MAX - 1);
    strncpy(cfg.ha_prefix, "ha/statestream", CONFIG_HA_PREFIX_MAX - 1);
    cfg.tile_borders = true;
    cfg.tile_radius = tile_radius::kMinimum;
  }
  const DeviceConfig previous_cfg = cfg;

  auto sendSaveError = [this, ajax_save](int status, const char* message) {
    if (ajax_save) {
      String json = "{\"ok\":false,\"error\":\"";
      appendJsonEscaped(json, String(message ? message : ""));
      json += "\"}";
      server.send(status, "application/json", json);
    } else {
      server.send(status, "text/html",
                  String("<h1>") + (message ? message : "") + "</h1>");
    }
  };

  auto copyIfNonEmpty = [this](char* dest, size_t max_len, const char* field) {
    if (!server.hasArg(field)) return;
    String value = server.arg(field);
    value.trim();
    if (!value.length()) return;
    copyToBuffer(dest, max_len, value);
  };

  auto copySharedIpField =
      [this](char* dest, size_t max_len, const char* shared_field,
             const char* wifi_legacy_field, const char* ethernet_legacy_field) {
        const char* selected = nullptr;
        if (server.hasArg(shared_field)) selected = shared_field;
        else if (server.hasArg(wifi_legacy_field)) selected = wifi_legacy_field;
        else if (server.hasArg(ethernet_legacy_field)) selected = ethernet_legacy_field;
        if (!selected) return;
        String value = server.arg(selected);
        value.trim();
        copyToBuffer(dest, max_len, value);
      };

  const bool use_static =
      access_only
          ? cfg.wifi_static_enabled
          : server.hasArg("network_use_static") ||
                server.hasArg("wifi_use_static") ||
                server.hasArg("ethernet_use_static") ||
                (server.hasArg("network_ip_mode") &&
                 server.arg("network_ip_mode").equalsIgnoreCase("static"));

  if (server.hasArg("mqtt_host")) {
    copyToBuffer(cfg.mqtt_host, sizeof(cfg.mqtt_host), server.arg("mqtt_host"));
  }
  copyIfNonEmpty(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), "wifi_ssid");
  copyIfNonEmpty(cfg.wifi_pass, sizeof(cfg.wifi_pass), "wifi_pass");
  cfg.wifi_static_enabled = use_static;
  if (use_static) {
    copySharedIpField(cfg.wifi_static_ip, sizeof(cfg.wifi_static_ip),
                      "network_static_ip", "wifi_static_ip",
                      "ethernet_static_ip");
    copySharedIpField(cfg.wifi_gateway, sizeof(cfg.wifi_gateway),
                      "network_gateway", "wifi_gateway",
                      "ethernet_gateway");
    copySharedIpField(cfg.wifi_subnet, sizeof(cfg.wifi_subnet),
                      "network_subnet", "wifi_subnet",
                      "ethernet_subnet");
    copySharedIpField(cfg.wifi_dns, sizeof(cfg.wifi_dns),
                      "network_dns", "wifi_dns", "ethernet_dns");
  }
  if (NetworkTransportManager::deviceSupportsEthernet()) {
    if (server.hasArg("network_mode")) {
      cfg.ethernet_enabled =
          server.arg("network_mode").equalsIgnoreCase("ethernet");
    }
  }
  if (server.hasArg("mqtt_port")) {
    cfg.mqtt_port = server.arg("mqtt_port").toInt();
  }
  if (server.hasArg("mqtt_user")) {
    copyToBuffer(cfg.mqtt_user, sizeof(cfg.mqtt_user), server.arg("mqtt_user"));
  }
  copyIfNonEmpty(cfg.mqtt_pass, sizeof(cfg.mqtt_pass), "mqtt_pass");
  if (server.hasArg("mqtt_client_id")) {
    String client_id = server.arg("mqtt_client_id");
    client_id.trim();
    copyToBuffer(cfg.mqtt_client_id, sizeof(cfg.mqtt_client_id), client_id);
  }
  if (server.hasArg("mqtt_base")) {
    String base = server.arg("mqtt_base");
    base.trim();
    while (base.endsWith("/")) base.remove(base.length() - 1);
    if (base.isEmpty()) base = "hometiles";
    copyToBuffer(cfg.mqtt_base_topic, sizeof(cfg.mqtt_base_topic), base);
  }
  if (server.hasArg("ha_prefix")) {
    String prefix = server.arg("ha_prefix");
    prefix.trim();
    while (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
    if (prefix.isEmpty()) prefix = "ha/statestream";
    copyToBuffer(cfg.ha_prefix, sizeof(cfg.ha_prefix), prefix);
  }
  if (server.hasArg("language")) {
    String language = server.arg("language");
    language.trim();
    strncpy(cfg.language, i18n::normalize_language_code(language.c_str()), sizeof(cfg.language) - 1);
    cfg.language[sizeof(cfg.language) - 1] = '\0';
  }
  if (server.hasArg("timezone")) {
    String timezone = server.arg("timezone");
    timezone.trim();
    copyToBuffer(cfg.timezone, sizeof(cfg.timezone), timezone);
  }
  if (server.hasArg("locale_time_format")) {
    cfg.global_time_format =
        clock_tile::normalize_time_format(server.arg("locale_time_format").toInt());
  }
  if (server.hasArg("locale_date_format")) {
    cfg.global_date_format =
        clock_tile::normalize_date_format(server.arg("locale_date_format").toInt());
  }
  bool access_changed = false;
  bool settings_visibility_changed = false;
  bool settings_visibility_commit_needed = false;
  bool settings_gesture_changed = false;
  int settings_tile_target_col = -1;
  int settings_tile_target_row = -1;
  if (server.hasArg("settings_access_present")) {
    const auto& tr = i18n::strings(cfg.language);
    const bool enable_pin = server.hasArg("settings_pin_enabled");
    const bool hide_tile = server.hasArg("settings_tile_hidden");
    const bool enable_swipe = server.hasArg("settings_swipe_enabled");
    String new_pin = server.hasArg("settings_pin")
                         ? server.arg("settings_pin")
                         : String();
    new_pin.trim();

    const bool has_target_col = server.hasArg("settings_tile_target_col");
    const bool has_target_row = server.hasArg("settings_tile_target_row");
    if (has_target_col != has_target_row) {
      sendSaveError(400, tr.save_failed);
      return;
    }
    if (has_target_col) {
      settings_tile_target_col =
          server.arg("settings_tile_target_col").toInt();
      settings_tile_target_row =
          server.arg("settings_tile_target_row").toInt();
      if (settings_tile_target_col < 0 ||
          settings_tile_target_col >= GRID_COLS ||
          settings_tile_target_row < 0 ||
          settings_tile_target_row >= GRID_ROWS) {
        sendSaveError(400, tr.save_failed);
        return;
      }
    }

    if (!enable_pin) {
      cfg.settings_pin_enabled = false;
      pin_access::clearCredential(cfg.settings_pin_salt,
                                  cfg.settings_pin_hash);
      pin_access::secureClear(cfg.settings_pin_value,
                              sizeof(cfg.settings_pin_value));
    } else {
      if (new_pin.length()) {
        if (!pin_access::isValidUserPin(new_pin) ||
            !pin_access::makeCredential(new_pin, cfg.settings_pin_salt,
                                        cfg.settings_pin_hash)) {
          new_pin = "";
          sendSaveError(400, tr.pin_invalid);
          return;
        }
        new_pin.toCharArray(cfg.settings_pin_value,
                            sizeof(cfg.settings_pin_value));
      } else if (!pin_access::credentialIsSet(cfg.settings_pin_salt,
                                               cfg.settings_pin_hash)) {
        sendSaveError(400, tr.pin_invalid);
        return;
      }
      cfg.settings_pin_enabled = true;
    }
    Tile settings_tile;
    const bool settings_tile_present = tileConfig.getSettingsTile(settings_tile);
    if (hide_tile && settings_tile_present) {
      SettingsTileSnapshot& snapshot = cfg.settings_tile_snapshot;
      memset(&snapshot, 0, sizeof(snapshot));
      snapshot.valid = true;
      strncpy(snapshot.title, settings_tile.title.c_str(),
              sizeof(snapshot.title) - 1);
      strncpy(snapshot.icon_name, settings_tile.icon_name.c_str(),
              sizeof(snapshot.icon_name) - 1);
      snapshot.bg_color = settings_tile.bg_color;
      snapshot.col = settings_tile.col;
      snapshot.row = settings_tile.row;
      snapshot.span_w = settings_tile.span_w < 1 ? 1 : settings_tile.span_w;
      snapshot.span_h = settings_tile.span_h < 1 ? 1 : settings_tile.span_h;
    }
    if (!hide_tile && previous_cfg.settings_tile_hidden &&
        settings_tile_target_col >= 0 && cfg.settings_tile_snapshot.valid) {
      cfg.settings_tile_snapshot.col =
          static_cast<uint8_t>(settings_tile_target_col);
      cfg.settings_tile_snapshot.row =
          static_cast<uint8_t>(settings_tile_target_row);
    }
    if (server.hasArg("settings_tile_snapshot_present")) {
      if (!(hide_tile || previous_cfg.settings_tile_hidden) ||
          !cfg.settings_tile_snapshot.valid) {
        sendSaveError(409, tr.save_failed);
        return;
      }
      SettingsTileSnapshot& snapshot = cfg.settings_tile_snapshot;
      if (server.hasArg("settings_tile_title")) {
        server.arg("settings_tile_title")
            .toCharArray(snapshot.title, sizeof(snapshot.title));
      }
      if (server.hasArg("settings_tile_icon")) {
        server.arg("settings_tile_icon")
            .toCharArray(snapshot.icon_name, sizeof(snapshot.icon_name));
      }
      if (server.hasArg("settings_tile_bg_color_default") &&
          server.arg("settings_tile_bg_color_default").toInt() != 0) {
        snapshot.bg_color = 0;
      } else if (server.hasArg("settings_tile_bg_color")) {
        snapshot.bg_color = makeTileBgColor(static_cast<uint32_t>(
            server.arg("settings_tile_bg_color").toInt()));
      }
      const bool has_snapshot_col = server.hasArg("settings_tile_col");
      const bool has_snapshot_row = server.hasArg("settings_tile_row");
      const bool has_snapshot_span_w = server.hasArg("settings_tile_span_w");
      const bool has_snapshot_span_h = server.hasArg("settings_tile_span_h");
      const bool has_any_snapshot_layout =
          has_snapshot_col || has_snapshot_row || has_snapshot_span_w ||
          has_snapshot_span_h;
      const bool has_complete_snapshot_layout =
          has_snapshot_col && has_snapshot_row && has_snapshot_span_w &&
          has_snapshot_span_h;
      if (has_any_snapshot_layout && !has_complete_snapshot_layout) {
        sendSaveError(400, tr.save_failed);
        return;
      }
      if (has_complete_snapshot_layout) {
        auto parse_bounded_arg = [&](const char* name, int minimum,
                                     int maximum, int& out) {
          String value = server.arg(name);
          value.trim();
          if (!value.length()) return false;
          char* end = nullptr;
          const long parsed = strtol(value.c_str(), &end, 10);
          if (end == value.c_str() || *end != '\0' || parsed < minimum ||
              parsed > maximum) {
            return false;
          }
          out = static_cast<int>(parsed);
          return true;
        };
        int snapshot_col = 0;
        int snapshot_row = 0;
        int snapshot_span_w = 1;
        int snapshot_span_h = 1;
        if (!parse_bounded_arg("settings_tile_col", 0, GRID_COLS - 1,
                               snapshot_col) ||
            !parse_bounded_arg("settings_tile_row", 0, GRID_ROWS - 1,
                               snapshot_row) ||
            !parse_bounded_arg("settings_tile_span_w", 1, GRID_COLS,
                               snapshot_span_w) ||
            !parse_bounded_arg("settings_tile_span_h", 1, GRID_ROWS,
                               snapshot_span_h) ||
            snapshot_col + snapshot_span_w > GRID_COLS ||
            snapshot_row + snapshot_span_h > GRID_ROWS) {
          sendSaveError(400, tr.save_failed);
          return;
        }
        snapshot.col = static_cast<uint8_t>(snapshot_col);
        snapshot.row = static_cast<uint8_t>(snapshot_row);
        snapshot.span_w = static_cast<uint8_t>(snapshot_span_w);
        snapshot.span_h = static_cast<uint8_t>(snapshot_span_h);
      }
    }
    cfg.settings_tile_hidden = hide_tile;
    // Hidden Settings must always retain an on-device recovery path.
    cfg.settings_swipe_enabled = hide_tile || enable_swipe;
    new_pin = "";

    if (server.hasArg("settings_reveal_edge")) {
      const int edge = server.arg("settings_reveal_edge").toInt();
      cfg.settings_reveal_edge =
          edge >= static_cast<int>(SettingsRevealEdge::Left) &&
                  edge <= static_cast<int>(SettingsRevealEdge::Bottom)
              ? static_cast<uint8_t>(edge)
              : static_cast<uint8_t>(SettingsRevealEdge::Left);
    }

    access_changed =
        cfg.settings_pin_enabled != previous_cfg.settings_pin_enabled ||
        cfg.settings_tile_hidden != previous_cfg.settings_tile_hidden ||
        cfg.settings_swipe_enabled != previous_cfg.settings_swipe_enabled ||
        cfg.settings_reveal_edge != previous_cfg.settings_reveal_edge ||
        memcmp(cfg.settings_pin_salt, previous_cfg.settings_pin_salt,
               sizeof(cfg.settings_pin_salt)) != 0 ||
        memcmp(cfg.settings_pin_hash, previous_cfg.settings_pin_hash,
               sizeof(cfg.settings_pin_hash)) != 0 ||
        strcmp(cfg.settings_pin_value,
               previous_cfg.settings_pin_value) != 0 ||
        server.hasArg("settings_tile_snapshot_present");

    settings_gesture_changed =
        cfg.settings_swipe_enabled != previous_cfg.settings_swipe_enabled ||
        cfg.settings_reveal_edge != previous_cfg.settings_reveal_edge;

    settings_visibility_changed =
        cfg.settings_tile_hidden != previous_cfg.settings_tile_hidden;
    const bool requested_visible = !cfg.settings_tile_hidden;
    settings_visibility_commit_needed =
        settings_visibility_changed ||
        settings_tile_present != requested_visible;
    if (settings_visibility_commit_needed) {
      const SettingsTileVisibilityResult visibility =
          tileConfig.validateSettingsTileVisible(
              !cfg.settings_tile_hidden, settings_tile_target_col,
              settings_tile_target_row);
      if (visibility != SettingsTileVisibilityResult::Success) {
        sendSaveError(
            visibility == SettingsTileVisibilityResult::NoFreeCell ? 409 : 500,
            visibility == SettingsTileVisibilityResult::NoFreeCell
                ? tr.settings_home_full
                : tr.save_failed);
        return;
      }
    }
  }
  bool settings_config_saved = false;
  bool settings_visibility_commit_failed = false;
  bool settings_config_rolled_back = false;
  SettingsTileVisibilityResult settings_visibility_reconcile =
      SettingsTileVisibilityResult::Success;
  auto commit_settings = [&]() {
    settings_config_saved = configManager.save(cfg);
    if (!settings_config_saved || !settings_visibility_commit_needed) return;

    const SettingsTileVisibilityResult visibility =
        tileConfig.setSettingsTileVisible(
            !cfg.settings_tile_hidden, settings_tile_target_col,
            settings_tile_target_row);
    if (visibility == SettingsTileVisibilityResult::Success) return;

    settings_visibility_commit_failed = true;
    settings_config_rolled_back = configManager.save(previous_cfg);
    const bool reconcile_visible =
        settings_config_rolled_back ? !previous_cfg.settings_tile_hidden
                                    : !cfg.settings_tile_hidden;
    settings_visibility_reconcile =
        tileConfig.setSettingsTileVisible(reconcile_visible);
  };

#if defined(DEVICE_ESP32_S3_RGB_480)
  // Hiding or restoring Settings updates NVS and LittleFS. Keep both writes in
  // one nested display guard so the RGB scanout is restarted only once.
  {
    Device::ScopedStorageWrite settings_visibility_storage_write(
        settings_visibility_commit_needed);
    commit_settings();
  }
#else
  commit_settings();
#endif

  if (settings_config_saved) {
    if (settings_visibility_commit_failed) {
      Serial.printf(
          "[Access] Settings visibility commit failed: rollback=%s "
          "reconcile=%u\n",
          settings_config_rolled_back ? "ok" : "failed",
          static_cast<unsigned>(settings_visibility_reconcile));
      tiles_request_reload_all();
      const auto& tr = i18n::strings(configManager.getConfig().language);
      sendSaveError(500, tr.save_failed);
      return;
    }
    if (!access_only) {
      settings_refresh_language();
      uiManager.scheduleNtpSync(0);
      // Reload grids in the loop, never inside the WebServer callback.
      tiles_request_reload_all();
    } else if (settings_visibility_commit_needed) {
      tiles_request_reload_all();
    }
    if (settings_gesture_changed) {
      // Web Admin access-only saves run in the main loop. Apply the new edge
      // and enabled state immediately without rebuilding the page or device UI.
      uiManager.refreshSettingsGestureZone();
    }
    if (ajax_save) {
      String response = "{\"ok\":true,\"reload\":";
      response += access_changed && !access_only ? "true" : "false";
      response += ",\"settings_pin\":\"";
      String stored_pin;
      if (configManager.getSettingsPin(stored_pin)) {
        appendJsonEscaped(response, stored_pin);
      }
      stored_pin = "";
      response += "\"}";
      server.send(200, "application/json", response);
    } else {
      server.sendHeader("Location", "/");
      server.send(303, "text/plain", "");
    }
    // Reply first, as handleRestart() does, before requesting the MQTT worker.
    // The request can block for 500 ms; reconnect live without delaying the
    // browser response or restarting the device.
    if (!access_only) networkManager.requestMqttReconfigure();
  } else {
    const auto& tr = i18n::strings(cfg.language);
    if (ajax_save) {
      sendSaveError(500, tr.save_failed);
    } else {
      server.send(500, "text/html", String("<h1>") + tr.save_failed + "</h1>");
    }
  }
}

void WebAdminServer::handleSaveBridge() {
  HaBridgeConfigData updated = haBridgeConfig.get();
  const auto sensors = parseSensorList(updated.sensors_text);
  const auto scenes = parseSceneList(updated.scene_alias_text);
  bool changed = false;

  for (size_t i = 0; i < HA_SENSOR_SLOT_COUNT; ++i) {
    String field = "sensor_slot";
    field += static_cast<int>(i);
    String value = server.hasArg(field) ? server.arg(field) : "";
    value = normalizeSensorSelection(value, sensors);
    if (updated.sensor_slots[i] != value) {
      updated.sensor_slots[i] = value;
      changed = true;
    }
    String label_field = "sensor_label";
    label_field += static_cast<int>(i);
    String title = server.hasArg(label_field) ? server.arg(label_field) : "";
    title.trim();
    if (updated.sensor_titles[i] != title) {
      updated.sensor_titles[i] = title;
      changed = true;
    }
    String unit_field = "sensor_unit";
    unit_field += static_cast<int>(i);
    String unit = server.hasArg(unit_field) ? server.arg(unit_field) : "";
    unit.trim();
    if (value.isEmpty()) {
      unit = "";
    }
    if (updated.sensor_custom_units[i] != unit) {
      updated.sensor_custom_units[i] = unit;
      changed = true;
    }

    // Farbe parsen (z.B. "#2A2A2A" → 0x2A2A2A)
    String color_field = "sensor_color";
    color_field += static_cast<int>(i);
    String colorStr = server.hasArg(color_field) ? server.arg(color_field) : "";
    colorStr.trim();

    uint32_t color = 0;
    if (colorStr.length() > 0 && colorStr[0] == '#') {
      colorStr = colorStr.substring(1); // "#" entfernen
      color = strtoul(colorStr.c_str(), nullptr, 16);
    }

    if (updated.sensor_colors[i] != color) {
      updated.sensor_colors[i] = color;
      changed = true;
    }
  }
  for (size_t i = 0; i < HA_SCENE_SLOT_COUNT; ++i) {
    String field = "scene_slot";
    field += static_cast<int>(i);
    String value = server.hasArg(field) ? server.arg(field) : "";
    value = normalizeSceneSelection(value, scenes);
    if (updated.scene_slots[i] != value) {
      updated.scene_slots[i] = value;
      changed = true;
    }
    String label_field = "scene_label";
    label_field += static_cast<int>(i);
    String title = server.hasArg(label_field) ? server.arg(label_field) : "";
    title.trim();
    if (updated.scene_titles[i] != title) {
      updated.scene_titles[i] = title;
      changed = true;
    }

    // Farbe parsen (z.B. "#353535" → 0x353535)
    String color_field = "scene_color";
    color_field += static_cast<int>(i);
    String colorStr = server.hasArg(color_field) ? server.arg(color_field) : "";
    colorStr.trim();

    uint32_t color = 0;
    if (colorStr.length() > 0 && colorStr[0] == '#') {
      colorStr = colorStr.substring(1); // "#" entfernen
      color = strtoul(colorStr.c_str(), nullptr, 16);
    }

    if (updated.scene_colors[i] != color) {
      updated.scene_colors[i] = color;
      changed = true;
    }
  }

  if (!changed) {
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "");
    return;
  }

  if (haBridgeConfig.save(updated)) {
    // Reload grids from the main loop.
    tiles_request_reload_all();
    mqttReloadDynamicSlots();
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "");
  } else {
    const auto& tr = i18n::strings(configManager.getConfig().language);
    server.send(500, "text/html", String("<h1>") + tr.save_failed + "</h1>");
  }
}

void WebAdminServer::handleBridgeRefresh() {
  if (!networkManager.isMqttConnected()) {
    server.send(503, "text/html",
                "<h1>MQTT ist nicht verbunden - bitte später erneut versuchen.</h1>");
    return;
  }
  networkManager.publishBridgeRequest(true);
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void WebAdminServer::handleStatus() {
  webAdminMarkActivity();
  sendChunkedResponse(server, 200, "application/json", getStatusJSON());
}

void WebAdminServer::handleRestart() {
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
  Serial.println("[WebAdmin] Restart requested");
  prepareDisplayForRestart();
  delay(200);
  BoardHAL::restart();
}

void WebAdminServer::handleTileRadius() {
  webAdminMarkActivity();
  if (server.method() == HTTP_POST) {
    const String value = server.arg("radius");
    bool valid = value.length() > 0 && value.length() <= 4;
    for (size_t i = 0; i < value.length(); ++i)
      valid = valid && value[i] >= '0' && value[i] <= '9';
    const long radius = value.toInt();
    if (!valid || radius < tile_radius::kMinimum || radius > tile_radius::kMaximum) {
      sendJsonError(server, 400, "Invalid tile radius");
      return;
    }
    if (server.arg("preview") == "1") {
      ui_surface_style::preview_radius(radius);
      server.send(200, "application/json", String("{\"success\":true,\"radius\":") + radius + "}");
      return;
    }
    if (!configManager.saveTileRadius(static_cast<uint16_t>(radius))) {
      sendJsonError(server, 500, "Could not save tile radius");
      return;
    }
    ui_surface_style::request_global_radius_refresh();
  }
  server.send(200, "application/json",
      String("{\"success\":true,\"radius\":") + configManager.getConfig().tile_radius + "}");
}

void WebAdminServer::handleSaveTileBorders() {
  webAdminMarkActivity();
  if (!server.hasArg("enabled")) {
    sendJsonError(server, 400, "Missing enabled value");
    return;
  }

  String value = server.arg("enabled");
  value.trim();
  value.toLowerCase();
  const bool enabled = value == "1" || value == "true" || value == "on";
  if (!configManager.saveTileBorders(enabled)) {
    sendJsonError(server, 500, "Could not save tile borders");
    return;
  }

  // Registered tile, folder, Back, Settings and popup surfaces apply the
  // option on the next safe UI pass. This handler does not touch LVGL;
  // the screensaver keeps its independent setting.
  ui_surface_style::request_global_tile_border_refresh();
  server.send(200, "application/json",
              enabled ? "{\"success\":true,\"enabled\":true}"
                      : "{\"success\":true,\"enabled\":false}");
}
