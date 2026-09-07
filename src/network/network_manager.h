#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <Network.h>
// Vendored (not the global Arduino library): patched to insert a real
// vTaskDelay() periodically inside the packet-read loop, since
// readByte()/readPacket() otherwise only yield while WAITING for the next
// byte -- once a whole TCP segment is already buffered, they can walk
// through thousands of bytes with zero scheduling points. See
// src/network/vendor/pubsubclient/PubSubClient.cpp for details.
#include "src/network/vendor/pubsubclient/PubSubClient.h"

// Single source of the device_id: the full 48-bit MAC as a hex string, with no
// prefix. Used by network_manager.cpp and mqtt_handlers.cpp so both are
// guaranteed to compute the same value.
void buildDeviceId(char* buffer, size_t len);

// HomeTiles Network Manager - manages shared transports and MQTT.
//
// Single-owner MQTT: after init(), the PubSubClient object (mqtt_client) is
// touched exclusively by the MQTT worker task; mqtt_worker_task in the .ino
// calls serviceMqttWorker() in a loop on the second core. Every other task
// talks to the worker only through the outbound command queues (mqttEnqueue*)
// and volatile request flags, which is why the client needs no mutex at all.
class HomeTilesNetworkManager {
public:
  // Initialization, running in setup() BEFORE the worker starts. Among other
  // things it builds the bridge and request topic strings ONCE. connectMqtt()
  // used to rebuild them on every reconnect, and now that connectMqtt() runs on
  // the worker, every string reassignment would race the getter reads of the
  // loop task.
  void init();

  // Update loop on the loop task: WiFi reconnect, Web Admin, NTP, telemetry.
  // The MQTT connection itself is managed by the worker alone.
  void update();

  // --- Single-Owner MQTT API ---
  void beginMqttWorker();    // Call once from setup() before task startup to create the queues.
  void serviceMqttWorker();  // Worker task body; one iteration per call.

  // Connection state: a volatile flag written by the worker ONLY. Every other
  // task may read it at any time (one writer, many readers).
  bool isMqttConnected() const { return mqtt_connected_flag; }
  uint16_t getMqttBufferSize() const { return mqtt_buffer_size; }
  // Reclaimable ESP-Hosted DMA reserve currently held by the MQTT worker.
  // Camera/network guards count it as protected headroom without freeing it.
  size_t mqttDmaReserveBytes() const;

  // Safe to call from ANY task: copies topic and payload into one PSRAM block
  // and appends it to the matching outbound queue without blocking. Normal
  // control commands have their own lane and are not blocked by memory-hungry
  // history, energy or bridge requests.
  bool mqttEnqueuePublish(const char* topic, const char* payload, bool retain);
  bool mqttEnqueuePublish(const char* topic, const uint8_t* payload, size_t length, bool retain);
  // Small interactive requests may be inserted ahead of a long subscribe storm.
  // The worker still stays the only owner of the client.
  bool mqttEnqueuePublishPriority(const char* topic, const char* payload,
                                  bool retain);
  // A publish whose transmission, or whose expected reply, needs the large MQTT
  // buffer. It goes into a separate large lane that may wait while the DMA
  // reserve is tight, without holding up normal control commands.
  bool mqttEnqueuePublishWithLargeBuffer(const char* topic,
                                         const char* payload,
                                         bool retain,
                                         uint32_t hold_ms,
                                         bool priority = false);
  bool mqttEnqueueSubscribe(const char* topic);
  bool mqttEnqueueUnsubscribe(const char* topic);

  // After a successful (re)connect the worker sets a pending flag. The loop task
  // consumes it through mqttServicePostConnect() in mqtt_handlers.cpp and brings
  // the application layer up: subscribes, discovery, device settings and the
  // snapshot touch flash, LVGL grids and I2C, so they must NOT run on the
  // worker. Their publishes and subscribes come back through the queue.
  bool consumeMqttPostConnectPending();

  // Request flags for the worker, with a short bounded wait (<=500ms) for the
  // acknowledgement. The callers, hotspot entry and OTA start, are rare and not
  // time critical.
  void disconnectMqtt();
  void prepareMqttForOta();
  void deferMqttReconnect(uint32_t hold_ms = 6000);

  // After new MQTT settings were saved in Web Admin: drops a running
  // connection, reads mqtt_enabled, host and port freshly from the
  // ConfigManager and reconnects right away, with no device restart. Unlike
  // disconnectMqtt() this is NOT gated on mqtt_enabled, because that flag is
  // exactly what is being set live here (first configuration, host cleared and
  // so on).
  void requestMqttReconfigure();

  // The actual setBufferSize() call is made by the worker alone.
  void restoreMqttBufferNormal();

  // Are media tiles configured? Then the worker raises the "normal" buffer to
  // kMqttBufferMedia (24 KB) instead of 16 KB so PubSubClient does not drop
  // bridge states with embedded cover art (~19 KB). Set at boot from the tile
  // configuration and kept current on every route rebuild.
  void setMqttMediaBufferNeeded(bool needed) { mqtt_media_buffer_needed = needed; }

  // Shared network status plus WiFi-specific status for the WiFi settings UI.
  bool isNetworkConnected() const;
  bool isWifiConnected() const;
  bool wasPreviouslyConnected() const { return was_connected; }

  // Establish a Wi-Fi connection.
  void connectWifi();

  // ESP32-P4 / ESP-Hosted liveness probe. With a stuck C6, WiFi.status() is only
  // a cached state and can keep reporting "connected". getMode() in turn is a
  // real RPC: it returns immediately when healthy and after the 5s timeout on a
  // wedge. In that case the existing safe recovery path is triggered.
  bool probeWifiDriverHealth(const char* context);

  // Disconnect requested by the user through the WLAN popup: disconnects and
  // suppresses every auto reconnect until connectWifi() is used again or the
  // device restarts. The credentials stay stored, so after a reboot the device
  // connects normally.
  void disconnectWifiManual();
  bool isWifiManuallyDisconnected() const { return wifi_manual_disconnect; }

  // Telemetry on the loop task; sends through the outbound queue.
  void publishTelemetry();
  void publishBridgeConfig();
  // Empty requests are the lightweight periodic dirty check. An explicit
  // "force" is only sent for the manual Admin refresh.
  void publishBridgeRequest(bool force = false);
  const char* getBridgeApplyTopic() const;
  const char* getBridgeRequestTopic() const;
  const char* getHistoryRequestTopic() const;
  const char* getHistoryResponseTopic() const;
  const char* getWeatherRequestTopic() const;
  const char* getEnergyRequestTopic() const;
  const char* getEnergyResponseTopic() const;
  const char* getBridgeIconsTopic() const;

  // WiFi Power Management
  void setWifiPowerSaving(bool enable);
  void setSleepWifiProfile(bool enable);

  // Stops the mDNS advertising, for example when entering hotspot/AP mode where
  // it does not run on the STA side anyway. startMdns() stays internal and is
  // only triggered by update() on the existing connect edge.
  void stopMdns();

private:
  NetworkClient net_client;
  PubSubClient mqtt_client;  // After init(), accessed only by the worker task.

  uint32_t wifi_retry_at = 0;
  uint32_t wired_ip_wait_until = 0;
  bool wifi_manual_disconnect = false;  // Loop task reads/writes; UI reads.
  bool wifi_suspended_for_wired = false;
  bool wired_link_was_up = false;
  bool wired_was_connected = false;
  // Loop task: millis() of the last link-up edge (0 = link down). While the
  // Ethernet link is up, WiFi may start at the earliest after
  // kWiredLinkWifiBlockMs without an IP; otherwise the hosted start sabotages
  // the DMA allocation of the Ethernet backend (field test 2026-07-16).
  uint32_t wired_link_up_since = 0;
  // Loop task: consecutive failed STA start attempts. From
  // kWifiStartWedgeThreshold on, the ESP-Hosted driver counts as dead because
  // the C6 stopped answering RPCs.
  uint8_t wifi_start_failures = 0;
  // Loop task: the WLAN driver was declared dead. With an Ethernet link the
  // device keeps running without WiFi; if Ethernet drops too, only the safe
  // restart helps, which resets the C6 along with it.
  bool wifi_wedge_latched = false;
  // While MQTT stays offline although the WiFi transport still claims a
  // connection, check a real hosted RPC after a grace period. That keeps a dead
  // C6 from hanging in the cached WL_CONNECTED state indefinitely.
  uint32_t wifi_mqtt_offline_since = 0;
  uint32_t wifi_health_probe_at = 0;
  uint32_t mqtt_retry_at = 0;      // worker-only
  uint8_t mqtt_connect_failures = 0;  // Worker only: consecutive failures.
  uint32_t last_telemetry = 0;
  bool was_connected = false;
  uint32_t transport_generation_seen = 0;
  bool mqtt_enabled = false;
  bool mdns_active = false;
  // Build-compat: used by older/newer network_manager.cpp variants.
  bool wifi_ps_state_known = false;
  bool wifi_ps_enabled = false;
  bool wifi_sleep_profile = false;
  uint32_t mqtt_connected_at = 0;  // Worker only: millis() of the last connection.

  // Cross-task signals. Plain aligned bool/uint reads and writes are atomic on
  // this architecture, and every flag has exactly one writer per direction: for
  // a request another task sets it and the worker clears it, for a status the
  // worker sets it and other tasks read it.
  volatile bool mqtt_connected_flag = false;
  volatile bool mqtt_post_connect_pending = false;
  volatile bool mqtt_disconnect_requested = false;
  volatile bool mqtt_reconfig_requested = false;
  volatile bool mqtt_ota_prep_requested = false;
  volatile bool mqtt_restore_normal_requested = false;
  volatile bool mqtt_suspended = false;  // OTA active: the worker leaves the client untouched.
  // Set by the worker after sustained DMA starvation. The loop task then
  // rebuilds WLAN/SDIO alone, in a controlled way. Until that happens the worker
  // no longer touches the network client.
  volatile bool mqtt_transport_recovery_requested = false;
  // Requests that made the bottleneck visible survive the recovery reconnect and
  // are processed again afterwards.
  volatile bool mqtt_preserve_outbound_on_connect = false;
  volatile uint32_t mqtt_reconnect_hold_until = 0;
  volatile uint32_t mqtt_post_connect_ready_at = 0;
  volatile uint32_t mqtt_large_until = 0;
  volatile uint16_t mqtt_buffer_size = 0;  // Client buffer-size mirror maintained by the worker.
  uint16_t mqtt_receive_buffer_floor = 0;  // Bounded receive high-water mark; worker-owned.
  volatile bool mqtt_media_buffer_needed = false;  // Media tiles require the 24 KB normal buffer.

  // Target size of the "normal" buffer, depending on the media configuration.
  uint16_t mqttNormalBufferSize() const;

  // Built once in init(), before the worker starts, and only read afterwards.
  String bridge_apply_topic_;
  String bridge_request_topic_;
  String history_request_topic_;
  String history_response_topic_;
  String weather_request_topic_;
  String energy_request_topic_;
  String energy_response_topic_;
  String bridge_icons_topic_;

  // Worker only, after init():
  void connectMqtt();
  void drainOutboundQueues(uint8_t max_commands);
  void serviceBufferHousekeeping(uint32_t now_ms);
  bool setMqttBufferSize(uint16_t size, const char* reason);

  // Wired transports are exclusive with STA WiFi. This matters especially on
  // ESP32-P4, where an otherwise idle WiFi connection still keeps the hosted
  // SDIO RX path and its DMA allocations alive.
  bool isWiredConnected() const;
  bool isWiredLinkUp() const;
  bool isWifiStationEnabled() const;
  bool ensureWifiStationStarted();
  void stopWifiForWired();
  bool recoverWifiFromDmaStarvation();

  // ESP-Hosted wedge, meaning the C6 stopped answering: write a report to
  // /crashlog.txt, then keep running on Ethernet or, without an Ethernet link,
  // perform the safe restart that resets the C6 as well. Runs on the loop
  // task.
  void handleWifiDriverWedge(const char* context = nullptr);

  // mDNS start on the loop task, on the same connect edge as webAdminServer.
  // Purely additive for the zeroconf discovery of the HA bridge: it affects
  // neither MQTT nor Web Admin, and on failure it is skipped silently instead of
  // blocking anything else.
  void startMdns();
};

// Shared instance.
extern HomeTilesNetworkManager networkManager;

#endif // NETWORK_MANAGER_H
