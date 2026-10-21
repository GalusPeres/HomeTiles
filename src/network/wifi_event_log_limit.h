#pragma once

#include <stdint.h>

// Rate limit of the station event log (network_manager.cpp, PR #120). A join
// that keeps failing (router off, wrong password) repeats the same events
// every few seconds for as long as it lasts. Each kind of line (the event,
// and for a disconnect its reason) prints at most once per kWindowMs; the
// next printed line of that kind says how many were left out. Different
// kinds never hide each other, so a first join logs every step. Only the
// Arduino event task calls it.
namespace wifi_event_log {

constexpr uint32_t kWindowMs = 60000;
constexpr int kSlots = 8;

inline uint32_t key(uint32_t event, uint32_t reason) { return (event << 16) | (reason & 0xFFFF); }

class Limiter {
 public:
  // True when the line may print; *skipped is then the number of lines of
  // this kind left out since the last one printed.
  bool allow(uint32_t kind, uint32_t now_ms, uint32_t* skipped) {
    *skipped = 0;
    Slot* slot = nullptr;
    for (Slot& candidate : slots_) {
      if (candidate.used && candidate.kind == kind) {
        slot = &candidate;
        break;
      }
    }
    if (slot && now_ms - slot->printed_ms < kWindowMs) {
      if (slot->skipped < UINT32_MAX) ++slot->skipped;
      return false;
    }
    if (!slot) {
      // A free slot, otherwise the kind printed longest ago.
      slot = &slots_[0];
      for (Slot& candidate : slots_) {
        if (!candidate.used) {
          slot = &candidate;
          break;
        }
        if (now_ms - candidate.printed_ms > now_ms - slot->printed_ms) slot = &candidate;
      }
      *slot = Slot{};
      slot->used = true;
      slot->kind = kind;
    }
    *skipped = slot->skipped;
    slot->skipped = 0;
    slot->printed_ms = now_ms;
    return true;
  }

 private:
  struct Slot {
    bool used = false;
    uint32_t kind = 0;
    uint32_t printed_ms = 0;
    uint32_t skipped = 0;
  };
  Slot slots_[kSlots];
};

}  // namespace wifi_event_log
