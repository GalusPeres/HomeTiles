#include "src/video/camera_transport.h"

#include <Arduino.h>
#include <Preferences.h>

#include "src/core/firmware/firmware_version.h"

namespace camera_transport {
namespace {

constexpr char kNamespace[] = "camstream";
constexpr char kRunKey[] = "fast_run";
constexpr char kSafeKey[] = "safe_fw";
constexpr uint32_t kFastChunkBytes = 32 * 1024;
constexpr uint8_t kFastWindow = 2;
constexpr uint32_t kSafeChunkBytes = 8 * 1024;
constexpr uint8_t kSafeWindow = 1;

bool g_ready = false;
bool g_safe = false;
bool g_run_marked = false;

void write_run_mark(bool running) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putBool(kRunKey, running);
  prefs.end();
  g_run_marked = running;
}

// Once per boot: a mark left from the last boot means it ended while a fast
// stream ran (a crash or wedge restart), so this firmware keeps 8 KB.
void ensure_ready() {
  if (g_ready) return;
  g_ready = true;
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  if (prefs.getBool(kRunKey, false)) {
    prefs.putBool(kRunKey, false);
    prefs.putString(kSafeKey, FW_VERSION);
    g_safe = true;
    Serial.println("[CameraTransport] The last restart came while a fast stream ran; "
                   "8 KB transport for this firmware");
  } else if (prefs.getString(kSafeKey, "") == FW_VERSION) {
    g_safe = true;
    Serial.println("[CameraTransport] 8 KB transport for this firmware (a fast stream ended in a restart)");
  }
  prefs.end();
}

}  // namespace

Request request() {
  ensure_ready();
  if (g_safe) return {kSafeChunkBytes, kSafeWindow, false};
  return {kFastChunkBytes, kFastWindow, true};
}

void stream_started(bool fast) {
  ensure_ready();
  if (fast && !g_run_marked) write_run_mark(true);
}

void popup_closed() {
  if (g_run_marked) write_run_mark(false);
}

void fall_back(const char* reason) {
  ensure_ready();
  if (g_safe) return;
  g_safe = true;
  Serial.printf("[CameraTransport] Fast transport off until restart: %s\n", reason ? reason : "");
}

}  // namespace camera_transport
