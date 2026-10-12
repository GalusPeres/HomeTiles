#pragma once

// Pure, host-testable parts of the on-panel wake word: the conversion of
// microfrontend features to the model's int8 input, the sliding-window
// decision on the model's uint8 probabilities, and the mono PCM ring that
// holds the audio after a detection. Nothing here touches hardware,
// FreeRTOS, Arduino or TensorFlow Lite Micro.
//
// The feature definitions follow kahrendt/microWakeWord (Apache-2.0):
// microwakeword/inference.py scales the uint16 microfrontend output by
// 0.0390625 (10/256) to the float feature the models were trained on, then
// quantizes it with the input tensor's scale and zero point.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace wake_word {

// microfrontend settings the microWakeWord models were trained with
// (microwakeword/audio/audio_utils.py and the TFLM audio_microfrontend
// defaults it relies on).
constexpr int kSampleRate = 16000;
constexpr int kWindowMs = 30;
constexpr int kFeatureChannels = 40;
constexpr float kLowerBandHz = 125.0f;
constexpr float kUpperBandHz = 7500.0f;
constexpr int kNoiseSmoothingBits = 10;
constexpr float kNoiseEvenSmoothing = 0.025f;
constexpr float kNoiseOddSmoothing = 0.06f;
constexpr float kNoiseMinSignalRemaining = 0.05f;
constexpr float kPcanStrength = 0.95f;
constexpr float kPcanOffset = 80.0f;
constexpr int kPcanGainBits = 21;
constexpr int kLogScaleShift = 6;

// uint16 feature -> float feature, as in microwakeword/inference.py.
constexpr float kFeatureScale = 0.0390625f;

// Invocations ignored after a (re)start while the streaming model's internal
// state fills with real audio instead of zeros.
constexpr uint16_t kWarmupInvocations = 100;

// Quantizes one microfrontend feature to the model's int8 input:
// round(value * kFeatureScale / input_scale) + zero_point, clamped. With the
// v2 models' input (scale 0.10196, zero point -128) this is about
// value * 256 / 668 - 128.
inline int8_t quantizeFeature(uint16_t value, float input_scale, int32_t zero_point) {
  float scaled = 0.0f;
  if (input_scale > 0.0f) scaled = static_cast<float>(value) * kFeatureScale / input_scale;
  const long q = lroundf(scaled) + zero_point;
  if (q < -128) return -128;
  if (q > 127) return 127;
  return static_cast<int8_t>(q);
}

// Probability cutoff (0..1) as the uint8 model output scale (0..255).
inline uint8_t cutoffToUint8(float cutoff) {
  if (!(cutoff > 0.0f)) return 0;
  if (cutoff >= 1.0f) return 255;
  return static_cast<uint8_t>(lroundf(cutoff * 255.0f));
}

// Detection when the mean of the last `window` probabilities is above the
// cutoff, after the warm-up invocations and once the window is full.
class SlidingWindowDetector {
 public:
  static constexpr uint8_t kMaxWindow = 16;

  void configure(float cutoff, uint8_t window) {
    cutoff_ = cutoffToUint8(cutoff);
    window_ = window == 0 ? 1 : (window > kMaxWindow ? kMaxWindow : window);
    reset(0);
  }

  void reset(uint16_t ignore_invocations) {
    ignore_ = ignore_invocations;
    count_ = 0;
    next_ = 0;
    sum_ = 0;
    memset(values_, 0, sizeof(values_));
  }

  // Returns true when this probability completes a detection.
  bool push(uint8_t probability) {
    if (ignore_ > 0) {
      --ignore_;
      return false;
    }
    if (count_ == window_) {
      sum_ -= values_[next_];
    } else {
      ++count_;
    }
    values_[next_] = probability;
    sum_ += probability;
    next_ = static_cast<uint8_t>((next_ + 1) % window_);
    if (count_ < window_) return false;
    return sum_ > static_cast<uint32_t>(cutoff_) * window_;
  }

  uint8_t cutoff() const { return cutoff_; }
  uint8_t window() const { return window_; }
  uint16_t ignoring() const { return ignore_; }
  // Mean of the filled window (0 before any value), for diagnostics.
  uint8_t mean() const { return count_ ? static_cast<uint8_t>(sum_ / count_) : 0; }

 private:
  uint8_t values_[kMaxWindow] = {};
  uint32_t sum_ = 0;
  uint16_t ignore_ = 0;
  uint8_t cutoff_ = 255;
  uint8_t window_ = 1;
  uint8_t count_ = 0;
  uint8_t next_ = 0;
};

// FIFO of mono 16-bit samples over caller-owned memory. Bytes are copied with
// memcpy, so the destination of peekBytes() needs no alignment.
class PcmRing {
 public:
  void reset(int16_t* memory, size_t capacity_samples) {
    data_ = memory;
    capacity_ = memory ? capacity_samples : 0;
    clear();
  }
  void clear() {
    head_ = 0;
    count_ = 0;
  }
  size_t size() const { return count_; }
  size_t capacity() const { return capacity_; }
  size_t space() const { return capacity_ - count_; }

  // Appends all samples, or none when they do not fit (returns false).
  bool write(const int16_t* samples, size_t count) {
    if (count > space()) return false;
    size_t tail = (head_ + count_) % (capacity_ ? capacity_ : 1);
    size_t left = count;
    while (left > 0) {
      const size_t run = left < capacity_ - tail ? left : capacity_ - tail;
      memcpy(data_ + tail, samples, run * sizeof(int16_t));
      samples += run;
      left -= run;
      tail = (tail + run) % capacity_;
    }
    count_ += count;
    return true;
  }

  // Copies the oldest `count` samples (native byte order) without removing
  // them. Returns false when fewer are buffered.
  bool peekBytes(uint8_t* out, size_t count) const {
    if (count > count_) return false;
    size_t pos = head_;
    size_t left = count;
    while (left > 0) {
      const size_t run = left < capacity_ - pos ? left : capacity_ - pos;
      memcpy(out, data_ + pos, run * sizeof(int16_t));
      out += run * sizeof(int16_t);
      left -= run;
      pos = (pos + run) % capacity_;
    }
    return true;
  }

  void discard(size_t count) {
    if (count > count_) count = count_;
    if (capacity_) head_ = (head_ + count) % capacity_;
    count_ -= count;
  }

 private:
  int16_t* data_ = nullptr;
  size_t capacity_ = 0;
  size_t head_ = 0;
  size_t count_ = 0;
};

}  // namespace wake_word
