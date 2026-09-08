#pragma once

#include <algorithm>

namespace media_layout {

// Keep text and transport controls clear. The Bridge's 240px artwork bounds
// the decoded tile buffer even on a full-screen tile.
inline int cover_side(int content_width, int content_height, int top,
                      int footer, int maximum = 240) {
  return std::max(1, std::min({maximum, content_width * 42 / 100,
                             content_height - top - footer}));
}

}  // namespace media_layout
