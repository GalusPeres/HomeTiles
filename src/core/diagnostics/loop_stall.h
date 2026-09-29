#pragma once

#include <stdint.h>

// Main-loop stall diagnostics for the Guition ESP32-S3.
//
// The S3 main loop stopped for 16 to 44 seconds while MQTT dropped, and the
// existing [LoopGap] timing (which covers only the steps before LVGL) stayed
// silent. The loop now names each step it enters. A step that ran for a second
// or more is logged when it ends. An esp_timer check reports a step that is
// still running after three seconds, with the Web Admin request being served,
// memory and Wi-Fi state, and prints the backtrace of every task once per
// stall so the exact blocking call can be decoded with the build's ELF.
//
// Logging only: nothing here changes what the loop does. Other profiles
// compile the markers to nothing.

#if defined(DEVICE_GUITION_ESP32_4848S040)
#define HOMETILES_LOOP_STALL_DIAGNOSTICS 1
#else
#define HOMETILES_LOOP_STALL_DIAGNOSTICS 0
#endif

namespace loop_stall {

enum class Step : uint8_t {
  Top,             // loop start: pending Wi-Fi, OTA and reboot requests
  OtaWeb,          // Web Admin while an OTA upload suspends the display
  AccessPoint,     // setup access point branch
  Board,           // BoardHAL::update()
  Power,           // screensaver and power manager
  SleepNetwork,    // display sleep: network manager
  SleepWeb,        // display sleep: Web Admin
  SleepMqtt,       // display sleep: MQTT post-connect, inbound queue, services
  SleepTiles,      // display sleep: tile queues, status labels, caches
  SleepTouch,      // display sleep: touch poll and 20 ms pause
  PreLvgl,         // popups, caches and queues ([LoopGap] splits this step)
  Lvgl,            // lv_timer_handler(): timers, rendering, flush
  FolderSwitch,    // pending folder switch and S3 diagnostics service
  WebAdmin,        // 1 ms pause and Web Admin request handling
  PostConnect,     // MQTT post-connect subscriptions and publishes
  Services,        // view navigation, camera and command channel
  MqttInbound,     // inbound MQTT queue
  DynamicSlots,    // dynamic MQTT route reload
  NetworkUpdate,   // network manager update
  Status,          // memory logs, status bar and NTP
};

#if HOMETILES_LOOP_STALL_DIAGNOSTICS

// Starts the stall check and the Wi-Fi event counters. Call once after the
// network stack is initialized.
void begin();

// Marks the step the loop enters; logs the previous step if it took a second
// or more. Called on the loop task only.
void enter(Step step);

// The Web Admin request whose handler runs (from the Web server middleware).
void webRequestBegin(const char* method, const char* uri);
void webRequestEnd();

// A file or OTA upload whose body the loop receives. The Web server reads the
// whole body in one call, so a long upload is expected: it is reported less
// often and does not use up the task backtraces.
void webUploadBegin(const char* uri);

// The Web server returned to the loop: no handler or upload is running, also
// when an upload ended without reaching its handler.
void webIdle();

// Samples RSSI, power save mode and TX power every two seconds and logs them
// once a minute. Called from the loop.
void sampleNetwork();

#else

inline void begin() {}
inline void enter(Step) {}
inline void webRequestBegin(const char*, const char*) {}
inline void webRequestEnd() {}
inline void webUploadBegin(const char*) {}
inline void webIdle() {}
inline void sampleNetwork() {}

#endif

}  // namespace loop_stall
