#include "src/core/diagnostics/loop_stall.h"

#if HOMETILES_LOOP_STALL_DIAGNOSTICS

#include <Arduino.h>
#include <WiFi.h>
#include <esp_debug_helpers.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <string.h>

namespace loop_stall {
namespace {

constexpr uint32_t kSlowStepMs = 1000;        // a finished step this long is logged
constexpr uint32_t kStuckMs = 3000;           // a running step this long is reported
constexpr uint32_t kStuckRepeatMs = 5000;     // repeated while the same step stays stuck
constexpr uint32_t kUploadRepeatMs = 30000;   // repeated while an upload is received
constexpr uint32_t kBacktraceStuckMs = 5000;  // task backtraces once a stall is this long
constexpr uint8_t kMaxBacktraces = 3;         // per boot; each print takes about 0.5 s
constexpr int kBacktraceDepth = 24;
constexpr uint64_t kCheckPeriodUs = 1000ULL * 1000ULL;
constexpr uint32_t kSampleMs = 2000;
constexpr uint32_t kWifiLogMs = 60000;
constexpr uint32_t kWifiEventLogMs = 5000;

const char* const kStepNames[] = {
    "top",           "ota-web",        "access-point", "board",
    "power",         "sleep-network",  "sleep-web",    "sleep-mqtt",
    "sleep-tiles",   "sleep-touch",    "pre-lvgl",     "lvgl",
    "folder-switch", "web-admin",      "post-connect", "services",
    "mqtt-inbound",  "dynamic-slots",  "network-update", "status",
};
static_assert(sizeof(kStepNames) / sizeof(kStepNames[0]) ==
                  static_cast<size_t>(Step::Status) + 1,
              "every step has a name");

// Written by the loop task and read by the esp_timer task. Aligned 32-bit
// values are read whole; the sequence number discards a step that changed
// while it was read. A torn request name only affects one log line.
volatile uint8_t g_step = 0;
volatile uint32_t g_step_since_ms = 0;
volatile uint32_t g_step_seq = 0;

// Loop task only.
uint32_t g_pass_start_ms = 0;
uint8_t g_slowest_step = 0;
uint32_t g_slowest_ms = 0;

char g_request[80] = "";
volatile uint32_t g_request_since_ms = 0;
volatile bool g_request_running = false;
volatile bool g_request_upload = false;

volatile bool g_wifi_connected = false;
volatile int8_t g_rssi = 0;
volatile uint8_t g_ps = 0xFF;
volatile int8_t g_tx_quarter_dbm = 0;
volatile uint32_t g_beacon_timeouts = 0;
volatile uint32_t g_disconnects = 0;
volatile uint16_t g_last_disconnect_reason = 0;

// esp_timer task only.
uint32_t g_reported_seq = 0;
uint32_t g_reported_ms = 0;
uint32_t g_backtrace_seq = 0;
uint8_t g_backtraces = 0;
esp_timer_handle_t g_timer = nullptr;

const char* stepName(uint8_t step) {
  return step < sizeof(kStepNames) / sizeof(kStepNames[0]) ? kStepNames[step] : "?";
}

bool isWebStep(uint8_t step) {
  return step == static_cast<uint8_t>(Step::OtaWeb) ||
         step == static_cast<uint8_t>(Step::SleepWeb) ||
         step == static_cast<uint8_t>(Step::WebAdmin);
}

const char* psName(uint8_t ps) {
  switch (ps) {
    case WIFI_PS_NONE: return "none";
    case WIFI_PS_MIN_MODEM: return "min-modem";
    case WIFI_PS_MAX_MODEM: return "max-modem";
    default: return "?";
  }
}

void copyRequest(char* out, size_t size) {
  memcpy(out, g_request, size);
  out[size - 1] = '\0';
}

void reportStuck(uint8_t step, uint32_t stuck_ms) {
  char request[sizeof(g_request)];
  copyRequest(request, sizeof(request));
  const uint32_t request_age_ms = millis() - g_request_since_ms;
  const int tx = g_tx_quarter_dbm;
  Serial.printf(
      "[LoopStall] Loop stuck for %u ms in step %s | web: %s%s%s (%s, %u ms ago) | "
      "int free=%u KB largest=%u KB min=%u KB | psram free=%u KB | "
      "wifi %s rssi=%d dBm ps=%s tx=%d.%02d dBm beacon-timeouts=%u "
      "disconnects=%u (last reason %u)\n",
      static_cast<unsigned>(stuck_ms), stepName(step),
      request[0] ? "\"" : "", request[0] ? request : "none", request[0] ? "\"" : "",
      g_request_running ? (g_request_upload ? "upload running" : "handler running")
                        : "handler finished",
      static_cast<unsigned>(request_age_ms),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
      static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
      g_wifi_connected ? "connected" : "disconnected", static_cast<int>(g_rssi),
      psName(g_ps), tx / 4, (tx % 4) * 25, static_cast<unsigned>(g_beacon_timeouts),
      static_cast<unsigned>(g_disconnects),
      static_cast<unsigned>(g_last_disconnect_reason));
}

// Runs every second on the esp_timer task, which keeps running while the loop
// task is blocked.
void check(void*) {
  const uint32_t seq = g_step_seq;
  const uint8_t step = g_step;
  const uint32_t since = g_step_since_ms;
  if (seq == 0 || seq != g_step_seq) return;
  const uint32_t now = millis();
  const uint32_t stuck_ms = now - since;
  if (stuck_ms < kStuckMs) return;
  const bool upload = isWebStep(step) && g_request_running && g_request_upload;
  const uint32_t repeat_ms = upload ? kUploadRepeatMs : kStuckRepeatMs;
  if (seq == g_reported_seq && now - g_reported_ms < repeat_ms) return;
  g_reported_seq = seq;
  g_reported_ms = now;
  reportStuck(step, stuck_ms);

  if (!upload && stuck_ms >= kBacktraceStuckMs && g_backtrace_seq != seq &&
      g_backtraces < kMaxBacktraces) {
    g_backtrace_seq = seq;
    ++g_backtraces;
    Serial.printf("[LoopStall] Backtraces of all tasks (%u of %u per boot), "
                  "decode with this build's ELF:\n",
                  static_cast<unsigned>(g_backtraces),
                  static_cast<unsigned>(kMaxBacktraces));
    // The backtraces go straight to the UART; drain the driver first so the
    // lines do not interleave.
    Serial.flush();
    const esp_err_t result = esp_backtrace_print_all_tasks(kBacktraceDepth);
    Serial.printf("[LoopStall] Backtraces end (%s)\n", esp_err_to_name(result));
  }
}

void onWifiEvent(void*, esp_event_base_t, int32_t id, void* data) {
  static uint32_t last_log_ms = 0;
  const uint32_t now = millis();
  const bool log_now = last_log_ms == 0 || now - last_log_ms >= kWifiEventLogMs;
  if (id == WIFI_EVENT_STA_BEACON_TIMEOUT) {
    ++g_beacon_timeouts;
    if (log_now) {
      last_log_ms = now;
      Serial.printf("[WiFiDiag] Beacon timeout (%u since boot), rssi=%d dBm\n",
                    static_cast<unsigned>(g_beacon_timeouts), static_cast<int>(g_rssi));
    }
  } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
    ++g_disconnects;
    if (data) {
      g_last_disconnect_reason =
          static_cast<const wifi_event_sta_disconnected_t*>(data)->reason;
    }
    if (log_now) {
      last_log_ms = now;
      Serial.printf("[WiFiDiag] Disconnected, reason %u (%u since boot)\n",
                    static_cast<unsigned>(g_last_disconnect_reason),
                    static_cast<unsigned>(g_disconnects));
    }
  }
}

}  // namespace

void begin() {
  if (g_timer) return;
  esp_err_t err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_BEACON_TIMEOUT,
                                             &onWifiEvent, nullptr);
  if (err == ESP_OK) {
    err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                     &onWifiEvent, nullptr);
  }
  if (err != ESP_OK) {
    Serial.printf("[LoopStall] Wi-Fi event counters unavailable: %s\n", esp_err_to_name(err));
  }

  esp_timer_create_args_t args = {};
  args.callback = &check;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "loop_stall";
  args.skip_unhandled_events = true;
  err = esp_timer_create(&args, &g_timer);
  if (err == ESP_OK) err = esp_timer_start_periodic(g_timer, kCheckPeriodUs);
  if (err != ESP_OK) {
    Serial.printf("[LoopStall] Stall check unavailable: %s\n", esp_err_to_name(err));
    return;
  }
  Serial.printf("[LoopStall] Active: steps >= %u ms are logged, stalls reported after %u ms\n",
                static_cast<unsigned>(kSlowStepMs), static_cast<unsigned>(kStuckMs));
}

void enter(Step step) {
  const uint32_t now = millis();
  if (g_step_seq != 0) {
    const uint8_t previous = g_step;
    const uint32_t took = now - g_step_since_ms;
    if (took >= kSlowStepMs) {
      if (isWebStep(previous) && g_request[0]) {
        Serial.printf("[LoopStall] Step %s took %u ms (last web request \"%s\")\n",
                      stepName(previous), static_cast<unsigned>(took), g_request);
      } else {
        Serial.printf("[LoopStall] Step %s took %u ms\n", stepName(previous),
                      static_cast<unsigned>(took));
      }
    }
    if (took > g_slowest_ms) {
      g_slowest_ms = took;
      g_slowest_step = previous;
    }
  }

  if (step == Step::Top) {
    // A slow pass without one slow step: many steps added up.
    const uint32_t pass_ms = now - g_pass_start_ms;
    if (g_pass_start_ms != 0 && pass_ms >= kSlowStepMs && g_slowest_ms < kSlowStepMs) {
      Serial.printf("[LoopStall] Loop pass took %u ms; slowest step %s (%u ms)\n",
                    static_cast<unsigned>(pass_ms), stepName(g_slowest_step),
                    static_cast<unsigned>(g_slowest_ms));
    }
    g_pass_start_ms = now;
    g_slowest_ms = 0;
  }

  g_step_since_ms = now;
  g_step = static_cast<uint8_t>(step);
  g_step_seq = g_step_seq + 1;
}

void webRequestBegin(const char* method, const char* uri) {
  snprintf(g_request, sizeof(g_request), "%s %s", method ? method : "?", uri ? uri : "?");
  g_request_since_ms = millis();
  g_request_upload = false;
  g_request_running = true;
}

void webIdle() {
  g_request_running = false;
  g_request_upload = false;
}

void webUploadBegin(const char* uri) {
  snprintf(g_request, sizeof(g_request), "UPLOAD %s", uri ? uri : "?");
  g_request_since_ms = millis();
  g_request_upload = true;
  g_request_running = true;
}

void webRequestEnd() {
  if (!g_request_running) return;
  g_request_running = false;
  const uint32_t took = millis() - g_request_since_ms;
  if (took >= kSlowStepMs) {
    Serial.printf("[LoopStall] Web request \"%s\" took %u ms\n", g_request,
                  static_cast<unsigned>(took));
  }
}

void sampleNetwork() {
  static uint32_t last_sample_ms = 0;
  static uint32_t last_log_ms = 0;
  const uint32_t now = millis();
  if (last_sample_ms != 0 && now - last_sample_ms < kSampleMs) return;
  last_sample_ms = now;
  if (last_log_ms == 0) last_log_ms = now;

  const bool connected = WiFi.status() == WL_CONNECTED;
  g_wifi_connected = connected;
  if (connected) {
    g_rssi = static_cast<int8_t>(WiFi.RSSI());
    wifi_ps_type_t ps;
    if (esp_wifi_get_ps(&ps) == ESP_OK) g_ps = static_cast<uint8_t>(ps);
    int8_t tx = 0;
    if (esp_wifi_get_max_tx_power(&tx) == ESP_OK) g_tx_quarter_dbm = tx;
  }

  if (now - last_log_ms >= kWifiLogMs) {
    last_log_ms = now;
    const int tx = g_tx_quarter_dbm;
    Serial.printf("[WiFiDiag] %s rssi=%d dBm ps=%s tx=%d.%02d dBm beacon-timeouts=%u "
                  "disconnects=%u\n",
                  connected ? "connected" : "disconnected", static_cast<int>(g_rssi),
                  psName(g_ps), tx / 4, (tx % 4) * 25,
                  static_cast<unsigned>(g_beacon_timeouts),
                  static_cast<unsigned>(g_disconnects));
  }
}

}  // namespace loop_stall

#endif  // HOMETILES_LOOP_STALL_DIAGNOSTICS
