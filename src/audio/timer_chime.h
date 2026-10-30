#pragma once

// Built-in timer chimes, generated while they play: no sound files. Each
// chime is a short note table; every note sums a few sine partials (one
// overtone recipe per chime) with a 6 ms attack, an exponential decay and a
// 25 ms release. Overtones above half the sample rate are left out and each
// chime is scaled by its own level so that every chime peaks near -6 dBFS
// (none clips; the timer volume sets the loudness). The notes stay at or
// below about 1.6 kHz with weak overtones: higher beeps sounded piercing on
// the panel speakers.
//
// Pure and host-testable: no Arduino, FreeRTOS or hardware. The speaker
// worker renders blocks with ChimeSynth::render() into its output.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace timer_chime {

// Chimes in the order of their stable ids (voice_timer_contract.h sound ids
// 0..4). Appended only.
enum class Chime : uint8_t { RisingThree = 0, KitchenBeep, SoftBell, Marimba, Westminster, Count };
constexpr uint8_t kChimeCount = static_cast<uint8_t>(Chime::Count);

constexpr uint32_t kSampleRate = 22050;
constexpr size_t kMaxNotes = 4;
constexpr size_t kMaxPartials = 3;
constexpr float kAttackMs = 6.0f;
constexpr float kReleaseMs = 25.0f;
// The ring pattern: a chime, this gap, the chime again.
constexpr uint32_t kRepeatGapMs = 250;

struct Partial {
  float ratio;   // multiple of the note's frequency
  float weight;  // loudness relative to the fundamental
};

struct Tone {
  Partial partials[kMaxPartials];
  uint8_t partial_count;
  uint16_t decay_ms;  // 0: no decay (a beep)
};

struct Note {
  float hz;
  uint16_t start_ms;
  uint16_t length_ms;
  float gain;
};

struct ChimeSpec {
  const char* id;
  const Tone* tone;
  Note notes[kMaxNotes];
  uint8_t note_count;
  float level;  // output scale: the chime's peak at about half of full scale
};

constexpr Tone kPure = {{{1.0f, 1.0f}, {2.0f, 0.12f}, {0, 0}}, 2, 380};
constexpr Tone kBeep = {{{1.0f, 1.0f}, {2.0f, 0.08f}, {0, 0}}, 2, 250};
constexpr Tone kSoftBell = {{{1.0f, 1.0f}, {2.76f, 0.25f}, {5.4f, 0.08f}}, 3, 700};
constexpr Tone kMarimba = {{{1.0f, 1.0f}, {4.0f, 0.1f}, {0, 0}}, 2, 220};
constexpr Tone kBell = {{{1.0f, 1.0f}, {2.0f, 0.35f}, {3.0f, 0.12f}}, 3, 420};

constexpr ChimeSpec kChimes[kChimeCount] = {
    {"rising_three", &kPure, {{1046.5f, 0, 500, 0.8f}, {1318.5f, 180, 500, 0.8f}, {1568.0f, 360, 900, 0.85f}}, 3, 0.333f},
    {"kitchen_beep",
     &kBeep,
     {{1046.5f, 0, 130, 0.55f}, {1046.5f, 210, 130, 0.55f}, {1046.5f, 420, 130, 0.55f}, {1046.5f, 630, 130, 0.55f}},
     4,
     0.97f},
    {"soft_bell", &kSoftBell, {{880.0f, 0, 1800, 0.9f}}, 1, 0.565f},
    {"marimba",
     &kMarimba,
     {{523.3f, 0, 350, 0.9f}, {659.3f, 120, 350, 0.9f}, {784.0f, 240, 350, 0.9f}, {1046.5f, 360, 600, 0.9f}},
     4,
     0.335f},
    {"westminster",
     &kBell,
     {{659.3f, 0, 600, 0.85f}, {523.3f, 450, 600, 0.85f}, {587.3f, 900, 600, 0.85f}, {392.0f, 1350, 1200, 0.9f}},
     4,
     0.552f},
};

inline const ChimeSpec* spec(uint8_t chime) { return chime < kChimeCount ? &kChimes[chime] : nullptr; }

// Length of one chime in ms (its last note's end).
inline uint32_t lengthMs(uint8_t chime) {
  const ChimeSpec* s = spec(chime);
  if (!s) return 0;
  uint32_t end = 0;
  for (uint8_t i = 0; i < s->note_count; ++i) {
    const uint32_t note_end = static_cast<uint32_t>(s->notes[i].start_ms) + s->notes[i].length_ms;
    if (note_end > end) end = note_end;
  }
  return end;
}

// 256-entry sine table plus the wrap entry, interpolated linearly.
class SineTable {
 public:
  SineTable() {
    for (int i = 0; i <= 256; ++i) values_[i] = sinf(6.28318530718f * static_cast<float>(i) / 256.0f);
  }
  float at(float phase) const {  // phase 0..1
    const float x = phase * 256.0f;
    int i = static_cast<int>(x);
    if (i < 0) i = 0;
    if (i > 255) i = 255;
    const float fraction = x - static_cast<float>(i);
    return values_[i] + (values_[i + 1] - values_[i]) * fraction;
  }

 private:
  float values_[257];
};

// Renders one chime, `repeats` times with kRepeatGapMs between, in blocks.
class ChimeSynth {
 public:
  // False for an unknown chime or no repeats.
  bool begin(uint8_t chime, uint8_t repeats, uint32_t rate = kSampleRate) {
    spec_ = spec(chime);
    if (!spec_ || repeats == 0 || rate == 0) {
      spec_ = nullptr;
      return false;
    }
    rate_ = rate;
    repeats_left_ = repeats;
    chime_frames_ = framesFor(lengthMs(chime));
    gap_frames_ = framesFor(kRepeatGapMs);
    attack_frames_ = framesFor(kAttackMs);
    release_frames_ = framesFor(kReleaseMs);
    if (attack_frames_ == 0) attack_frames_ = 1;
    if (release_frames_ == 0) release_frames_ = 1;
    float norm = 0.0f;
    for (uint8_t p = 0; p < spec_->tone->partial_count; ++p) norm += spec_->tone->partials[p].weight;
    norm_ = norm > 0.0f ? 1.0f / norm : 1.0f;
    decay_step_ = spec_->tone->decay_ms > 0 ? expf(-1000.0f / (static_cast<float>(rate) * spec_->tone->decay_ms)) : 1.0f;
    startPass();
    return true;
  }

  bool done() const { return spec_ == nullptr; }

  // Writes up to `frames` mono samples; returns how many (0 once done).
  size_t render(int16_t* out, size_t frames) {
    size_t written = 0;
    while (spec_ && written < frames) {
      if (frame_ < chime_frames_) {
        out[written++] = sample();
        ++frame_;
      } else if (repeats_left_ > 1 && frame_ < chime_frames_ + gap_frames_) {
        out[written++] = 0;
        ++frame_;
      } else if (repeats_left_ > 1) {
        --repeats_left_;
        startPass();
      } else {
        spec_ = nullptr;
      }
    }
    return written;
  }

 private:
  struct Voice {
    uint32_t start = 0;
    uint32_t count = 0;
    float gain = 0;
    float env = 1.0f;
    float phase[kMaxPartials] = {};
    float step[kMaxPartials] = {};
  };

  uint32_t framesFor(float ms) const { return static_cast<uint32_t>(static_cast<float>(rate_) * ms / 1000.0f + 0.5f); }

  void startPass() {
    frame_ = 0;
    for (uint8_t n = 0; n < spec_->note_count; ++n) {
      Voice& v = voices_[n];
      const Note& note = spec_->notes[n];
      v.start = framesFor(note.start_ms);
      v.count = framesFor(note.length_ms);
      v.gain = note.gain;
      v.env = 1.0f;
      for (uint8_t p = 0; p < spec_->tone->partial_count; ++p) {
        v.phase[p] = 0.0f;
        v.step[p] = note.hz * spec_->tone->partials[p].ratio / static_cast<float>(rate_);
      }
    }
  }

  int16_t sample() {
    float mix = 0.0f;
    for (uint8_t n = 0; n < spec_->note_count; ++n) {
      Voice& v = voices_[n];
      if (frame_ < v.start || frame_ >= v.start + v.count) continue;
      const uint32_t k = frame_ - v.start;
      float env = v.env;
      if (k < attack_frames_) env *= static_cast<float>(k) / static_cast<float>(attack_frames_);
      if (k + release_frames_ > v.count) env *= static_cast<float>(v.count - k) / static_cast<float>(release_frames_);
      v.env *= decay_step_;
      float s = 0.0f;
      for (uint8_t p = 0; p < spec_->tone->partial_count; ++p) {
        if (v.step[p] >= 0.5f) continue;  // above the Nyquist limit
        s += table().at(v.phase[p]) * spec_->tone->partials[p].weight;
        v.phase[p] += v.step[p];
        if (v.phase[p] >= 1.0f) v.phase[p] -= 1.0f;
      }
      mix += s * norm_ * env * v.gain;
    }
    float value = mix * spec_->level;
    if (value > 1.0f) value = 1.0f;
    if (value < -1.0f) value = -1.0f;
    return static_cast<int16_t>(lrintf(value * 32767.0f));
  }

  static const SineTable& table() {
    static const SineTable kTable;
    return kTable;
  }

  const ChimeSpec* spec_ = nullptr;
  uint32_t rate_ = kSampleRate;
  uint8_t repeats_left_ = 0;
  uint32_t frame_ = 0;
  uint32_t chime_frames_ = 0;
  uint32_t gap_frames_ = 0;
  uint32_t attack_frames_ = 1;
  uint32_t release_frames_ = 1;
  float norm_ = 1.0f;
  float decay_step_ = 1.0f;
  Voice voices_[kMaxNotes];
};

}  // namespace timer_chime
