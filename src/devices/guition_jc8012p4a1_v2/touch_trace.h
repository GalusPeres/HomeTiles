#pragma once

#include <stdint.h>
#include <stdio.h>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

// Touch diagnostics of the Guition V2 (test build, user 2026-10-27: sliders
// and the graph cursor still move when the finger is lifted, on this board
// only). Another project on this board notes that the GSL3680 loses a held
// finger now and then and that its finger numbers do not stay with a finger
// (jef-sure/dgx-di). One line per release shows the press's last points
// (ms before the release: x,y; "*" for a report with more than one contact)
// and how often the contacts changed; a finger back soon after a release
// logs how long it was lost. Both only follow real touches.
class TouchTrace {
 public:
  static constexpr uint8_t kSize = 6;
  static constexpr uint32_t kBackMs = 250;

  void press(uint32_t now, int32_t x, int32_t y) {
    if (released_ && now - release_ms_ < kBackMs) {
      Serial.printf("[Touch] finger back %lu ms after a release (%u empty reads), moved %ld,%ld px\n",
                    static_cast<unsigned long>(now - release_ms_), static_cast<unsigned>(empty_reads_),
                    static_cast<long>(x - last_x_), static_cast<long>(y - last_y_));
    }
    count_ = head_ = 0;
    multi_ = id_changes_ = 0;
    press_ms_ = now;
    released_ = false;
  }

  void sample(uint32_t now, int32_t x, int32_t y, uint8_t contacts, bool id_changed) {
    samples_[head_] = {now, static_cast<int16_t>(x), static_cast<int16_t>(y), contacts};
    head_ = static_cast<uint8_t>((head_ + 1) % kSize);
    if (count_ < kSize) ++count_;
    if (contacts > 1 && multi_ < 0xFFFF) ++multi_;
    if (id_changed && id_changes_ < 0xFFFF) ++id_changes_;
    last_x_ = x;
    last_y_ = y;
  }

  void empty_read() {
    if (released_ && empty_reads_ < 0xFF) ++empty_reads_;
  }

  void release(uint32_t now) {
    char text[200];
    size_t used = static_cast<size_t>(
        snprintf(text, sizeof(text), "[Touch] release after %lu ms: last", static_cast<unsigned long>(now - press_ms_)));
    for (uint8_t i = 0; i < count_ && used < sizeof(text); ++i) {
      const Sample& s = samples_[(head_ + kSize - count_ + i) % kSize];
      used += static_cast<size_t>(snprintf(text + used, sizeof(text) - used, " -%lu:%d,%d%s",
                                           static_cast<unsigned long>(now - s.ms), s.x, s.y,
                                           s.contacts > 1 ? "*" : ""));
    }
    Serial.printf("%s | multi %u, id changes %u\n", text, static_cast<unsigned>(multi_),
                  static_cast<unsigned>(id_changes_));
    released_ = true;
    release_ms_ = now;
    empty_reads_ = 1;
  }

 private:
  struct Sample {
    uint32_t ms;
    int16_t x, y;
    uint8_t contacts;
  };
  Sample samples_[kSize] = {};
  uint8_t head_ = 0, count_ = 0, empty_reads_ = 0;
  uint16_t multi_ = 0, id_changes_ = 0;
  uint32_t press_ms_ = 0, release_ms_ = 0;
  int32_t last_x_ = 0, last_y_ = 0;
  bool released_ = false;
};
