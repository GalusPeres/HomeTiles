#pragma once

#include <stdint.h>

// Paces the Home Assistant commands of one control (a slider gesture or a
// button) exactly like Home Assistant's own color temperature slider
// (frontend light-color-temp-picker.ts: throttle(500 ms, leading, trailing),
// and ha-control-slider.ts: a tap only fires value-changed). Slow light buses
// such as DALI DT8, where a color temperature takes several bus frames, lose
// commands that arrive while the previous one still runs (GitHub issue #11);
// Home Assistant's timing works there.
//
// Rules:
//   - pressing sends nothing; a tap sends exactly one command on release,
//   - the first drag movement sends at once (leading edge), later movements
//     at most once per interval after the previous command,
//   - the final value always goes out, but never closer than one interval to
//     the previous command (the caller delays it by final_wait()),
//   - the final value is skipped when this gesture already sent exactly it.
//
// Pure timing logic without LVGL or Arduino, so host tests can run it. All
// times are millis() values; differences use unsigned wrap-around.
namespace command_pacer {

inline constexpr uint32_t kIntervalMs = 500;

struct Pacer {
  uint32_t last_sent_ms = 0;
  uint32_t gesture_signature = 0;
  bool has_sent = false;
  bool in_gesture = false;
  bool gesture_sent = false;

  void begin_gesture() {
    in_gesture = true;
    gesture_sent = false;
  }

  void end_gesture() {
    in_gesture = false;
    gesture_sent = false;
  }

  // Milliseconds until the next command may go out (0 = now). Used for live
  // drag values and for the final value alike.
  uint32_t wait(uint32_t now) const {
    if (!has_sent) return 0;
    const uint32_t elapsed = now - last_sent_ms;
    return elapsed >= kIntervalMs ? 0 : kIntervalMs - elapsed;
  }

  // True when the final value equals the one this gesture already sent.
  bool final_redundant(uint32_t signature) const {
    return in_gesture && gesture_sent && gesture_signature == signature;
  }

  void sent(uint32_t now, uint32_t signature) {
    last_sent_ms = now;
    has_sent = true;
    if (in_gesture) {
      gesture_sent = true;
      gesture_signature = signature;
    }
  }
};

}  // namespace command_pacer
