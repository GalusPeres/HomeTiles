#include "src/network/network_manager.h"
#include "src/network/transport/network_transport.h"
#include "src/core/config/config_manager.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/web/server/web_admin.h"
#include "src/ui/ui_manager.h"
#include "src/ui/tabs/settings/tab_settings.h"
#include "src/devices/device.h"
#include "src/core/hardware/board_hal.h"
#include "src/core/diagnostics/crash_log.h"
#include "src/video/camera_stream.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#if defined(CONFIG_IDF_TARGET_ESP32P4)
// Optional diagnostic hook exported by the HomeTiles ESP-Hosted SDIO object.
// Keep it weak so stock-core and non-P4 builds remain link-compatible.
extern "C" void hometiles_sdio_get_rx_diag(
    uint32_t* last_pkt_len_raw, uint32_t* rx_byte_count,
    uint32_t* last_interrupts, uint32_t* legal_fffff_hits,
    uint32_t* pending_drains, uint32_t* alloc_retries,
    uint32_t* bus_faults) __attribute__((weak));
#endif

// Shared instance.
HomeTilesNetworkManager networkManager;

static constexpr uint16_t kMqttBufferOta = 1024;
static constexpr uint16_t kMqttBufferNormal = 16 * 1024;
// When media tiles are configured, the normal buffer must fit Bridge
// media states with embedded 240 px covers: about 14 KB JPEG or 19 KB
// base64/JSON. PubSubClient discards them at 16 KB; covers then arrived
// only by chance during a 32 KB large-buffer window.
static constexpr uint16_t kMqttBufferMedia = 24 * 1024;
static constexpr uint16_t kMqttBufferLarge = 32 * 1024;
static constexpr uint32_t kMqttPostConnectQuietMs = 3000;
static constexpr uint32_t kWiredDhcpWaitMs = 10000;
// While Ethernet has carrier, delay Wi-Fi until this no-IP timeout.
// Starting Hosted consumes the DMA memory the Ethernet backend needs
// for data buffers; in the 2026-07-16 field test Ethernet never came up
// while Wi-Fi kept retrying an unresponsive C6. The timeout also lets
// Wi-Fi take over on networks without DHCP.
static constexpr uint32_t kWiredLinkWifiBlockMs = 60000;
// Consecutive STA start failures that mark ESP-Hosted unresponsive
// after RPC timeouts to the C6 coprocessor.
static constexpr uint8_t kWifiStartWedgeThreshold = 3;
// Healthy WiFi.begin()/mode calls return in milliseconds. An unresponsive
// C6 reaches the 5-second RPC timeout. A connection attempt longer than
// this threshold is evidence of a stuck transport even if the call
// formally reports success.
static constexpr uint32_t kWifiRpcSlowMs = 3000;
// ESP-Hosted caches WiFi.status(). If MQTT disconnects while that cache
// still reports WL_CONNECTED, probe a real RPC after a short grace period.
// Healthy Wi-Fi takes milliseconds; an unresponsive C6 costs one 5-second
// timeout before the safe recovery path takes over.
static constexpr uint32_t kWifiHealthProbeDelayMs = 15000;
static constexpr uint32_t kWifiHealthProbeIntervalMs = 60000;
// Minimum uptime before an automatic transport-recovery restart.
// Avoid a tight reboot loop if the C6 remains faulty.
static constexpr uint32_t kWedgeRestartMinUptimeMs = 2 * 60 * 1000;
static constexpr uint8_t kMqttOutboundDrainNormal = 12;
static constexpr uint8_t kMqttOutboundDrainStorm = 1;
static constexpr size_t kMqttMinDmaLargestBeforeTx = 8 * 1024;
// Contiguous emergency reserve for ESP-Hosted. Hold it while healthy and
// release it under DMA pressure so SDIO RX/TX immediately has an
// unfragmented block, without depending on small free regions eventually
// coalescing.
static constexpr size_t kMqttDmaReserveBytes = 12 * 1024;
static constexpr size_t kMqttDmaReserveRearmLargest =
    kMqttDmaReserveBytes + kMqttMinDmaLargestBeforeTx + 4 * 1024;
static constexpr uint32_t kMqttDmaReserveRearmStableMs = 5000;
// A large request must not wait indefinitely at the DMA threshold.
// If releasing the reserve does not restore enough heap, rebuild only
// the Wi-Fi/SDIO transport in a controlled recovery.
static constexpr uint32_t kMqttDmaRecoveryWaitMs = 5000;
static constexpr uint32_t kMqttDmaRecoveryCooldownMs = 30000;
// Subscribe/unsubscribe can immediately trigger retained packets. On P4,
// space these control packets so SDIO RX can forward each response to the
// MQTT inbound queue and release its DMA buffer.
static constexpr uint32_t kMqttSdioControlQuietMs = 50;

static void applyWifiAutoReconnectPolicy() {
#if defined(CONFIG_ESP_WIFI_REMOTE_ENABLED) && CONFIG_ESP_WIFI_REMOTE_ENABLED
  // ESP32-P4 uses ESP-Hosted control RPCs. HomeTiles owns normal reconnects;
  // this reduces repeated reconnect RPCs from the Arduino event task. Its
  // first retry is unconditional, so RPC serialization remains mandatory.
  WiFi.setAutoReconnect(false);
#else
  WiFi.setAutoReconnect(true);
#endif
}

// Keep the MQTT receive buffer small immediately after connecting. It
// now lives in PSRAM, but this quiet window still prevents large history
// or Bridge replies from competing with the retained-message burst for
// SDIO RX buffers.
static constexpr uint32_t kMqttStormWindowMs = 8000;

// ---------------------------------------------------------------------------
// Outbound-Command-Queues (Single-Owner MQTT)
//
// Counterpart to the inbound queue in mqtt_handlers.cpp: any task may
// enqueue, but only the worker dequeues commands and accesses mqtt_client.
// Normal publishes, large-response requests and SDIO control commands
// have separate queues. History, energy and Bridge requests must wait
// when DMA reserve is low without holding up scene, light or other small
// interactive commands. Allocate one block per command as
// [MqttOutboundCmd][topic\0][payload], preferring PSRAM, like mqttAllocInbound().
// ---------------------------------------------------------------------------
enum class MqttCmdKind : uint8_t { PUBLISH, SUBSCRIBE, UNSUBSCRIBE };

struct MqttOutboundCmd {
  MqttCmdKind kind;
  bool retain;
  uint32_t large_buffer_hold_ms;
  uint32_t editable_connection_generation;
  size_t payload_len;
  char* topic;       // Points into the same allocation.
  uint8_t* payload;  // Points into the same allocation; empty for SUBSCRIBE/UNSUBSCRIBE.
};

// 64 entries covered a mqttReloadDynamicSlots() burst, but the combined
// post-connect routes, discovery, settings, snapshot and reload can queue
// about 90 commands while the worker is inside a large readPacket().
// Use 128 entries to accommodate that burst.
static constexpr size_t kMqttPublishQueueDepth = 128;
static constexpr size_t kMqttLargePublishQueueDepth = 32;
static constexpr size_t kMqttControlQueueDepth = 128;
static std::atomic<uint32_t> g_editable_connection_generation{1};
static QueueHandle_t g_mqtt_publish_queue = nullptr;
static QueueHandle_t g_mqtt_large_publish_queue = nullptr;
static QueueHandle_t g_mqtt_control_queue = nullptr;
static uint32_t g_mqtt_outbound_dropped = 0;
static uint32_t g_mqtt_last_tx_guard_log_ms = 0;
static uint32_t g_mqtt_sdio_control_quiet_until = 0;
static void* g_mqtt_dma_reserve = nullptr;
static volatile bool g_mqtt_dma_reserve_held = false;
static uint32_t g_mqtt_dma_reserve_rearm_since = 0;
static uint32_t g_mqtt_dma_low_since = 0;
static uint32_t g_mqtt_dma_recovery_cooldown_until = 0;

static void initMqttDmaReserve() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (g_mqtt_dma_reserve || !networkTransport.isSdioWifiActive()) return;
  g_mqtt_dma_reserve = heap_caps_malloc(
      kMqttDmaReserveBytes,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (g_mqtt_dma_reserve) {
    g_mqtt_dma_reserve_held = true;
    Serial.printf("[MQTT] DMA reserve allocated: %u KB\n",
                  static_cast<unsigned>(kMqttDmaReserveBytes / 1024));
  } else {
    Serial.println("[MQTT] WARNING: Could not allocate DMA reserve");
  }
#endif
}

static void releaseMqttDmaReserve(const char* reason) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!g_mqtt_dma_reserve) return;
  g_mqtt_dma_reserve_held = false;
  heap_caps_free(g_mqtt_dma_reserve);
  g_mqtt_dma_reserve = nullptr;
  g_mqtt_dma_reserve_rearm_since = 0;
  Serial.printf("[MQTT] DMA reserve released (%s), largest=%u KB\n",
                reason ? reason : "?",
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(
                        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) /
                    1024));
#else
  (void)reason;
#endif
}

// Under pressure, release the guaranteed contiguous reserve first.
// Reallocate only after several seconds with substantially more than
// reserve plus minimum-block headroom, avoiding allocation/free churn.
static size_t serviceMqttDmaHeadroom(uint32_t now_ms) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!networkTransport.isSdioWifiActive()) {
    g_mqtt_dma_reserve_rearm_since = 0;
    return static_cast<size_t>(-1);
  }

  size_t largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (largest < kMqttMinDmaLargestBeforeTx && g_mqtt_dma_reserve) {
    const size_t largest_before_release = largest;
    g_mqtt_dma_reserve_held = false;
    heap_caps_free(g_mqtt_dma_reserve);
    g_mqtt_dma_reserve = nullptr;
    g_mqtt_dma_reserve_rearm_since = 0;
    largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    Serial.printf(
        "[MQTT] DMA reserve released: largest %u KB -> %u KB\n",
        static_cast<unsigned>(largest_before_release / 1024),
        static_cast<unsigned>(largest / 1024));
  }

  if (!g_mqtt_dma_reserve) {
    if (largest >= kMqttDmaReserveRearmLargest) {
      if (g_mqtt_dma_reserve_rearm_since == 0) {
        g_mqtt_dma_reserve_rearm_since = now_ms;
      } else if ((uint32_t)(now_ms - g_mqtt_dma_reserve_rearm_since) >=
                 kMqttDmaReserveRearmStableMs) {
        void* reserve = heap_caps_malloc(
            kMqttDmaReserveBytes,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (reserve) {
          g_mqtt_dma_reserve = reserve;
          g_mqtt_dma_reserve_held = true;
          largest = heap_caps_get_largest_free_block(
              MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
          Serial.printf("[MQTT] DMA reserve allocated again, largest=%u KB\n",
                        static_cast<unsigned>(largest / 1024));
        }
        g_mqtt_dma_reserve_rearm_since = 0;
      }
    } else {
      g_mqtt_dma_reserve_rearm_since = 0;
    }
  }
  return largest;
#else
  (void)now_ms;
  return static_cast<size_t>(-1);
#endif
}

static MqttOutboundCmd* mqttAllocOutbound(MqttCmdKind kind,
                                          const char* topic,
                                          const uint8_t* payload,
                                          size_t payload_len,
                                          bool retain,
                                          uint32_t large_buffer_hold_ms) {
  if (!topic || !*topic) return nullptr;
  const size_t topic_len = strlen(topic);
  const size_t total = sizeof(MqttOutboundCmd) + topic_len + 1 + payload_len;
  uint8_t* block = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!block) block = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_8BIT));
  if (!block) return nullptr;
  MqttOutboundCmd* cmd = reinterpret_cast<MqttOutboundCmd*>(block);
  cmd->kind = kind;
  cmd->retain = retain;
  cmd->editable_connection_generation = g_editable_connection_generation.load();
  cmd->large_buffer_hold_ms = large_buffer_hold_ms;
  cmd->payload_len = payload_len;
  cmd->topic = reinterpret_cast<char*>(block + sizeof(MqttOutboundCmd));
  cmd->payload = reinterpret_cast<uint8_t*>(cmd->topic + topic_len + 1);
  memcpy(cmd->topic, topic, topic_len);
  cmd->topic[topic_len] = '\0';
  if (payload_len) memcpy(cmd->payload, payload, payload_len);
  return cmd;
}

// Never block or process inline, which would access mqtt_client from
// the wrong task. Drop and log allocation or queue-capacity failures.
static bool enqueueOutboundCmd(MqttCmdKind kind,
                               const char* topic,
                               const uint8_t* payload,
                               size_t payload_len,
                               bool retain,
                               bool priority = false,
                               uint32_t large_buffer_hold_ms = 0) {
  const bool large_publish =
      kind == MqttCmdKind::PUBLISH && large_buffer_hold_ms > 0;
  QueueHandle_t queue = kind == MqttCmdKind::PUBLISH
                            ? (large_publish ? g_mqtt_large_publish_queue
                                             : g_mqtt_publish_queue)
                            : g_mqtt_control_queue;
  if (!queue) return false;
  MqttOutboundCmd* cmd = mqttAllocOutbound(
      kind, topic, payload, payload_len, retain, large_buffer_hold_ms);
  if (!cmd) {
    Serial.println("[MQTT] Outbound allocation failed -> command dropped");
    return false;
  }
  const BaseType_t queued = priority
                                ? xQueueSendToFront(queue, &cmd, 0)
                                : xQueueSend(queue, &cmd, 0);
  if (queued != pdTRUE) {
    heap_caps_free(cmd);
    ++g_mqtt_outbound_dropped;
    Serial.printf("[MQTT] Outbound %s queue full -> dropped (#%u)\n",
                  kind != MqttCmdKind::PUBLISH
                      ? "Control"
                      : (large_publish ? "Large-Publish" : "Publish"),
                  static_cast<unsigned>(g_mqtt_outbound_dropped));
    return false;
  }
  return true;
}

static void purgeOutboundQueue() {
  QueueHandle_t queues[] = {g_mqtt_publish_queue,
                            g_mqtt_large_publish_queue,
                            g_mqtt_control_queue};
  for (QueueHandle_t queue : queues) {
    if (!queue) continue;
    MqttOutboundCmd* cmd = nullptr;
    while (xQueueReceive(queue, &cmd, 0) == pdTRUE) {
      if (cmd) heap_caps_free(cmd);
    }
  }
}

// Use the full 48-bit MAC, not just its low 16 bits. Two panels from
// similar manufacturing batches were observed to collide in those bits:
// HA silently rejected one Zeroconf event as already configured because
// both panels reported the same device_id. Use the MAC as hexadecimal
// text without the former "tab5_lvgl_" prefix.
void buildDeviceId(char* buffer, size_t len) {
  if (!buffer || !len) return;
  uint64_t mac = ESP.getEfuseMac();
  snprintf(buffer, len, "%012llX", (unsigned long long)(mac & 0xFFFFFFFFFFFFULL));
}

static void logMdnsHeap(const char* tag) {
  const uint32_t dma_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  const uint32_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  Serial.printf("[mDNS] %s | DMA free=%u KB | DMA largest=%u KB\n",
                tag, dma_free / 1024, dma_largest / 1024);
}

static void logNetworkHeap(const char* tag) {
  const uint32_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const uint32_t dma_free =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  const uint32_t dma_largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  const uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  Serial.printf("[Network/Mem] %s | Int free=%u KB | Int largest=%u KB | "
                "DMA free=%u KB | DMA largest=%u KB | PSRAM free=%u KB\n",
                tag ? tag : "?",
                static_cast<unsigned>(int_free / 1024),
                static_cast<unsigned>(int_largest / 1024),
                static_cast<unsigned>(dma_free / 1024),
                static_cast<unsigned>(dma_largest / 1024),
                static_cast<unsigned>(psram_free / 1024));
}

static bool parseConfiguredIp(const char* value, IPAddress& out) {
  if (!value || !value[0]) return false;
  String text = value;
  text.trim();
  if (!text.length()) return false;
  return out.fromString(text);
}

static void applyWifiAddressing(const DeviceConfig& cfg) {
  if (!cfg.wifi_static_enabled) {
    WiFi.config(IPAddress(), IPAddress(), IPAddress());
    return;
  }

  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;

  const bool has_ip = parseConfiguredIp(cfg.wifi_static_ip, ip);
  const bool has_gateway = parseConfiguredIp(cfg.wifi_gateway, gateway);
  const bool has_subnet = parseConfiguredIp(cfg.wifi_subnet, subnet);
  const bool has_dns = parseConfiguredIp(cfg.wifi_dns, dns);

  if (has_ip || has_gateway || has_subnet || has_dns) {
    if (has_ip && has_gateway && has_subnet) {
      if (!has_dns) {
        dns = gateway;
      }
      if (WiFi.config(ip, gateway, subnet, dns)) {
        Serial.printf("WiFi: Static IP %s / GW %s / MASK %s / DNS %s\n",
                      ip.toString().c_str(),
                      gateway.toString().c_str(),
                      subnet.toString().c_str(),
                      dns.toString().c_str());
      } else {
        Serial.println("WiFi: Static IP configuration failed, fallback to DHCP");
        WiFi.config(IPAddress(), IPAddress(), IPAddress());
      }
    } else {
      Serial.println("WiFi: Incomplete static IP configuration, fallback to DHCP");
      WiFi.config(IPAddress(), IPAddress(), IPAddress());
    }
  } else {
    WiFi.config(IPAddress(), IPAddress(), IPAddress());
  }
}

bool HomeTilesNetworkManager::isWiredConnected() const {
  return networkTransport.isUsbEthernetConnected() ||
         networkTransport.isNativeEthernetConnected();
}

bool HomeTilesNetworkManager::isWiredLinkUp() const {
  return networkTransport.isUsbEthernetLinkUp() ||
         networkTransport.isNativeEthernetLinkUp();
}

bool HomeTilesNetworkManager::isWifiStationEnabled() const {
  return (static_cast<uint8_t>(WiFi.getMode()) &
          static_cast<uint8_t>(WIFI_MODE_STA)) != 0;
}

bool HomeTilesNetworkManager::ensureWifiStationStarted() {
  wifi_suspended_for_wired = false;
  applyWifiAutoReconnectPolicy();
#if defined(DEVICE_ESP32_S3_RGB_480)
  // Arduino-ESP32 defaults WiFi persistence to true. Set RAM storage before
  // the first WiFi.mode() call, otherwise esp_wifi_set_mode() can update NVS
  // after the S3 RGB scanout is already visible. The old call below happened
  // too late to protect that first transition.
  WiFi.persistent(false);
#endif
  if (!isWifiStationEnabled()) {
    // On these Guition P4 boards, the card is mounted on SDMMC slot 0 while
    // ESP-Hosted starts on slot 1. Mount the card again after that shared host
    // transition so the FAT driver uses the final runtime state.
#if defined(DEVICE_GUITION_JC1060P470C_FAMILY) || \
    defined(DEVICE_GUITION_JC8012P4A1)
    const bool sd_was_mounted = Device::suspendSDCardForNetworkTransition();
#endif
    const bool wifi_started = WiFi.mode(WIFI_STA);
#if defined(DEVICE_GUITION_JC1060P470C_FAMILY) || \
    defined(DEVICE_GUITION_JC8012P4A1)
    if (sd_was_mounted && !Device::resumeSDCardAfterNetworkTransition()) {
      Serial.println("[Network] SD remount after ESP-Hosted start failed");
    }
#endif
    if (!wifi_started) {
      networkTransport.setWifiDriverActive(false);
      Serial.println("WiFi: STA start failed");
      return false;
    }
    logNetworkHeap("after-WiFi.mode");
  }

  networkTransport.setWifiDriverActive(true);
  applyWifiAutoReconnectPolicy();
  WiFi.persistent(false);
  return true;
}

void HomeTilesNetworkManager::stopWifiForWired() {
  if (wifi_suspended_for_wired ||
      !networkTransport.isWifiDriverActive()) {
    return;
  }
  // Block every transport-level WiFi status/IP query before the hosted driver
  // teardown starts. On ESP32-P4, an overlapping RPC can otherwise outlive
  // the queue/semaphore it is using.
  wifi_suspended_for_wired = true;
  networkTransport.setWifiDriverActive(false);

  // ESP-Hosted and the SD card use separate P4 SDMMC slots, but IDF 5.5
  // deinitializes the shared SDMMC host queue when Hosted is torn down.
  // Unmount first and remount immediately afterwards so no stale semaphore
  // remains inside the mounted FAT driver.
  const bool sd_was_mounted = Device::suspendSDCardForNetworkTransition();
  WiFi.setAutoReconnect(false);
  // WIFI_OFF performs one controlled mode query followed by the complete
  // Arduino/ESP-Hosted teardown. Avoid the former disconnect()+getMode()
  // sequence, which issued several RPCs exactly while DMA was being released.
  const bool stopped = WiFi.mode(WIFI_OFF);
  if (sd_was_mounted && !Device::resumeSDCardAfterNetworkTransition()) {
    Serial.println("[Network] WARNING: SD card not mounted after Wi-Fi stop");
  }
  networkTransport.setWifiDriverActive(!stopped);
  wifi_ps_state_known = false;

  if (stopped) {
    Serial.println("[Network] Wi-Fi/SDIO stopped: Ethernet is active");
  } else {
    Serial.println("[Network] Wi-Fi stop for Ethernet failed");
    wifi_suspended_for_wired = false;
  }
}

bool HomeTilesNetworkManager::recoverWifiFromDmaStarvation() {
  if (!mqtt_transport_recovery_requested) return false;

#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!networkTransport.isSdioWifiActive() ||
      networkTransport.isEthernetMode()) {
    mqtt_transport_recovery_requested = false;
    mqtt_preserve_outbound_on_connect = false;
    return false;
  }

  Serial.println(
      "[Network] MQTT TX recovery: rebuilding Wi-Fi/SDIO in a controlled manner");
  if (webAdminServer.isRunning()) webAdminServer.stop();
  stopMdns();
  was_connected = false;

  // As with switching exclusively to Ethernet, unmount SD first.
  // ESP-Hosted and SD use separate slots, but IDF teardown resets their
  // shared SDMMC host queue.
  const bool sd_was_mounted = Device::suspendSDCardForNetworkTransition();
  networkTransport.setWifiDriverActive(false);
  WiFi.setAutoReconnect(false);
  const bool stopped = WiFi.mode(WIFI_OFF);
  if (sd_was_mounted && !Device::resumeSDCardAfterNetworkTransition()) {
    Serial.println(
        "[Network] WARNING: SD card not mounted after Wi-Fi recovery");
  }

  wifi_ps_state_known = false;
  wifi_suspended_for_wired = false;
  mqtt_retry_at = 0;
  mqtt_connect_failures = 0;

  if (!stopped) {
    // The old driver is still active. Release the worker so normal connection
    // backoff remains available; the cooldown bounds another recovery attempt.
    networkTransport.setWifiDriverActive(true);
    mqtt_transport_recovery_requested = false;
    Serial.println("[Network] Wi-Fi/SDIO recovery: driver stop failed");
    return true;
  }

  // Let IDF teardown release all tasks and buffers. Retain the display
  // and its fast SRAM draw buffer.
  delay(100);
  wifi_retry_at = 0;
  connectWifi();
  mqtt_transport_recovery_requested = false;
  Serial.println("[Network] Wi-Fi/SDIO recovery: rebuild initiated");
  return true;
#else
  mqtt_transport_recovery_requested = false;
  mqtt_preserve_outbound_on_connect = false;
  return false;
#endif
}

// ========== Initialization ==========
void HomeTilesNetworkManager::init() {
  networkTransport.begin();
  networkTransport.update();
  transport_generation_seen = networkTransport.generation();
  Serial.println("🌐 Initializing Network Manager...");

  if (!configManager.isConfigured()) {
    Serial.println("⚠️ No network configuration available");
    return;
  }

  const DeviceConfig& cfg = configManager.getConfig();

  wired_was_connected = isWiredConnected();
  wired_link_was_up = isWiredLinkUp();
  if (wired_link_was_up && !wired_was_connected) {
    wired_ip_wait_until = millis() + kWiredDhcpWaitMs;
  }
  if (networkTransport.isEthernetMode()) {
    // Fixed Ethernet mode never starts Wi-Fi/ESP-Hosted in this boot session.
    // Without a cable or adapter the device stays offline. The former Wi-Fi
    // fallback forced both stacks to compete for DMA RAM.
    Serial.printf("[Network] Ethernet mode: %s, Wi-Fi remains off\n",
                  wired_was_connected ? networkTransport.activeName()
                                      : "waiting for link/DHCP");
  } else {
    // Wi-Fi mode starts no Ethernet backend, so the former wait before
    // starting Wi-Fi is unnecessary.
    ensureWifiStationStarted();
    wifi_retry_at = 0;  // Attempt to connect immediately.
  }

  // Build Bridge/request topics once here: they depend only on the fixed
  // eFuse MAC. Rebuilding Strings on each worker reconnect would race
  // loop-task reads such as getBridgeApplyTopic(). init() runs before the
  // worker starts.
  char did[24];
  buildDeviceId(did, sizeof(did));
  String base = "tab5_lvgl/config/";
  base += did;
  bridge_apply_topic_ = base + "/bridge/apply";
  bridge_request_topic_ = base + "/bridge/request";
  history_request_topic_ = base + "/history/request";
  history_response_topic_ = base + "/history/response";
  weather_request_topic_ = base + "/weather/request";
  energy_request_topic_ = base + "/energy/request";
  energy_response_topic_ = base + "/energy/response";
  bridge_icons_topic_ = base + "/bridge/icons";

  mqtt_enabled = configManager.hasMqttConfig();
  if (mqtt_enabled) {
    // MQTT setup precedes worker startup, so direct client access is safe.
    mqtt_client.setClient(net_client);
    mqtt_client.setServer(cfg.mqtt_host, cfg.mqtt_port);
    setMqttBufferSize(mqttNormalBufferSize(), "init");
    mqtt_client.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
      // Receive growth happens inside readPacket, before the queue validates
      // callback bounds. Publish its actual capacity before copying the data.
      mqtt_buffer_size = mqtt_client.getBufferSize();
      mqtt_receive_buffer_floor = mqtt_client.getReceiveBufferSize();
      mqttCallback(topic, payload, length);
    });
  } else {
    Serial.println("MQTT: No configuration available - skipping connection");
  }

  Serial.println("✓ Network Manager initialized");
}

// ========== Connect Wi-Fi ==========
void HomeTilesNetworkManager::connectWifi() {
  wifi_retry_at = millis() + 5000UL;  // Retry in 5s

  // Fixed Ethernet mode keeps Wi-Fi off regardless of retries, reconnects
  // or UI requests. Returning to Wi-Fi requires changing the network mode
  // and restarting.
  if (networkTransport.isEthernetMode()) return;

  // Do not retry a driver marked unresponsive. Calls into partially alive
  // Hosted state can block the loop for seconds on RPC timeouts; update()
  // handles the Ethernet or restart recovery path.
  if (wifi_wedge_latched) return;

  // Any connection attempt, including manual connection, changed credentials
  // or leaving AP mode, clears a previous manual disconnect.
  if (wifi_manual_disconnect) {
    wifi_manual_disconnect = false;
    applyWifiAutoReconnectPolicy();
  }

  networkTransport.update();
  if (isWiredConnected()) {
    stopWifiForWired();
    Serial.printf("WiFi: Not started, %s is active\n",
                  networkTransport.activeName());
    return;
  }
  if (isWiredLinkUp()) {
    // While wired carrier is present, reserve DMA RAM for Ethernet buffers
    // and DHCP startup. Wi-Fi may take over only after kWiredLinkWifiBlockMs
    // without an IP, for example on a network without DHCP, rather than after
    // the shorter DHCP window.
    const uint32_t link_since = wired_link_up_since;
    if (link_since == 0 ||
        (uint32_t)(millis() - link_since) < kWiredLinkWifiBlockMs) {
      Serial.println("WiFi: Not started, Ethernet link active");
      return;
    }
  }

  if (!configManager.isConfigured()) {
    Serial.println("WiFi: No configuration available");
    return;
  }

  const DeviceConfig& cfg = configManager.getConfig();
  if (cfg.wifi_ssid && cfg.wifi_ssid[0]) {
    const uint32_t attempt_started = millis();
    if (!ensureWifiStationStarted()) {
      // STA startup failure means an unresponsive C6 RPC or exhausted memory.
      // Use exponential backoff instead of spending about 5 seconds in a loop
      // RPC timeout every 5 seconds; recover after the failure threshold.
      if (wifi_start_failures < 255) ++wifi_start_failures;
      const uint32_t shift =
          wifi_start_failures < 4 ? wifi_start_failures : 4;
      wifi_retry_at = millis() + (5000UL << shift);  // 10s..80s
      if (wifi_start_failures >= kWifiStartWedgeThreshold) {
        handleWifiDriverWedge();
      }
      return;
    }
    Serial.printf("WiFi: Connecting to %s\n", cfg.wifi_ssid);
    applyWifiAddressing(cfg);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);

    // A slow call can signal a stuck transport even without a formal error.
    // Healthy calls take milliseconds; an unresponsive C6 reaches the
    // 5-second RPC timeout. Only the next fast successful attempt or a live
    // connection in update() clears the counter.
    const uint32_t attempt_ms = millis() - attempt_started;
    if (attempt_ms >= kWifiRpcSlowMs) {
      if (wifi_start_failures < 255) ++wifi_start_failures;
      Serial.printf(
          "WiFi: Connection attempt blocked for %lu ms (suspected RPC timeout %u/%u)\n",
          static_cast<unsigned long>(attempt_ms),
          static_cast<unsigned>(wifi_start_failures),
          static_cast<unsigned>(kWifiStartWedgeThreshold));
      const uint32_t shift =
          wifi_start_failures < 4 ? wifi_start_failures : 4;
      wifi_retry_at = millis() + (5000UL << shift);
      if (wifi_start_failures >= kWifiStartWedgeThreshold) {
        handleWifiDriverWedge();
      }
    } else {
      wifi_start_failures = 0;
    }
  }
}

bool HomeTilesNetworkManager::probeWifiDriverHealth(const char* context) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (wifi_wedge_latched ||
      networkTransport.isEthernetMode() ||
      !networkTransport.isWifiDriverActive()) {
    return !wifi_wedge_latched;
  }

  const uint32_t started = millis();
  (void)WiFi.getMode();
  const uint32_t elapsed = millis() - started;
  if (elapsed < kWifiRpcSlowMs) return true;

  wifi_start_failures = kWifiStartWedgeThreshold;
  Serial.printf(
      "[Network] Wi-Fi liveness probe blocked for %lu ms (%s): "
      "ESP-Hosted/C6 is not responding\n",
      static_cast<unsigned long>(elapsed),
      context && context[0] ? context : "without context");
  handleWifiDriverWedge(context);
  return false;
#else
  (void)context;
  return true;
#endif
}

void HomeTilesNetworkManager::handleWifiDriverWedge(const char* context) {
  if (wifi_wedge_latched) return;
  wifi_wedge_latched = true;

  const bool wired = isWiredLinkUp();
  String detail;
  detail.reserve(512);
  detail += "ESP-Hosted RPC timeout to C6";
  if (context && context[0]) {
    detail += " (";
    detail += context;
    detail += ')';
  }
  detail += '\n';
  detail += "Wedge indicators ";
  detail += wifi_start_failures;
  detail += '/';
  detail += kWifiStartWedgeThreshold;
  detail += '\n';
  detail += "Uptime ";
  detail += millis() / 1000;
  detail += " s | Ethernet link ";
  detail += wired ? "present" : "missing";
  detail += '\n';
  char mem[96];
  snprintf(mem, sizeof(mem), "mem int=%uKB largest=%uKB dma_largest=%uKB\n",
           static_cast<unsigned>(
               heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           static_cast<unsigned>(
               heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
           static_cast<unsigned>(heap_caps_get_largest_free_block(
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) /
                                 1024));
  detail += mem;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (hometiles_sdio_get_rx_diag) {
    uint32_t last_raw = 0;
    uint32_t consumed = 0;
    uint32_t last_intr = 0;
    uint32_t legal_fffff = 0;
    uint32_t pending_drains = 0;
    uint32_t alloc_retries = 0;
    uint32_t bus_faults = 0;
    hometiles_sdio_get_rx_diag(
        &last_raw, &consumed, &last_intr, &legal_fffff,
        &pending_drains, &alloc_retries, &bus_faults);
    char sdio_diag[224];
    snprintf(
        sdio_diag, sizeof(sdio_diag),
        "sdio raw=0x%08lX consumed=%lu intr=0x%08lX "
        "legal_fffff=%lu pending_drains=%lu alloc_retries=%lu "
        "bus_faults=%lu\n",
        static_cast<unsigned long>(last_raw),
        static_cast<unsigned long>(consumed),
        static_cast<unsigned long>(last_intr),
        static_cast<unsigned long>(legal_fffff),
        static_cast<unsigned long>(pending_drains),
        static_cast<unsigned long>(alloc_retries),
        static_cast<unsigned long>(bus_faults));
    detail += sdio_diag;
    Serial.printf("[Network] %s", sdio_diag);
  } else {
    detail += "sdio diagnostics unavailable (stock/old hosted object)\n";
  }
#endif
  detail += camera_stream_is_active()
                ? "camera_stream=active\n"
                : "camera_stream=inactive\n";
  detail += wired
                ? "Continuing over Ethernet; Wi-Fi disabled until restart\n"
                : "Safe restart follows (also resets the C6)\n";
  CrashLog::appendNetworkWedgeReport(detail);

  Serial.printf("[Network] Wi-Fi driver is no longer responding - %s\n",
                wired ? "continuing over Ethernet (Wi-Fi off until restart)"
                      : "safe restart");
  if (!wired) {
    if (millis() >= kWedgeRestartMinUptimeMs) {
      Serial.flush();
      BoardHAL::prepareForRestart();
      delay(500);
      BoardHAL::restart();
    }
    // Too early for an automatic restart: keep the failure latched. update()
    // restarts after the minimum uptime if no link is available.
  }
}

// ========== Manual Wi-Fi disconnect from the Wi-Fi popup ==========
void HomeTilesNetworkManager::disconnectWifiManual() {
  wifi_manual_disconnect = true;
  if (networkTransport.activeKind() == NetworkTransportKind::Wifi &&
      isMqttConnected()) {
    disconnectMqtt();
  }
  WiFi.setAutoReconnect(false);
  if (isWifiStationEnabled()) WiFi.disconnect();
  Serial.println("WiFi: Manually disconnected (no automatic reconnect until connect/restart)");
}

// ========== Connect MQTT (worker only) ==========
void HomeTilesNetworkManager::connectMqtt() {
  if (!mqtt_enabled) return;
  mqtt_retry_at = millis() + 3000UL;  // Retry in 3s

  if (!networkTransport.isConnected()) return;
  if (mqtt_large_until == 0 && mqtt_client.getBufferSize() < mqttNormalBufferSize()) {
    setMqttBufferSize(mqttNormalBufferSize(), "connect");
  }

  if (!configManager.isConfigured()) {
    Serial.println("MQTT: No configuration available");
    return;
  }

  const DeviceConfig& cfg = configManager.getConfig();

  char client_id[CONFIG_MQTT_CLIENT_ID_MAX];
  if (cfg.mqtt_client_id[0]) {
    snprintf(client_id, sizeof(client_id), "%s", cfg.mqtt_client_id);
  } else {
    const unsigned long long mac = static_cast<unsigned long long>(ESP.getEfuseMac() & 0xFFFFFFFFFFFFULL);
    snprintf(client_id, sizeof(client_id), "Tab5_LVGL-%012llX", mac);
  }

  Serial.printf("MQTT: Connecting to %s:%u as %s\n", cfg.mqtt_host, cfg.mqtt_port, client_id);

  const char* stat_topic = mqttTopics.topic(TopicKey::STAT_CONN);
  if (!stat_topic || !*stat_topic) {
    stat_topic = "tab5/stat/connected";
  }

  bool ok = false;
  if (cfg.mqtt_user && cfg.mqtt_user[0]) {
    ok = mqtt_client.connect(client_id, cfg.mqtt_user, cfg.mqtt_pass,
                             stat_topic, 0, true, "0");
  } else {
    ok = mqtt_client.connect(client_id, nullptr, nullptr,
                             stat_topic, 0, true, "0");
  }

  if (!ok) {
    // Use exponential backoff for an unavailable broker or stuck Wi-Fi stack.
    // Repeated blocking connects every 3 seconds consumed worker/loop time
    // and internal RAM in the 2026-07-16 field test: continuous state=-2
    // failures drove the heap minimum down to 33 KB.
    if (mqtt_connect_failures < 255) ++mqtt_connect_failures;
    const uint32_t shift =
        mqtt_connect_failures < 5 ? mqtt_connect_failures : 5;
    mqtt_retry_at = millis() + (3000UL << shift);  // 6s..96s
    Serial.printf("MQTT: Connection failed, state=%d (retry in %lus)\n",
                  mqtt_client.state(),
                  static_cast<unsigned long>((3000UL << shift) / 1000));
    return;
  }

  mqtt_connect_failures = 0;
  Serial.println("✓ MQTT connected");
  mqtt_connected_at = millis();
  logNetworkHeap("after-MQTT-connect");

  // Publish status and subscribe to reply topics directly. Client access
  // is safe because connectMqtt() runs only on the owning worker.
  mqtt_client.publish(stat_topic, "1", true);
  const char* ip_topic = mqttTopics.topic(TopicKey::STAT_IP);
  if (ip_topic && *ip_topic) {
    mqtt_client.publish(
        ip_topic, networkTransport.localIP().toString().c_str(), true);
  }
  if (!bridge_apply_topic_.isEmpty()) {
    if (mqtt_client.subscribe(bridge_apply_topic_.c_str())) {
      Serial.printf("[MQTT] Listening for bridge config on %s\n",
                    bridge_apply_topic_.c_str());
    } else {
      Serial.printf("[MQTT] ERROR subscribing to bridge config on %s\n",
                    bridge_apply_topic_.c_str());
    }
  }
  if (!history_response_topic_.isEmpty()) {
    mqtt_client.subscribe(history_response_topic_.c_str());
    Serial.printf("[MQTT] Listening for history responses on %s\n", history_response_topic_.c_str());
  }
  if (!energy_response_topic_.isEmpty()) {
    mqtt_client.subscribe(energy_response_topic_.c_str());
    Serial.printf("[MQTT] Listening for energy responses on %s\n", energy_response_topic_.c_str());
  }
  if (!bridge_icons_topic_.isEmpty()) {
    mqtt_client.subscribe(bridge_icons_topic_.c_str());
    Serial.printf("[MQTT] Listening for icon updates on %s\n", bridge_icons_topic_.c_str());
  }

  // Discard stale interactive commands after an ordinary disconnect.
  // During targeted DMA recovery, retain accepted requests: they have not
  // reached the broker and can run after reconnection using the reply
  // topics subscribed above.
  if (mqtt_preserve_outbound_on_connect) {
    mqtt_preserve_outbound_on_connect = false;
    Serial.println("[MQTT] DMA recovery: pending requests preserved");
  } else {
    purgeOutboundQueue();
  }

  ++g_editable_connection_generation;
  mqtt_connected_flag = true;

  // The loop task starts the application layer via mqttServicePostConnect():
  // subscriptions, discovery, device settings and snapshot code access
  // flash, LVGL and battery I2C, which must not run on the worker. Their
  // publishes/subscribes return here through the outbound queue.
  mqtt_post_connect_ready_at = mqtt_connected_at + kMqttPostConnectQuietMs;
  mqtt_post_connect_pending = true;

  // Bridge requests are handled by the background refresh. The retained
  // device announcement is queued later by mqttServicePostConnect(); its
  // large-buffer lane waits until the startup RAM storm has passed.
}

// ========== Single-Owner MQTT: Worker ==========
void HomeTilesNetworkManager::beginMqttWorker() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  Serial.println(
      "[Network] ESP-Hosted SDIO buffers: DMA-capable PSRAM preferred, "
      "internal DMA fallback active");
#endif
  if (!g_mqtt_publish_queue) {
    g_mqtt_publish_queue =
        xQueueCreate(kMqttPublishQueueDepth, sizeof(MqttOutboundCmd*));
    if (!g_mqtt_publish_queue) {
      Serial.println("[MQTT] Could not create outbound publish queue");
    }
  }
  if (!g_mqtt_large_publish_queue) {
    g_mqtt_large_publish_queue =
        xQueueCreate(kMqttLargePublishQueueDepth, sizeof(MqttOutboundCmd*));
    if (!g_mqtt_large_publish_queue) {
      Serial.println(
          "[MQTT] Could not create outbound large-publish queue");
    }
  }
  if (!g_mqtt_control_queue) {
    g_mqtt_control_queue =
        xQueueCreate(kMqttControlQueueDepth, sizeof(MqttOutboundCmd*));
    if (!g_mqtt_control_queue) {
      Serial.println("[MQTT] Could not create outbound control queue");
    }
  }
  initMqttDmaReserve();
}

size_t HomeTilesNetworkManager::mqttDmaReserveBytes() const {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  return g_mqtt_dma_reserve_held ? kMqttDmaReserveBytes : 0;
#else
  return 0;
#endif
}

// Worker task body: the only post-init owner of mqtt_client.
void HomeTilesNetworkManager::serviceMqttWorker() {
  // Handle reconfiguration before the mqtt_enabled gate because it updates
  // that flag live: initial Admin configuration, a cleared host or a new
  // host. Other requests stay behind the gate because they apply only to
  // a previously enabled MQTT configuration.
  if (mqtt_reconfig_requested) {
    mqtt_reconfig_requested = false;
    if (mqtt_client.connected()) {
      const char* stat_topic = mqttTopics.topic(TopicKey::STAT_CONN);
      if (stat_topic && *stat_topic) {
        // Publish a clean "0" before disconnecting. PubSubClient sends a normal
        // MQTT DISCONNECT, which does not trigger the last will; without this
        // status update the Bridge would keep showing the device as connected.
        mqtt_client.publish(stat_topic, "0", true);
      }
      mqtt_client.disconnect();
      Serial.println("[MQTT] Disconnect for reconfiguration");
    }
    purgeOutboundQueue();
    mqtt_post_connect_pending = false;
    mqtt_post_connect_ready_at = 0;
    mqtt_large_until = 0;
    if (mqtt_buffer_size > mqttNormalBufferSize()) {
      setMqttBufferSize(mqttNormalBufferSize(), "reconfig");
    }
    mqtt_connected_flag = false;

    mqtt_enabled = configManager.hasMqttConfig();
    if (mqtt_enabled) {
      const DeviceConfig& cfg = configManager.getConfig();
      mqtt_client.setClient(net_client);
      mqtt_client.setServer(cfg.mqtt_host, cfg.mqtt_port);
      mqtt_client.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
        mqtt_buffer_size = mqtt_client.getBufferSize();
        mqtt_receive_buffer_floor = mqtt_client.getReceiveBufferSize();
        mqttCallback(topic, payload, length);
      });
      mqtt_retry_at = 0;  // Connect immediately on the next iteration.
      mqtt_connect_failures = 0;  // Fresh transport, fresh backoff.
      Serial.println("[MQTT] Reconfigure: new settings applied");
    } else {
      Serial.println("[MQTT] Reconfigure: no host configured, remaining disconnected");
    }
    return;
  }

  if (mqtt_transport_recovery_requested) return;
  if (!mqtt_enabled) return;

  // Process request flags even while suspended so restoreMqttBufferNormal()
  // can wake the worker after an aborted OTA.
  if (mqtt_ota_prep_requested) {
    mqtt_large_until = 0;
    if (mqtt_client.connected()) {
      mqtt_client.disconnect();
      Serial.println("[OTA] MQTT disconnected for OTA");
    }
    setMqttBufferSize(kMqttBufferOta, "ota");
    // This reserve protects MQTT publishes only. While OTA suspends the
    // worker, release the contiguous 12 KB for ESP-Hosted/HTTP RX. Normal
    // restoration allocates it again automatically.
    releaseMqttDmaReserve("ota");
    mqtt_connected_flag = false;
    mqtt_suspended = true;  // Do not reconnect or pump loop() during OTA.
    mqtt_ota_prep_requested = false;
    return;
  }
  if (mqtt_restore_normal_requested) {
    mqtt_restore_normal_requested = false;
    mqtt_large_until = 0;
    setMqttBufferSize(mqttNormalBufferSize(), "normal");
    mqtt_suspended = false;  // Resume after aborted OTA.
    return;
  }
  if (mqtt_disconnect_requested) {
    if (mqtt_client.connected()) {
      mqtt_client.disconnect();
      Serial.println("[MQTT] Disconnect requested (hotspot mode)");
    }
    purgeOutboundQueue();
    mqtt_post_connect_pending = false;
    mqtt_post_connect_ready_at = 0;
    mqtt_large_until = 0;
    if (mqtt_buffer_size > mqttNormalBufferSize()) {
      setMqttBufferSize(mqttNormalBufferSize(), "disconnect");
    }
    mqtt_connected_flag = false;
    mqtt_disconnect_requested = false;
    return;
  }
  if (mqtt_suspended) return;

  if (!networkTransport.isConnected()) {
    if (mqtt_connected_flag) mqtt_connected_flag = false;
    mqtt_post_connect_pending = false;
    mqtt_post_connect_ready_at = 0;
    return;
  }

  const uint32_t now_ms = millis();
  const uint32_t hold_until = mqtt_reconnect_hold_until;
  if (hold_until != 0 && (int32_t)(now_ms - hold_until) < 0) {
    if (mqtt_client.connected()) {
      mqtt_client.disconnect();
      Serial.println("[MQTT] Disconnect during reconnect quiet period");
    }
    if (mqtt_connected_flag) mqtt_connected_flag = false;
    mqtt_post_connect_pending = false;
    mqtt_post_connect_ready_at = 0;
    return;
  }
  if (hold_until != 0) {
    mqtt_reconnect_hold_until = 0;
  }

  serviceBufferHousekeeping(now_ms);

  if (!mqtt_client.connected()) {
    if (mqtt_connected_flag) {
      mqtt_connected_flag = false;
      Serial.println("[MQTT] Connection lost");
    }
    if ((int32_t)(now_ms - mqtt_retry_at) >= 0) {
      connectMqtt();
    }
    return;
  }

  // Drain outgoing commands before pumping the socket. Large-buffer requests
  // travel with the publish command and become active immediately before
  // its transmission.
  const bool startup_storm =
      mqtt_connected_at != 0 && (uint32_t)(now_ms - mqtt_connected_at) < kMqttStormWindowMs;
  drainOutboundQueues(startup_storm ? kMqttOutboundDrainStorm
                                    : kMqttOutboundDrainNormal);
  mqtt_client.loop();
  mqtt_buffer_size = mqtt_client.getBufferSize();
  mqtt_receive_buffer_floor = mqtt_client.getReceiveBufferSize();
  if (!mqtt_client.connected()) {
    Serial.printf("[MQTT] Connection lost in loop, state=%d\n",
                  mqtt_client.state());
    mqtt_connected_flag = false;
  }
}

void HomeTilesNetworkManager::drainOutboundQueues(uint8_t max_commands) {
  if (max_commands == 0) return;

  const uint32_t now_ms = millis();
  size_t dma_largest = static_cast<size_t>(-1);
  bool control_quiet = false;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (networkTransport.isSdioWifiActive()) {
    control_quiet =
        g_mqtt_sdio_control_quiet_until != 0 &&
        static_cast<int32_t>(now_ms - g_mqtt_sdio_control_quiet_until) < 0;
    if (!control_quiet) g_mqtt_sdio_control_quiet_until = 0;
    // Release the reserve under acute pressure even without a waiting large
    // request, giving the currently active SDIO RX path immediate headroom.
    dma_largest = serviceMqttDmaHeadroom(now_ms);
  }
#endif

  auto log_dma_wait = [&](const char* lane, size_t largest) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    if ((uint32_t)(now_ms - g_mqtt_last_tx_guard_log_ms) >= 2000) {
      g_mqtt_last_tx_guard_log_ms = now_ms;
      Serial.printf("[MQTT] %s waiting: DMA largest only %u KB\n",
                    lane,
                    static_cast<unsigned>(largest / 1024));
    }
#else
    (void)lane;
    (void)largest;
#endif
  };

  // Control commands retain priority and P4 sends them individually with
  // spacing. Publishes in their separate queue can continue during that
  // guard interval.
  MqttOutboundCmd* cmd = nullptr;
  const bool control_waiting =
      g_mqtt_control_queue &&
      xQueuePeek(g_mqtt_control_queue, &cmd, 0) == pdTRUE;
  if (control_waiting && !control_quiet) {
    if (xQueueReceive(g_mqtt_control_queue, &cmd, 0) == pdTRUE) {
      bool ok = false;
      const char* verb = "control";
      if (cmd) {
        if (cmd->kind == MqttCmdKind::SUBSCRIBE) {
          verb = "subscribe";
          ok = mqtt_client.subscribe(cmd->topic);
        } else if (cmd->kind == MqttCmdKind::UNSUBSCRIBE) {
          verb = "unsubscribe";
          ok = mqtt_client.unsubscribe(cmd->topic);
        }
        if (!ok) {
          Serial.printf("[MQTT] Worker: %s '%s' failed\n",
                        verb, cmd->topic);
        }
        heap_caps_free(cmd);
      }
#if defined(CONFIG_IDF_TARGET_ESP32P4)
      if (networkTransport.isSdioWifiActive()) {
        g_mqtt_sdio_control_quiet_until = millis() + kMqttSdioControlQuietMs;
      }
#endif
      return;
    }
  }

  // After subscribe/unsubscribe, send at most one publish before calling
  // mqtt_client.loop() to receive an immediate retained response. Keep the
  // SDIO protection without stopping all publishes for the full 50 ms.
  const uint8_t publish_limit = control_quiet ? 1 : max_commands;
  const bool startup_storm =
      mqtt_connected_at != 0 &&
      (uint32_t)(millis() - mqtt_connected_at) < kMqttStormWindowMs;
  MqttOutboundCmd* large_peek = nullptr;
  const bool large_waiting =
      !startup_storm && g_mqtt_large_publish_queue &&
      xQueuePeek(g_mqtt_large_publish_queue, &large_peek, 0) == pdTRUE;
  // Outside the startup burst, reserve one slot for large requests in a
  // normal 12-command drain. Continuous small status publishes must not
  // starve history, energy or Bridge requests.
  const uint8_t normal_limit =
      large_waiting && publish_limit > 1 ? publish_limit - 1 : publish_limit;
  uint32_t drained = 0;
  if (g_mqtt_publish_queue) {
    while (drained < normal_limit &&
           xQueueReceive(g_mqtt_publish_queue, &cmd, 0) == pdTRUE) {
      if (!cmd) continue;

      const size_t topic_length = strlen(cmd->topic);
      if (topic_length >= 11 && strcmp(cmd->topic + topic_length - 11, "/cmnd/value") == 0 &&
          cmd->editable_connection_generation != g_editable_connection_generation.load()) {
        heap_caps_free(cmd);
        ++drained;
        continue;
      }

      // Small interactive publishes need no 32 KB receive buffer and remain
      // sendable with fragmented DMA RAM. Waiting history, energy or Bridge
      // requests must never block this queue.
      const bool ok = mqtt_client.publish(
          cmd->topic, cmd->payload, cmd->payload_len, cmd->retain);
      if (!ok) {
        Serial.printf("[MQTT] Worker: publish '%s' failed\n", cmd->topic);
      }
      heap_caps_free(cmd);

      // Large publish bursts must also give the idle task time to run.
      if ((++drained & 0x07) == 0) vTaskDelay(1);
    }
  }

  if (!large_waiting || drained >= publish_limit) {
    g_mqtt_dma_low_since = 0;
    return;
  }

  // Send large-response requests only after the retained-message burst.
  // They wait in their own queue while normal interactive commands continue.
  while (drained < publish_limit &&
         xQueuePeek(g_mqtt_large_publish_queue, &cmd, 0) == pdTRUE) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    // Check DMA headroom before enlarging the receive buffer. Insufficient
    // reserve stops only large requests; the normal queue has already been
    // serviced above and remains responsive.
    if (networkTransport.isSdioWifiActive()) {
      dma_largest = serviceMqttDmaHeadroom(now_ms);
      if (dma_largest < kMqttMinDmaLargestBeforeTx) {
        log_dma_wait("Large-Publish", dma_largest);
        if (g_mqtt_dma_low_since == 0) {
          g_mqtt_dma_low_since = now_ms;
        }
        const bool recovery_allowed =
            g_mqtt_dma_recovery_cooldown_until == 0 ||
            static_cast<int32_t>(
                now_ms - g_mqtt_dma_recovery_cooldown_until) >= 0;
        if (recovery_allowed &&
            (uint32_t)(now_ms - g_mqtt_dma_low_since) >=
                kMqttDmaRecoveryWaitMs) {
          // Never tear down ESP-Hosted while the camera task owns an active
          // HTTP socket. The teardown invalidates that socket underneath the
          // other core and caused an instruction-access panic. The camera
          // path has its own DMA guard and will close first if memory remains
          // scarce; normal MQTT publishes continue through their own lane.
          if (camera_stream_is_active()) {
            log_dma_wait("Recovery waiting for camera to stop", dma_largest);
            return;
          }
          Serial.printf(
              "[MQTT] DMA starvation for %u ms: Wi-Fi/SDIO recovery\n",
              static_cast<unsigned>(now_ms - g_mqtt_dma_low_since));
          // Preserve single ownership: the worker closes its own client before
          // the loop task may tear down the driver.
          if (mqtt_client.connected()) mqtt_client.disconnect();
          mqtt_connected_flag = false;
          mqtt_post_connect_pending = false;
          mqtt_post_connect_ready_at = 0;
          mqtt_large_until = 0;
          mqtt_preserve_outbound_on_connect = true;
          g_mqtt_dma_low_since = 0;
          g_mqtt_dma_recovery_cooldown_until =
              now_ms + kMqttDmaRecoveryCooldownMs;
          mqtt_transport_recovery_requested = true;
        }
        return;
      }
    }
#endif
    g_mqtt_dma_low_since = 0;

    cmd = nullptr;
    if (xQueueReceive(g_mqtt_large_publish_queue, &cmd, 0) != pdTRUE) return;
    if (!cmd) continue;

    // Grow the buffer only now, when DMA reserve is sufficient and the
    // command is about to be sent.
    if (mqtt_buffer_size < kMqttBufferLarge &&
        !setMqttBufferSize(kMqttBufferLarge, "queued-publish")) {
      ++g_mqtt_outbound_dropped;
      Serial.printf(
          "[MQTT] Large publish dropped: buffer unavailable (#%u)\n",
          static_cast<unsigned>(g_mqtt_outbound_dropped));
      heap_caps_free(cmd);
      return;
    }

    mqtt_large_until = millis() + cmd->large_buffer_hold_ms;
    const bool ok = mqtt_client.publish(
        cmd->topic, cmd->payload, cmd->payload_len, cmd->retain);
    if (!ok) {
      Serial.printf("[MQTT] Worker: publish '%s' failed\n", cmd->topic);
    }
    heap_caps_free(cmd);

    if ((++drained & 0x07) == 0) vTaskDelay(1);
  }
}

// Keep the former update() receive-buffer grow/shrink policy on the
// worker because setBufferSize() accesses the client.
void HomeTilesNetworkManager::serviceBufferHousekeeping(uint32_t now_ms) {
  const uint32_t large_until = mqtt_large_until;
  if (large_until == 0) {
    // Without an active large-response window, match the normal buffer to
    // the media configuration: adding/removing media tiles selects 24 KB
    // or the base size. Leave the 1 KB OTA buffer alone. Growth waits for
    // the startup burst to finish; shrinking is safe immediately.
    const uint16_t normal_size = mqttNormalBufferSize();
    if (mqtt_buffer_size != 0 && mqtt_buffer_size != kMqttBufferOta &&
        mqtt_buffer_size != normal_size &&
        (mqtt_buffer_size > normal_size ||
         mqtt_connected_at == 0 ||
         (uint32_t)(now_ms - mqtt_connected_at) >= kMqttStormWindowMs)) {
      setMqttBufferSize(normal_size, "media-config");
    }
    return;
  }
  if ((int32_t)(now_ms - large_until) >= 0) {
    mqtt_large_until = 0;
    setMqttBufferSize(mqttNormalBufferSize(), "normal");
  } else if (mqtt_buffer_size < kMqttBufferLarge &&
             (mqtt_connected_at == 0 ||
              (uint32_t)(now_ms - mqtt_connected_at) >= kMqttStormWindowMs)) {
    // Grow immediately outside the startup burst. Otherwise defer growth
    // until the window ends, as the former update() "large-deferred" path did.
    setMqttBufferSize(kMqttBufferLarge, "large");
  }
}

// ========== Single-owner MQTT: API for other tasks ==========
bool HomeTilesNetworkManager::mqttEnqueuePublish(const char* topic, const char* payload, bool retain) {
  const size_t len = payload ? strlen(payload) : 0;
  return enqueueOutboundCmd(MqttCmdKind::PUBLISH, topic,
                            reinterpret_cast<const uint8_t*>(payload), len, retain);
}

bool HomeTilesNetworkManager::mqttEnqueuePublish(const char* topic, const uint8_t* payload,
                                            size_t length, bool retain) {
  return enqueueOutboundCmd(MqttCmdKind::PUBLISH, topic, payload, length, retain);
}

bool HomeTilesNetworkManager::mqttEnqueuePublishPriority(const char* topic,
                                                    const char* payload,
                                                    bool retain) {
  const size_t len = payload ? strlen(payload) : 0;
  return enqueueOutboundCmd(MqttCmdKind::PUBLISH, topic,
                            reinterpret_cast<const uint8_t*>(payload), len,
                            retain, true);
}

bool HomeTilesNetworkManager::mqttEnqueuePublishWithLargeBuffer(
    const char* topic,
    const char* payload,
    bool retain,
    uint32_t hold_ms,
    bool priority) {
  const size_t len = payload ? strlen(payload) : 0;
  if (hold_ms == 0) hold_ms = 15000;
  return enqueueOutboundCmd(MqttCmdKind::PUBLISH, topic,
                            reinterpret_cast<const uint8_t*>(payload), len,
                            retain, priority, hold_ms);
}

bool HomeTilesNetworkManager::mqttEnqueueSubscribe(const char* topic) {
  return enqueueOutboundCmd(MqttCmdKind::SUBSCRIBE, topic, nullptr, 0, false);
}

bool HomeTilesNetworkManager::mqttEnqueueUnsubscribe(const char* topic) {
  return enqueueOutboundCmd(MqttCmdKind::UNSUBSCRIBE, topic, nullptr, 0, false);
}

bool HomeTilesNetworkManager::consumeMqttPostConnectPending() {
  if (!mqtt_post_connect_pending) return false;
  if (!mqtt_connected_flag) {
    mqtt_post_connect_pending = false;
    mqtt_post_connect_ready_at = 0;
    return false;
  }
  const uint32_t ready_at = mqtt_post_connect_ready_at;
  if (ready_at != 0 && (int32_t)(millis() - ready_at) < 0) {
    return false;
  }
  mqtt_post_connect_pending = false;  // The loop task is the only consumer.
  mqtt_post_connect_ready_at = 0;
  return true;
}

void HomeTilesNetworkManager::disconnectMqtt() {
  if (!mqtt_enabled) return;
  mqtt_disconnect_requested = true;
  // The worker checks this flag about every 2 ms. Hotspot entry is rare
  // and can tolerate a short bounded wait.
  for (int i = 0; i < 100 && mqtt_disconnect_requested; ++i) {
    delay(5);
  }
  if (mqtt_disconnect_requested) {
    Serial.println("[MQTT] WARNING: Worker did not acknowledge disconnect request");
  }
}

void HomeTilesNetworkManager::requestMqttReconfigure() {
  // Do not copy disconnectMqtt()'s "if (!mqtt_enabled) return;" gate.
  // The worker must determine mqtt_enabled here, including the first
  // MQTT configuration when it was previously false.
  mqtt_reconfig_requested = true;
  for (int i = 0; i < 100 && mqtt_reconfig_requested; ++i) {
    delay(5);
  }
  if (mqtt_reconfig_requested) {
    Serial.println("[MQTT] WARNING: Worker did not acknowledge reconfigure request");
  }
}

void HomeTilesNetworkManager::prepareMqttForOta() {
  if (!mqtt_enabled) return;
  mqtt_ota_prep_requested = true;
  for (int i = 0; i < 100 && mqtt_ota_prep_requested; ++i) {
    delay(5);
  }
  if (mqtt_ota_prep_requested) {
    Serial.println("[OTA] WARNING: MQTT worker did not acknowledge OTA preparation");
  }
}

void HomeTilesNetworkManager::deferMqttReconnect(uint32_t hold_ms) {
  if (!mqtt_enabled) return;
  if (hold_ms == 0) hold_ms = 6000;
  mqtt_reconnect_hold_until = millis() + hold_ms;
  mqtt_post_connect_pending = false;
  mqtt_post_connect_ready_at = 0;
  mqtt_large_until = 0;
  purgeOutboundQueue();
  Serial.printf("[MQTT] Reconnect paused for %u ms\n",
                static_cast<unsigned>(hold_ms));
}

// ========== MQTT status ==========
uint16_t HomeTilesNetworkManager::mqttNormalBufferSize() const {
  const uint16_t configured = mqtt_media_buffer_needed ? kMqttBufferMedia : kMqttBufferNormal;
  // Avoid reallocating for every repeated retained state larger than the base
  // buffer. The vendored parser caps this high-water mark at 65,535 bytes.
  return mqtt_receive_buffer_floor > configured ? mqtt_receive_buffer_floor : configured;
}

bool HomeTilesNetworkManager::setMqttBufferSize(uint16_t size, const char* reason) {
  if (size == 0) return false;
  const uint16_t before = mqtt_client.getBufferSize();
  if (before == size) {
    mqtt_buffer_size = size;
    return true;
  }

  if (!mqtt_client.setBufferSize(size)) {
    Serial.printf("[MQTT] Buffer resize failed: %u -> %u bytes (%s)\n",
                  static_cast<unsigned>(before),
                  static_cast<unsigned>(size),
                  reason ? reason : "?");
    return false;
  }

  mqtt_buffer_size = mqtt_client.getBufferSize();
  Serial.printf("[MQTT] Buffer: %u -> %u bytes (%s, %s)\n",
                static_cast<unsigned>(before),
                static_cast<unsigned>(mqtt_buffer_size),
                reason ? reason : "?",
                mqtt_client.bufferInExternalRam() ? "PSRAM" : "internal");
  return true;
}

void HomeTilesNetworkManager::restoreMqttBufferNormal() {
  // Request a worker action instead of touching the client directly. The
  // worker restores the buffer and clears any OTA suspension; the caller
  // is restoreDisplayAfterOtaFailure().
  mqtt_restore_normal_requested = true;
}

// ========== Network status ==========
bool HomeTilesNetworkManager::isNetworkConnected() const {
  return networkTransport.isConnected();
}

bool HomeTilesNetworkManager::isWifiConnected() const {
  return networkTransport.isWifiConnected();
}

// ========== Send telemetry ==========
void HomeTilesNetworkManager::publishTelemetry() {
  if (!isMqttConnected()) return;

  uint32_t now = millis();
  if (now - last_telemetry > 30000UL) {  // 30 seconds
    last_telemetry = now;
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(now / 1000UL));
    const char* tele_topic = mqttTopics.topic(TopicKey::TELE_UP);
    if (tele_topic && *tele_topic) {
      mqttEnqueuePublish(tele_topic, buf, true);
    }
    mqttPublishHomeSnapshot();
  }
}

void HomeTilesNetworkManager::publishBridgeConfig() {
  if (!isMqttConnected()) return;
  if (!configManager.isConfigured()) return;

  const DeviceConfig& cfg = configManager.getConfig();
  char did[24];
  buildDeviceId(did, sizeof(did));
  String payload = haBridgeConfig.buildJsonPayload(did, cfg.mqtt_base_topic, cfg.ha_prefix);
  if (payload.isEmpty()) return;

  String topic = "tab5_lvgl/config/";
  topic += did;
  topic += "/bridge";
  const size_t packet_estimate = payload.length() + topic.length() + 16;
  if (packet_estimate > kMqttBufferLarge) {
    Serial.printf("[Network] Bridge config too large for MQTT buffer: %u > %u bytes\n",
                  static_cast<unsigned>(packet_estimate),
                  static_cast<unsigned>(kMqttBufferLarge));
  }
  mqttEnqueuePublishWithLargeBuffer(
      topic.c_str(), payload.c_str(), true, 15000);
  Serial.println("[Network] Home Assistant Bridge configuration published");
}

const char* HomeTilesNetworkManager::getBridgeApplyTopic() const {
  return bridge_apply_topic_.length() ? bridge_apply_topic_.c_str() : nullptr;
}

void HomeTilesNetworkManager::publishBridgeRequest(bool force) {
  if (!isMqttConnected()) return;
  if (bridge_request_topic_.isEmpty()) return;
  mqttEnqueuePublishWithLargeBuffer(
      bridge_request_topic_.c_str(), force ? "force" : "", false, 30000);
  Serial.printf("[Network] Home Assistant Bridge refresh requested%s\n",
                force ? " (forced)" : "");
}

const char* HomeTilesNetworkManager::getBridgeRequestTopic() const {
  return bridge_request_topic_.length() ? bridge_request_topic_.c_str() : nullptr;
}

const char* HomeTilesNetworkManager::getHistoryRequestTopic() const {
  return history_request_topic_.length() ? history_request_topic_.c_str() : nullptr;
}

const char* HomeTilesNetworkManager::getHistoryResponseTopic() const {
  return history_response_topic_.length() ? history_response_topic_.c_str() : nullptr;
}

const char* HomeTilesNetworkManager::getWeatherRequestTopic() const {
  return weather_request_topic_.length() ? weather_request_topic_.c_str() : nullptr;
}

const char* HomeTilesNetworkManager::getEnergyRequestTopic() const {
  return energy_request_topic_.length() ? energy_request_topic_.c_str() : nullptr;
}

const char* HomeTilesNetworkManager::getEnergyResponseTopic() const {
  return energy_response_topic_.length() ? energy_response_topic_.c_str() : nullptr;
}

const char* HomeTilesNetworkManager::getBridgeIconsTopic() const {
  return bridge_icons_topic_.length() ? bridge_icons_topic_.c_str() : nullptr;
}

// ========== mDNS-Advertising ==========
// Add Zeroconf discovery for the HA Bridge before MQTT credentials exist.
// Run only while MQTT is unconfigured, as update() enforces. Brief broker
// outages must not repeatedly restart mDNS: on P4, ESP-IDF does not always
// return all reserved internal/DMA blocks in each cycle. Start only after
// webAdminServer.start(); a blocked or failed MDNS.begin() must not delay
// the working Admin interface.
void HomeTilesNetworkManager::startMdns() {
  if (mdns_active) return;

  char did[24];
  buildDeviceId(did, sizeof(did));

  // device_id is hexadecimal text without underscores, so it can be used
  // as the mDNS hostname without RFC 952/1123 character replacement.
  char hostname[24];
  snprintf(hostname, sizeof(hostname), "%s", did);

  logMdnsHeap("before-begin");
  if (!MDNS.begin(hostname)) {
    Serial.println("[mDNS] begin() failed -- advertising skipped");
    return;
  }
  MDNS.addService("hometiles", "tcp", 80);

  // addServiceTxt() has char*, const char* and String overloads. Mixing
  // string literals and char[] can make overload resolution ambiguous
  // through the deprecated literal-to-char* conversion. Pass all four
  // arguments as named const char* values to select that overload.
  const char* svc_name = "hometiles";
  const char* svc_proto = "tcp";
  const char* key_txtvers = "txtvers";
  const char* val_txtvers = "1";
  const char* key_device_id = "device_id";
  const char* val_device_id = did;
  const char* key_name = "name";
  const char* val_name = Device::displayName();
  const char* key_model = "model";
  const char* val_model = Device::profile().key;
  const DeviceConfig& cfg = configManager.getConfig();
  const char* key_base_topic = "base_topic";
  const char* val_base_topic = cfg.mqtt_base_topic;
  const char* key_ha_prefix = "ha_prefix";
  const char* val_ha_prefix = cfg.ha_prefix;

  MDNS.addServiceTxt(svc_name, svc_proto, key_txtvers, val_txtvers);
  MDNS.addServiceTxt(svc_name, svc_proto, key_device_id, val_device_id);
  MDNS.addServiceTxt(svc_name, svc_proto, key_name, val_name);
  MDNS.addServiceTxt(svc_name, svc_proto, key_model, val_model);
  MDNS.addServiceTxt(svc_name, svc_proto, key_base_topic, val_base_topic);
  MDNS.addServiceTxt(svc_name, svc_proto, key_ha_prefix, val_ha_prefix);
  mdns_active = true;
  logMdnsHeap("after-begin");
}

void HomeTilesNetworkManager::stopMdns() {
  if (!mdns_active) return;
  MDNS.end();
  mdns_active = false;
}

// ========== Update loop (loop task) ==========
void HomeTilesNetworkManager::update() {
  if (!configManager.isConfigured()) {
    return;
  }

  if (recoverWifiFromDmaStarvation()) {
    networkTransport.update();
    return;
  }

  networkTransport.update();
  uint32_t now_ms = millis();
  const bool wired_connected = isWiredConnected();
  const bool wired_link_up = isWiredLinkUp();

  // A failed C6 can leave ESP-Hosted cached at WL_CONNECTED while MQTT and
  // Web Admin are already unreachable, bypassing the Wi-Fi reconnect path.
  // After 15 seconds without MQTT, probe a real mode RPC. A broker outage
  // returns promptly and must not restart Wi-Fi; only a 5-second RPC
  // timeout is evidence of an unresponsive C6.
  const bool wifi_claims_connected = networkTransport.isWifiConnected();
  if (mqtt_enabled && !mqtt_suspended && !wifi_manual_disconnect &&
      wifi_claims_connected && !isMqttConnected() && !wifi_wedge_latched) {
    if (wifi_mqtt_offline_since == 0) {
      wifi_mqtt_offline_since = now_ms;
      wifi_health_probe_at = now_ms + kWifiHealthProbeDelayMs;
    } else if ((int32_t)(now_ms - wifi_health_probe_at) >= 0) {
      wifi_health_probe_at = now_ms + kWifiHealthProbeIntervalMs;
      if (!probeWifiDriverHealth("MQTT offline, cached WiFi link")) {
        return;
      }
    }
  } else {
    wifi_mqtt_offline_since = 0;
    wifi_health_probe_at = 0;
  }

  if (wired_link_up && !wired_link_was_up) {
    wired_ip_wait_until = now_ms + kWiredDhcpWaitMs;
    wired_link_up_since = now_ms;
    Serial.printf("[Network] Ethernet link is up; waiting up to %u ms for DHCP\n",
                  static_cast<unsigned>(kWiredDhcpWaitMs));
  } else if (!wired_link_up) {
    wired_ip_wait_until = 0;
    wired_link_up_since = 0;
  }
  wired_link_was_up = wired_link_up;

  // With Wi-Fi marked unresponsive, an Ethernet link can keep the device
  // online. If that link also disappears, a safe restart resets the C6
  // and restores a route to recovery. /crashlog.txt already holds the report.
  if (wifi_wedge_latched && !wired_link_up &&
      now_ms >= kWedgeRestartMinUptimeMs) {
    Serial.println(
        "[Network] Wi-Fi wedge without Ethernet link: safe restart");
    Serial.flush();
    BoardHAL::prepareForRestart();
    delay(500);
    BoardHAL::restart();
    return;
  }

  const bool wired_dhcp_pending =
      wired_link_up && !wired_connected && wired_ip_wait_until != 0 &&
      static_cast<int32_t>(now_ms - wired_ip_wait_until) < 0;
  if (wired_dhcp_pending && networkTransport.isWifiDriverActive()) {
    // Close WiFi-owned sockets before the ESP-hosted driver is torn down.
    // The USB backend delays Ethernet DHCP until this memory is available.
    if (isMqttConnected()) disconnectMqtt();
    if (webAdminServer.isRunning()) webAdminServer.stop();
    stopMdns();
    stopWifiForWired();
    networkTransport.update();
  }

  if (wired_connected) {
    wired_ip_wait_until = 0;
    stopWifiForWired();
  } else if (wired_was_connected) {
    // Fixed Ethernet mode has no Wi-Fi fallback. Stay offline until carrier
    // and DHCP return.
    Serial.println(
        "[Network] Ethernet disconnected; waiting for a new link (no Wi-Fi fallback)");
  }
  wired_was_connected = wired_connected;

  const uint32_t current_generation = networkTransport.generation();
  const bool transport_changed =
      current_generation != transport_generation_seen;
  if (transport_changed) {
    Serial.printf("[Network] Active transport: %s (generation=%u)\n",
                  networkTransport.activeName(),
                  static_cast<unsigned>(current_generation));
    transport_generation_seen = current_generation;

    // Recreate sockets after a default-route switch. Existing sockets can
    // otherwise stay bound to the interface that just disappeared.
    if (mqtt_enabled) {
      mqtt_reconfig_requested = true;
    }
    if (webAdminServer.isRunning()) {
      webAdminServer.stop();
    }
    stopMdns();
    // Treat a live route switch like a fresh connection edge so services
    // stopped above are immediately rebound to the new default interface.
    was_connected = false;
  }

  bool is_connected = networkTransport.isConnected();

  // Shared connectivity plus WiFi fallback management.
  if (!is_connected) {
    wifi_ps_state_known = false;

    // Retry a lost connection unless the user disconnected manually.
    // connectWifi() itself blocks attempts in fixed Ethernet mode.
    if (!wifi_manual_disconnect && !wired_dhcp_pending &&
        (int32_t)(now_ms - wifi_retry_at) >= 0) {
      connectWifi();
    }

    // Stop Web Admin when the connection is lost.
    if (was_connected && webAdminServer.isRunning()) {
      webAdminServer.stop();
    }
    if (was_connected) {
      stopMdns();
    }
  } else {
    // Connected.

    // A live Wi-Fi connection proves Hosted is responsive; earlier isolated
    // slow attempts reflected load rather than a stuck transport.
    if (networkTransport.isWifiConnected()) wifi_start_failures = 0;

    if (!was_connected) {
      logNetworkHeap(networkTransport.activeName());
    }

    // Start Web Admin on connection.
    if (!was_connected && !webAdminServer.isRunning()) {
      webAdminServer.start();
    }

    // mDNS serves initial pairing only. Once MQTT is configured, keep it off
    // even across broker reconnects to avoid begin/end cycles and DMA heap
    // fragmentation on every brief interruption.
    if (configManager.hasMqttConfig()) {
      stopMdns();
    } else if (webAdminServer.isRunning()) {
      startMdns();
    }

    // Trigger NTP synchronization on a new connection.
    if (!was_connected) {
      uiManager.scheduleNtpSync(0);
    }

    // serviceMqttWorker owns MQTT connection, socket and buffers. Telemetry
    // stays here because publishTelemetry() -> mqttPublishHomeSnapshot()
    // reads battery state of charge through I2C on the loop task. Actual
    // transmission goes through the outbound queue.
    if (mqtt_enabled && isMqttConnected()) {
      publishTelemetry();
    }
  }

  // Remember Wi-Fi status for the next iteration.
  was_connected = is_connected;
}

// ========== WiFi Power Management ==========
void HomeTilesNetworkManager::setWifiPowerSaving(bool enable) {
  if (!isWifiConnected()) {
    wifi_ps_state_known = false;
    return;
  }

#if defined(CONFIG_IDF_TARGET_ESP32P4)
  // ESP32-P4 uses an SDIO/esp-hosted WiFi transport. Modem sleep can trigger
  // transport TX asserts under WebUI, MQTT, or media-cover traffic.
  enable = false;
#endif

  if (wifi_ps_state_known && wifi_ps_enabled == enable) {
    return;
  }

  if (enable) {
    if (wifi_sleep_profile) {
      // Sleep profile: retain minimal connectivity for maximum power saving.
      WiFi.setSleep(WIFI_PS_MAX_MODEM);
      WiFi.setTxPower(WIFI_POWER_5dBm);
      Serial.println("🔋 WiFi Sleep Profile: Max Modem Sleep + 5dBm");
    } else {
      // Normal idle power-saving mode.
      WiFi.setSleep(WIFI_PS_MIN_MODEM);
      WiFi.setTxPower(WIFI_POWER_11dBm);
      Serial.println("🔋 WiFi Power Saving: Light Sleep + 11dBm");
    }
  } else {
    // Mains profile: full performance.
    WiFi.setSleep(WIFI_PS_NONE);       // No modem sleep.
    WiFi.setTxPower(WIFI_POWER_19_5dBm); // Maximum range.
    Serial.println("🔌 WiFi Full Power: No Sleep + 19.5dBm");
  }

  wifi_ps_state_known = true;
  wifi_ps_enabled = enable;
}

void HomeTilesNetworkManager::setSleepWifiProfile(bool enable) {
  if (wifi_sleep_profile == enable) return;
  wifi_sleep_profile = enable;
  // Apply a changed profile on the next setWifiPowerSaving() call.
  wifi_ps_state_known = false;
}
