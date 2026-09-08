#pragma once

#include <stdint.h>

namespace media_updates {

// Dispatch existing work sooner without polling or decoding when idle.
// The caller retains the camera gate and processes at most one queued update.
inline bool idle_batch_due(bool pending, uint32_t now, uint32_t& last_batch) {
  if (!pending || static_cast<uint32_t>(now - last_batch) < 100) return false;
  last_batch = now;
  return true;
}

}  // namespace media_updates
