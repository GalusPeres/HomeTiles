#pragma once

// Opt-in still-image service for the built-in camera, shared by every device
// with a camera board file (HOMETILES_LOCAL_CAMERA, see camera_driver.h and
// camera_select.h). On every other profile all functions are inexpensive stubs
// that report "not supported".
//
// Threading:
//   - begin(), setEnabled(), service(), onMqttConnected(), handleMqttMessage(),
//     releaseForSleep() and shutdown() run on the Arduino loop task.
//   - isCommandTopic() may be called from the MQTT worker.
//   - Sensor access, capture, encoding and waiting for the MQTT upload run on a
//     dedicated low-priority worker task that is created on first enable.
//   - A live stream runs its capture loop on that worker and its socket on a
//     second idle-priority sender task (local_camera_upload.cpp).

#include <Arduino.h>

#include "src/video/local_camera/local_camera_contract.h"
#include "src/video/local_camera/local_camera_stream_contract.h"

namespace local_camera {

// Exact-profile capability: true only where the hardware path is compiled.
bool supported();

// Loads the persisted opt-in and probes the sensor when it is enabled.
void begin();

bool enabled();
// Persists the opt-in under its own NVS key and starts or stops the service.
bool setEnabled(bool enable);
// Pause from Home Assistant: the camera stays announced but captures nothing
// (a running stream ends, requests are refused). Not persisted, so a reboot
// always brings the camera back.
bool paused();
// Loop task only. Publishes the retained status so the Bridge follows.
bool setPaused(bool paused);
// Loop task only (the display pill). Ends the running live stream; keepalives
// of that Bridge session are ignored afterwards, a new session (the camera
// opened again in Home Assistant) streams normally. Nothing is paused or
// stored. Returns false when no live stream session exists.
bool endStream();

// Publishes retained status changes and, when needed, the Bridge capability.
void service();

// Post-connect hook: the retained {base}/stat/local_camera status.
void onMqttConnected();

// Command topic for the explicit subscription list; nullptr when unsupported.
const char* commandTopic();
// True when topic is {base}/cmnd/local_camera. Safe on the MQTT worker.
bool isCommandTopic(const char* topic);
// Returns true when the topic belonged to this module.
bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length);

// "local_camera" flag in the Bridge capabilities: enabled and sensor detected.
bool bridgeCapability();
// "local_camera_stream" flag: the live upload is available under exactly the
// same conditions as snapshots (supported, enabled, sensor detected).
bool bridgeStreamCapability();

// Live stream (panel -> Bridge upload, local_camera_stream_contract.h).
// true while a stream run or its sender still owns the camera or a socket.
bool streamActive();
// Wi-Fi/SDIO DMA-starvation recovery: stops a running upload with reason
// "error" (keepalives wait kStreamErrorRetryMs) so the recovery can proceed.
// Safe on the MQTT worker. Returns true when a wanted stream was stopped now.
bool stopStreamForTransportRecovery();
// Stored stream mode id (local_camera_stream::kModes, 0 = Auto).
uint8_t streamMode();
// Size of the picture Home Assistant shows (a quarter-turn board's JPEG is
// turned by the Bridge); 0 where unsupported.
uint16_t imageWidth();
uint16_t imageHeight();
// Validates and persists the mode; a running stream switches after its
// current frame and reconnects within the same session. Loop task only.
bool setStreamMode(uint8_t mode);
// Frames per second and JPEG quality of the Custom mode
// (local_camera_stream::kModeCustom).
local_camera_stream::CustomMode customMode();
// Clamps and persists them; a running Custom stream switches after its
// current frame. Loop task only.
bool setCustomMode(const local_camera_stream::CustomMode& mode);
// Privacy indicator: true while the sensor captures (snapshot or live
// stream) and for a short hold after it stopped. Any task.
bool indicatorActive();
// On-display indicator style (Web Admin, experimental): none, the red line
// only, or the line with the pill that ends the live stream.
enum class IndicatorStyle : uint8_t { None = 0, Line = 1, Pill = 2 };
IndicatorStyle indicatorStyle();
// Validates and persists the style under its own NVS key. Loop task only.
bool setIndicatorStyle(IndicatorStyle style);

// Storage work from the Web Admin (tile, folder and settings saves): flash
// operations stall both cores, and with the camera running this tripped the
// interrupt watchdog. beginStorageHold() stops a running stream (reason
// "storage", the session survives) and aborts a snapshot, waiting a bounded
// time; keepalives and snapshot requests are refused meanwhile. After the
// last endStorageHold() the hold lingers a few seconds (the layout reload
// after a save reads the flash too), then the next keepalive resumes the same
// session. Nestable. Loop task only.
void beginStorageHold();
void endStorageHold();
class ScopedStorageHold {
 public:
  explicit ScopedStorageHold(bool active = true) : active_(active) {
    if (active_) beginStorageHold();
  }
  ~ScopedStorageHold() {
    if (active_) endStorageHold();
  }
  ScopedStorageHold(const ScopedStorageHold&) = delete;
  ScopedStorageHold& operator=(const ScopedStorageHold&) = delete;

 private:
  bool active_;
};
// Stored horizontal mirror for snapshots and the live stream.
bool mirror();
// Persists the mirror setting; the next frame uses it. Loop task only.
bool setMirror(bool mirror);
// Stored user image controls (brightness, contrast, saturation, red, blue).
local_camera_contract::ImageSettings imageSettings();
// Clamps, persists the changed keys and applies them live: the worker pushes
// them to the ISP before the next snapshot or stream frame. Loop task only.
bool setImageSettings(const local_camera_contract::ImageSettings& settings);

// Web Admin state: disabled, probing, ready, not_found, error or unsupported.
const char* stateName();

// Appends one JSON object (without a leading comma) for Web Admin.
void appendStatusJson(String& json);

// Releases the capture pipeline while the display sleeps. Requests still work
// and re-create it on demand.
void releaseForSleep();
// OTA or restart: releases every camera resource and puts the sensor into
// software standby. Waits a bounded time for the worker.
void shutdown(const char* reason);

}  // namespace local_camera
