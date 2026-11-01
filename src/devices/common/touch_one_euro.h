#pragma once

#include <math.h>
#include <stdint.h>

// The 1-euro filter (Casiez, Roussel, Vogel, CHI 2012,
// https://cristal.univ-lille.fr/~casiez/1euro): a low-pass filter whose
// cutoff rises with the speed, so a resting finger's jitter is smoothed away
// while a fast drag keeps up. The usual remedy for touch jitter (TUIO, Meta's
// interaction SDK); the GSL3680 boards need it because the chip leaves
// smoothing to the host and jitters 5 to 8 px under a resting finger
// (Guition V2 log, 2026-10-27). One filter per axis, in pixels.
class TouchOneEuro {
 public:
  // The authors' tuning: about 1 Hz at rest against jitter, then beta
  // (Hz per px/s) until fast moves no longer lag; the derivative at 1 Hz.
  static constexpr float kMinCutoffHz = 1.0f;
  static constexpr float kBeta = 0.02f;
  static constexpr float kDerivativeCutoffHz = 1.0f;

  void reset(float value) {
    value_ = value;
    speed_ = 0.0f;
  }

  // The filtered value after a raw `value` `dt_s` seconds after the last.
  float filter(float value, float dt_s) {
    const float raw_speed = (value - value_) / dt_s;
    speed_ += alpha(dt_s, kDerivativeCutoffHz) * (raw_speed - speed_);
    value_ += alpha(dt_s, kMinCutoffHz + kBeta * fabsf(speed_)) * (value - value_);
    return value_;
  }

 private:
  static float alpha(float dt_s, float cutoff_hz) {
    const float tau = 1.0f / (2.0f * 3.14159265f * cutoff_hz);
    return 1.0f / (1.0f + tau / dt_s);
  }

  float value_ = 0.0f;
  float speed_ = 0.0f;
};
