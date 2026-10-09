#pragma once

#include <stdint.h>

// The camera stream's transport (#65, b320). Every panel asks the Bridge for
// 32 KB chunks with two in flight: one 8 KB chunk at a time held a Guition V2
// at 12 Mbit/s of the 28 it receives (b319 measurement). The 8 KB
// one-at-a-time protocol stays as the fallback: a fast stream that ends in a
// transport error falls back until the next restart, and a restart while a
// fast stream ran falls back for this firmware version (stored in NVS).
// UI thread only.
namespace camera_transport {

struct Request {
  uint32_t chunk_bytes;
  uint8_t window;
  bool fast;
};

// What the next stream asks the Bridge for.
Request request();
// A stream starts or the camera popup closes: marks or clears "a fast
// stream runs" in NVS, written only when it changes.
void stream_started(bool fast);
void popup_closed();
// A restart asked for by the user or an update (b327: an OTA during a stream
// fell back for the new firmware): the mark is cleared, the next boot keeps
// the fast transport. Recovery restarts (network wedge, display timeout) do
// not call this, so they still fall back.
void planned_restart();
// The 8 KB transport until the next restart.
void fall_back(const char* reason);

}  // namespace camera_transport
