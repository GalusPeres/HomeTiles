#pragma once

// Lip sync for a camera with sound (b506): while the panel plays a camera's
// sound, each received JPEG waits here until the sound reaches it, then the
// camera task decodes or queues it as before. The speaker reports how long
// its sound still needs to the loudspeaker, less how much earlier the Bridge
// sent it (speaker::cameraSyncDelayMs); the picture waits that long, less its
// own decode and present time. Frames sit in one PSRAM byte ring; this header
// only keeps the offsets, so it has no allocation, hardware or FreeRTOS code
// and runs on the host.
//
// Without sound the delay is 0 and the camera task never touches the ring.

#include <stddef.h>
#include <stdint.h>

namespace camera_sync {

// A second of 30 FPS full-screen frames (about 60 KB each) is far more than
// the 0.1-0.3 s a picture waits.
constexpr size_t kDelayRingBytes = 1024u * 1024u;
constexpr uint8_t kMaxDelayedFrames = 32;
// From a received frame to the picture on screen: the P4 decodes a popup
// frame in about 5 ms and presents it in about 18 ms, a full-screen frame in
// about 12 ms plus the swap (b505 log).
constexpr uint32_t kPresentMs = 25;
// Longer waits mean something is off; the picture then goes out unsynced.
constexpr uint32_t kMaxDelayMs = 600;
// A frame this late is skipped when a newer one is already due: the panel
// fell behind (a stall) and catches up. Shorter lateness (two frames that
// arrived together) shows every frame in order (b507: b506 showed only the
// newest due frame and dropped 3-5 of 30 frames per second).
constexpr uint32_t kCatchUpMs = 100;
// Even spacing (b508): Wi-Fi hands frames over in bunches (full-screen
// frames are two 32 KB blocks), and b507 passed those gaps on, 12 ms then
// 54 ms on the V2 full screen. A held frame is now due no earlier than
// 15/16 of the average arrival interval after the one before (a little less
// than the interval, so the line never grows), and at most one interval
// later than its sound needs. Gaps longer than kPauseGapMs are pauses, not
// the stream's rhythm, and leave the average alone.
constexpr uint32_t kPauseGapMs = 200;

// The wait for a frame received now: the sound's delay less the picture's
// own decode and present time, 0 without sound, at most kMaxDelayMs.
inline uint32_t pictureDelayMs(uint32_t sound_delay_ms) {
  if (sound_delay_ms <= kPresentMs) return 0;
  const uint32_t delay = sound_delay_ms - kPresentMs;
  return delay > kMaxDelayMs ? kMaxDelayMs : delay;
}

struct DelayedFrame {
  size_t offset = 0;
  size_t length = 0;
  uint32_t due_ms = 0;
};

// FIFO of frames in a byte ring, each in one piece for the JPEG decoder.
class FrameDelayLine {
 public:
  void reset(size_t capacity) {
    capacity_ = capacity;
    head_ = 0;
    count_ = 0;
    write_pos_ = 0;
    have_arrival_ = false;
    have_due_ = false;
    interval_x16_ = 0;
  }

  // The average arrival interval in ms (0 until two frames arrived).
  uint32_t intervalMs() const { return interval_x16_ / 16; }

  bool empty() const { return count_ == 0; }
  uint8_t size() const { return count_; }

  // Where `length` bytes fit in one piece, or false when the ring is full
  // (the caller drops the oldest frame and asks again).
  bool reserve(size_t length, size_t* offset) const {
    if (length == 0 || length > capacity_ || count_ >= kMaxDelayedFrames) return false;
    if (count_ == 0) {
      *offset = 0;
      return true;
    }
    // Occupied from the oldest frame to write_pos_, wrapping at the end.
    const size_t oldest = frames_[head_].offset;
    if (write_pos_ > oldest) {
      if (capacity_ - write_pos_ >= length) {
        *offset = write_pos_;
        return true;
      }
      if (oldest >= length) {
        *offset = 0;
        return true;
      }
      return false;
    }
    // Wrapped: free between write_pos_ and the oldest frame (none when they
    // meet: the frames fill the ring).
    if (write_pos_ < oldest && oldest - write_pos_ >= length) {
      *offset = write_pos_;
      return true;
    }
    return false;
  }

  // The frame copied to `offset` arrived at arrival_ms and its sound needs
  // delay_ms: due then, spaced evenly after the frame before (see
  // kPauseGapMs). Due times never go back: when the sound's delay shrinks,
  // frames catch up instead of overtaking.
  void commit(size_t offset, size_t length, uint32_t arrival_ms, uint32_t delay_ms) {
    if (have_arrival_) {
      const uint32_t gap = arrival_ms - last_arrival_ms_;
      if (gap <= kPauseGapMs) {
        // Average over about 16 frames, in 1/16 ms.
        interval_x16_ = interval_x16_ == 0 ? gap * 16 : interval_x16_ - interval_x16_ / 16 + gap;
      }
    }
    have_arrival_ = true;
    last_arrival_ms_ = arrival_ms;

    uint32_t due_ms = arrival_ms + delay_ms;
    if (have_due_) {
      const uint32_t paced = last_due_ms_ + interval_x16_ / 16 * 15 / 16;
      const uint32_t latest = due_ms + interval_x16_ / 16;
      if (static_cast<int32_t>(paced - due_ms) > 0) {
        due_ms = static_cast<int32_t>(paced - latest) > 0 ? latest : paced;
      }
      if (static_cast<int32_t>(due_ms - last_due_ms_) < 0) due_ms = last_due_ms_;
    }
    have_due_ = true;
    last_due_ms_ = due_ms;

    DelayedFrame& frame = frames_[(head_ + count_) % kMaxDelayedFrames];
    frame.offset = offset;
    frame.length = length;
    frame.due_ms = due_ms;
    ++count_;
    write_pos_ = offset + length;
  }

  void dropOldest() {
    if (count_ == 0) return;
    head_ = (head_ + 1) % kMaxDelayedFrames;
    if (--count_ == 0) write_pos_ = 0;
  }

  // When the oldest frame is due (false when the line is empty).
  bool nextDue(uint32_t* due_ms) const {
    if (count_ == 0) return false;
    *due_ms = frames_[head_].due_ms;
    return true;
  }

  // The oldest frame due at now_ms, taken out of the line; the caller asks
  // again for the next one. A frame more than kCatchUpMs late goes without
  // being shown when a newer one is due too, and counts as skipped.
  // Its bytes stay valid until the next reserve/commit.
  bool takeDue(uint32_t now_ms, DelayedFrame* out, uint32_t* skipped) {
    while (count_ > 1 && lateMs(now_ms, frames_[head_]) > static_cast<int32_t>(kCatchUpMs) &&
           lateMs(now_ms, frames_[(head_ + 1) % kMaxDelayedFrames]) >= 0) {
      if (skipped) ++*skipped;
      head_ = (head_ + 1) % kMaxDelayedFrames;
      --count_;
    }
    if (count_ == 0 || lateMs(now_ms, frames_[head_]) < 0) return false;
    *out = frames_[head_];
    head_ = (head_ + 1) % kMaxDelayedFrames;
    --count_;
    return true;
  }

 private:
  static int32_t lateMs(uint32_t now_ms, const DelayedFrame& frame) {
    return static_cast<int32_t>(now_ms - frame.due_ms);
  }

  DelayedFrame frames_[kMaxDelayedFrames];
  size_t capacity_ = 0;
  uint8_t head_ = 0;
  uint8_t count_ = 0;
  size_t write_pos_ = 0;
  // Pacing: the last arrival and due time (also after the frame left the
  // line) and the average arrival interval.
  bool have_arrival_ = false;
  bool have_due_ = false;
  uint32_t last_arrival_ms_ = 0;
  uint32_t last_due_ms_ = 0;
  uint32_t interval_x16_ = 0;
};

// Shortest and longest gap between two frames on screen, for the camera's
// FPS lines: even spacing shows as a narrow range. Reset with each line.
struct ShownGaps {
  bool have_last = false;
  uint32_t last_ms = 0;
  uint32_t count = 0;
  uint32_t min_ms = 0;
  uint32_t max_ms = 0;

  void note(uint32_t now_ms) {
    if (have_last) {
      const uint32_t gap = now_ms - last_ms;
      if (count == 0 || gap < min_ms) min_ms = gap;
      if (count == 0 || gap > max_ms) max_ms = gap;
      ++count;
    }
    have_last = true;
    last_ms = now_ms;
  }
  // Keeps the last frame's time, so the next line's first gap still counts.
  void startLine() { count = min_ms = max_ms = 0; }
  void restart() { have_last = false; startLine(); }
};

}  // namespace camera_sync
