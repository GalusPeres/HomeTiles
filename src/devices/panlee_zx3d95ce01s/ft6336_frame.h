#pragma once

#include <stdint.h>

// FT6336 point frame as read from register 0x02:
//   [0] TD_STATUS (low nibble = number of points)
//   [1] event flag (bits 7:6) + X high, [2] X low
//   [3] touch id (bits 7:4) + Y high,   [4] Y low
// Event flag: 0 = put down, 1 = put up, 2 = contact, 3 = none.
//
// On the Panlee ZX3D95CE01S the controller sometimes returns a leftover
// release frame with one point: TD_STATUS 0x41, event flag 1 (put up), touch id
// 4 and a fixed coordinate. Real touches report TD_STATUS 0x01, event flag 0 or
// 2 and touch id 0 or 1. Treating the leftover frame as a touch pressed the
// tile under that coordinate on its own (ghost trigger).
namespace ft6336 {

constexpr uint8_t kEventPutUp = 1;
constexpr uint8_t kMaxTouchId = 1;  // The FT6336 tracks two points: ids 0 and 1.

inline bool isLiveTouchFrame(const uint8_t frame[5]) {
  if ((frame[0] & 0xF0) != 0) return false;  // Undefined TD_STATUS bits.
  if ((frame[0] & 0x0F) == 0) return false;
  if ((frame[1] >> 6) == kEventPutUp) return false;
  if ((frame[3] >> 4) > kMaxTouchId) return false;
  return true;
}

}  // namespace ft6336
