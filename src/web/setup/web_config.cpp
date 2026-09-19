#include "src/web/setup/web_config.h"
#include "src/devices/device_select.h"
#include "src/devices/device.h"
#include "src/web/server/web_admin_utils.h"
#include "src/web/server/assets/web_admin_fonts.h"
#include <WiFi.h>

// Shared instance.
WebConfigServer webConfigServer;

// Hotspot settings.
static const char* AP_PASS = "12345678";  // WPA2 requires at least eight characters.
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

namespace {

const char* apSsidForDevice() {
#if defined(DEVICE_M5STACKS_TAB5)
  return "M5Stacks_Tab5_Config";
#elif defined(DEVICE_GUITION_JC8012P4A1)
  return "Guition_JC8012P4A1_Config";
#elif defined(DEVICE_GUITION_JC8012P4A1_V2)
  return "Guition_JC8012P4A1_V2_Config";
#elif defined(DEVICE_GUITION_JC1060P470C)
  return "Guition_JC1060P470C_Config";
#elif defined(DEVICE_GUITION_JC1060P470C_V2)
  return "Guition_JC1060P470C_V2_Config";
#elif defined(DEVICE_GUITION_JC4880P443_PORTRAIT)
  return "Guition_JC4880P443_P_Config";
#elif defined(DEVICE_GUITION_ESP32_4848S040)
  return "Guition_4848S040_Config";
#elif defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4)
  return "Waveshare_S3_4_Config";
#elif defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4B)
  return "Waveshare_S3_4B_Config";
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_4_3)
  return "Waveshare_LCD4_3_Config";
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_7)
  return "Waveshare_LCD7_Config";
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)
  return "Waveshare_LCD7B_Config";
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_8)
  return "Waveshare_LCD8_Config";
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
  return "Waveshare_LCD10_1_Config";
#elif defined(DEVICE_LAYOUT_TEST_1024X600)
  return "HomeTiles_1024x600_Test";
#elif defined(DEVICE_LAYOUT_TEST_480X480)
  return "HomeTiles_480x480_Test";
#elif defined(DEVICE_WAVESHARE_4B)
  return "Waveshare_B4_Config";
#else
  return "ESP32_P4_Config";
#endif
}

void applyWifiAutoReconnectPolicy() {
#if defined(CONFIG_ESP_WIFI_REMOTE_ENABLED) && CONFIG_ESP_WIFI_REMOTE_ENABLED
  WiFi.setAutoReconnect(false);
#else
  WiFi.setAutoReconnect(true);
#endif
}

bool setWifiModeWithSdRemount(wifi_mode_t mode) {
#if defined(DEVICE_GUITION_JC1060P470C_FAMILY) || \
    defined(DEVICE_GUITION_JC8012P4A1)
  const bool sd_was_mounted = Device::suspendSDCardForNetworkTransition();
  const bool mode_ok = WiFi.mode(mode);
  if (sd_was_mounted && !Device::resumeSDCardAfterNetworkTransition()) {
    Serial.println("[WebConfig] SD remount after ESP-Hosted mode change failed");
  }
  return mode_ok;
#else
  return WiFi.mode(mode);
#endif
}

}  // namespace

const char* webConfigApSsid() {
  return apSsidForDevice();
}

const char* webConfigApPassword() {
  return AP_PASS;
}

static void restoreStaModeAfterAp() {
#if defined(DEVICE_ESP32_S3_RGB_480)
  // Calling persistent(false) after mode() is too late for the first
  // esp_wifi_set_mode() transition on the RGB-S3.
  WiFi.persistent(false);
#endif
  applyWifiAutoReconnectPolicy();
  setWifiModeWithSdRemount(WIFI_STA);
  applyWifiAutoReconnectPolicy();
  WiFi.persistent(false);
}

WebConfigServer::WebConfigServer()
    : server(80), running(false), config_saved(false),
      routes_registered(false) {}

bool WebConfigServer::start() {
  if (running) {
    Serial.println("⚠️ WebConfigServer already running");
    return true;
  }

  Serial.println("\n🌐 Starting WiFi configuration mode...");

  // Disconnect the current Wi-Fi connection before captive portal setup.
#if defined(DEVICE_ESP32_S3_RGB_480)
  WiFi.persistent(false);
#endif
  applyWifiAutoReconnectPolicy();
  WiFi.disconnect();
  delay(100);

  // Retain AP + STA mode from the original Tab5_LVGL implementation.
  setWifiModeWithSdRemount(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);

  // Start a visible AP on channel 1 with at most four clients.
  const char* ap_ssid = webConfigApSsid();
  bool ap_ok = WiFi.softAP(ap_ssid, AP_PASS, 1, 0, 4);
  if (!ap_ok) {
    restoreStaModeAfterAp();
  }
  if (!ap_ok) {
    Serial.println("❌ Could not start access point!");
    return false;
  }

  delay(500);

  IPAddress ip = WiFi.softAPIP();
  Serial.printf("✓ Access point started: %s\n", ap_ssid);
  Serial.printf("  Password: %s\n", AP_PASS);
  Serial.printf("  IP address: %s\n", ip.toString().c_str());

  // Redirect all DNS requests to the captive portal.
  if (dnsServer.start(53, "*", AP_IP)) {
    Serial.println("✓ DNS server started (port 53, all domains → 192.168.4.1)");
  } else {
    Serial.println("⚠️ Could not start DNS server!");
  }

  // WebServer::stop() closes the socket but retains handlers. Register routes
  // once per server object to avoid permanent heap growth on every AP start.
  if (!routes_registered) {
    server.on("/", [this]() { this->handleRoot(); });
    server.on("/assets/inter-4.1-regular.woff2", HTTP_GET,
              [this]() { sendWebFontRegular(this->server); });
    server.on("/assets/inter-4.1-semibold.woff2", HTTP_GET,
              [this]() { sendWebFontSemibold(this->server); });
    auto captive_handler = [this]() { this->handleCaptivePortal(); };
    server.on("/generate_204", captive_handler);
    server.on("/gen_204", captive_handler);
    server.on("/hotspot-detect.html", captive_handler);
    server.on("/library/test/success.html", captive_handler);
    server.on("/success.txt", captive_handler);
    server.on("/ncsi.txt", captive_handler);
    server.on("/connecttest.txt", captive_handler);
    server.on("/redirect", captive_handler);
    server.on("/wpad.dat", captive_handler);
    server.on("/favicon.ico", captive_handler);
    server.on("/save", HTTP_POST, [this]() { this->handleSave(); });
    server.onNotFound([this]() { this->handleNotFound(); });
    routes_registered = true;
  }

  server.begin();
  Serial.println("✓ Web server started at http://192.168.4.1");
  Serial.printf("  Connect to WiFi '%s' and open your browser\n", ap_ssid);

  running = true;
  config_saved = false;
  return true;
}

void WebConfigServer::stop() {
  if (!running)
    return;

  Serial.println("🛑 Stopping WebConfigServer...");

  dnsServer.stop();
  Serial.println("  ✓ DNS server stopped");

  server.stop();
  Serial.println("  ✓ Web server stopped");

  WiFi.softAPdisconnect(true);
  restoreStaModeAfterAp();
  Serial.println("  ✓ AP disconnected");

  Serial.println("  ✓ WiFi mode: STA");

  running = false;
  Serial.println(
      "✓ WebConfigServer stopped - ready for normal WiFi connection");
}

void WebConfigServer::handle() {
  if (!running) return;
  dnsServer.processNextRequest();
  server.handleClient();
}

void WebConfigServer::handleRoot() {
  Serial.println("📄 Configuration page requested");
  sendChunkedResponse(server, 200, "text/html", getConfigPage());
}

void WebConfigServer::handleCaptivePortal() {
  String path = server.uri();
  Serial.printf("🌐 Captive Portal Request: %s (Host: %s)\n", path.c_str(), server.hostHeader().c_str());

  // Serve the configuration page directly for captive portal probes,
  // including the checks used by current smartphones.
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  sendChunkedResponse(server, 200, "text/html", getConfigPage());
  Serial.println("  ✓ Configuration page sent directly");
}

void WebConfigServer::handleSave() {
  Serial.println("💾 Saving WiFi configuration...");

  // Read Wi-Fi POST fields; AP setup preserves the remaining configuration.
  DeviceConfig cfg = configManager.getConfig();  // Includes display, sleep and MQTT defaults.
  if (!configManager.isConfigured()) {
    cfg.mqtt_port = 1883;
    if (cfg.display_brightness < Device::kBacklightInputMin) {
      cfg.display_brightness = 200;  // Keep the display visible during setup.
    }
    if (cfg.auto_sleep_seconds == 0) {
      cfg.auto_sleep_seconds = 60;
    }
    if (cfg.auto_sleep_battery_seconds == 0) {
      cfg.auto_sleep_battery_seconds = cfg.auto_sleep_seconds;
    }
    cfg.auto_sleep_battery_enabled = cfg.auto_sleep_enabled;
    if (cfg.mqtt_base_topic[0] == '\0') {
      strncpy(cfg.mqtt_base_topic, "hometiles", CONFIG_MQTT_BASE_MAX - 1);
    }
    if (cfg.ha_prefix[0] == '\0') {
      strncpy(cfg.ha_prefix, "ha/statestream", CONFIG_HA_PREFIX_MAX - 1);
    }
  }

  // Replace only the Wi-Fi credentials.
  if (server.hasArg("wifi_ssid")) {
    String ssid = server.arg("wifi_ssid");
    strncpy(cfg.wifi_ssid, ssid.c_str(), CONFIG_WIFI_SSID_MAX - 1);
  }

  if (server.hasArg("wifi_pass")) {
    String pass = server.arg("wifi_pass");
    strncpy(cfg.wifi_pass, pass.c_str(), CONFIG_WIFI_PASS_MAX - 1);
  }

  // Captive portal always resets WiFi addressing back to DHCP.
  // This prevents stale static IP settings from locking the device out
  // when the user only wants to reconnect it to a network.
  cfg.wifi_static_enabled = false;
  cfg.wifi_static_ip[0] = '\0';
  cfg.wifi_gateway[0] = '\0';
  cfg.wifi_subnet[0] = '\0';
  cfg.wifi_dns[0] = '\0';

  // Validate the required SSID.
  if (strlen(cfg.wifi_ssid) == 0) {
    server.send(400, "text/html", "<h1>WiFi SSID is required</h1>");
    return;
  }

  // Save the configuration while preserving MQTT settings.
  if (configManager.save(cfg)) {
    Serial.println("✓ WiFi configuration saved successfully");
    config_saved = true;
    sendChunkedResponse(server, 200, "text/html", getSuccessPage());
  } else {
    Serial.println("❌ Failed to save configuration");
    server.send(500, "text/html", "<h1>Saving the configuration failed</h1>");
  }
}

void WebConfigServer::handleNotFound() {
  String path = server.uri();
  Serial.printf("❓ Not Found: %s (Host: %s)\n", path.c_str(), server.hostHeader().c_str());

  // Serve configuration for unknown paths too, so captive portal discovery
  // can reach setup regardless of its probe URL.
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  sendChunkedResponse(server, 200, "text/html", getConfigPage());
  Serial.println("  ✓ Configuration page sent (not found → configuration page)");
}

String WebConfigServer::getConfigPage() {
  // Read the saved configuration when available.
  const DeviceConfig& cfg = configManager.getConfig();
  const String ap_page_title = String(Device::displayName()) + " WiFi Configuration";

  String html = "<!DOCTYPE html>\n<html lang=\"en\">\n";
  html += R"html(<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>)html";
  html += ap_page_title;
  html += R"html(</title>
  <link rel="icon" type="image/svg+xml" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'%3E%3Crect width='48' height='48' rx='10' fill='%2316181c'/%3E%3Crect x='4' y='4' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Crect x='27' y='4' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Crect x='4' y='27' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Cpath d='M33 26h5v6.5h6.5v5H38V44h-5v-6.5h-6.5v-5H33z' fill='%2326a69a'/%3E%3C/svg%3E">
)html";
  appendWebFontFaceStyles(html);
  html += R"html(
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'HomeTiles Inter', sans-serif;
      background: #0a0a0a;
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }
    .container {
      background: #1c1c1c;
      border: 1px solid #2a2a2a;
      border-radius: 22px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.5);
      max-width: 420px;
      width: 100%;
      padding: 32px;
    }
    .brand {
      display: flex;
      align-items: center;
      gap: 14px;
      margin-bottom: 28px;
    }
    .brand h1 {
      color: #ffffff;
      font-size: 22px;
    }
    .brand .device {
      color: #8a8a8a;
      font-size: 13px;
      margin-top: 2px;
    }
    .form-group {
      margin-bottom: 16px;
    }
    label {
      display: block;
      color: #b8b8b8;
      font-size: 14px;
      font-weight: 500;
      margin-bottom: 6px;
    }
    input {
      width: 100%;
      padding: 12px 16px;
      border: 1px solid #333333;
      border-radius: 12px;
      background: #141414;
      color: #ffffff;
      font-size: 15px;
      transition: border-color 0.2s;
      font-family: inherit;
    }
    input:focus {
      outline: none;
      border-color: #26a69a;
    }
    input::placeholder {
      color: #666666;
    }
    .password-field {
      display: flex;
      gap: 8px;
      align-items: center;
    }
    .password-field input {
      flex: 1 1 auto;
    }
    .password-toggle {
      flex: 0 0 auto;
      padding: 12px 14px;
      border: 1px solid #333333;
      border-radius: 12px;
      background: #141414;
      color: #b8b8b8;
      font-size: 13px;
      font-weight: 600;
      cursor: pointer;
    }
    .hint {
      color: #666666;
      font-size: 12px;
      margin-top: 4px;
    }
    .btn {
      width: 100%;
      padding: 14px;
      /* Match the green action buttons; teal remains the focus/logo accent. */
      background: #2e7d32;
      color: white;
      border: none;
      border-radius: 12px;
      font-size: 16px;
      font-weight: 600;
      cursor: pointer;
      margin-top: 8px;
    }
    .btn:active {
      background: #1b5e20;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="brand">
      <svg width="48" height="48" viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg" style="flex-shrink:0;" aria-hidden="true">
        <rect x="4" y="4" width="17" height="17" rx="4" fill="#ffffff"/>
        <rect x="27" y="4" width="17" height="17" rx="4" fill="#ffffff"/>
        <rect x="4" y="27" width="17" height="17" rx="4" fill="#ffffff"/>
        <path d="M33 26h5v6.5h6.5v5H38V44h-5v-6.5h-6.5v-5H33z" fill="#26a69a"/>
      </svg>
      <div>
        <h1>HomeTiles</h1>
        <div class="device">)html";
  html += String(Device::displayName());
  html += R"html(</div>
      </div>
    </div>

    <form action="/save" method="POST">
      <div class="form-group">
        <label for="wifi_ssid">Network</label>
        <input type="text" id="wifi_ssid" name="wifi_ssid" placeholder="My WiFi" value=")html";
  appendHtmlEscaped(html, String(cfg.wifi_ssid));
  html += R"html(" required>
      </div>
      <div class="form-group">
        <label for="wifi_pass">Password</label>
        <div class="password-field">
          <input type="password" id="wifi_pass" name="wifi_pass" placeholder="Password" value=")html";
  appendHtmlEscaped(html, String(cfg.wifi_pass));
  html += R"html(">
          <button type="button" class="password-toggle" onclick="togglePasswordVisibility('wifi_pass', this)">Show</button>
        </div>
        <div class="hint">Leave empty for an open network</div>
      </div>

      <button type="submit" class="btn">Connect</button>
    </form>
  </div>
  <script>
    function togglePasswordVisibility(inputId, buttonEl) {
      const input = document.getElementById(inputId);
      if (!input || !buttonEl) return;
      const isHidden = input.type === 'password';
      input.type = isHidden ? 'text' : 'password';
      buttonEl.textContent = isHidden ? 'Hide' : 'Show';
    }
  </script>
</body>
</html>
)html";

  return html;
}

String WebConfigServer::getSuccessPage() {
  String html = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Connecting...</title>
  <link rel="icon" type="image/svg+xml" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'%3E%3Crect width='48' height='48' rx='10' fill='%2316181c'/%3E%3Crect x='4' y='4' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Crect x='27' y='4' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Crect x='4' y='27' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Cpath d='M33 26h5v6.5h6.5v5H38V44h-5v-6.5h-6.5v-5H33z' fill='%2326a69a'/%3E%3C/svg%3E">
)html";
  appendWebFontFaceStyles(html);
  html += R"html(
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'HomeTiles Inter', sans-serif;
      background: #0a0a0a;
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }
    .container {
      background: #1c1c1c;
      border: 1px solid #2a2a2a;
      border-radius: 22px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.5);
      max-width: 420px;
      width: 100%;
      padding: 32px;
      text-align: center;
    }
    .logo {
      margin: 0 auto 20px;
      width: 56px;
      height: 56px;
    }
    h1 {
      color: #ffffff;
      font-size: 22px;
      margin-bottom: 10px;
    }
    p {
      color: #8a8a8a;
      font-size: 14px;
      line-height: 1.6;
    }
  </style>
</head>
<body>
  <div class="container">
    <svg class="logo" viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
      <rect x="4" y="4" width="17" height="17" rx="4" fill="#ffffff"/>
      <rect x="27" y="4" width="17" height="17" rx="4" fill="#ffffff"/>
      <rect x="4" y="27" width="17" height="17" rx="4" fill="#ffffff"/>
      <path d="M33 26h5v6.5h6.5v5H38V44h-5v-6.5h-6.5v-5H33z" fill="#26a69a"/>
    </svg>
    <h1>Connecting...</h1>
    <p>HomeTiles is joining your network now.<br>This page will lose its connection - you can close it.</p>
  </div>
</body>
</html>
)html";
  return html;
}
