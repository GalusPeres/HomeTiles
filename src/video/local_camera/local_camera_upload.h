#pragma once

// Live-stream sender of the built-in camera core (panel -> Bridge, protocol in
// local_camera_stream_contract.h). Internal to src/video/local_camera: the
// capture worker in local_camera.cpp produces JPEG frames, this module owns
// the socket, the two latest-frame-wins slots and the persistent sender task.
//
// Threading:
//   setEndpoint(), clearEndpoint() and requestReconnect(): Arduino loop task.
//   start(), stop(), waitIdle(), releaseBuffers(), publishFrame() and
//   takeWindow(): the capture worker.
//   The sender task runs at idle priority on the camera core with a PSRAM
//   stack and is never deleted; it waits for a notification between sessions.
// Only compiled with a camera board file (HOMETILES_LOCAL_CAMERA).

#include <stddef.h>
#include <stdint.h>

#include "src/video/local_camera/local_camera_stream_contract.h"

namespace local_camera_upload {

struct Endpoint {
  char session[local_camera_stream::kMaxSessionLength + 1] = {};
  char token[local_camera_stream::kMaxTokenLength + 1] = {};
  char host[16] = {};
  uint16_t port = 0;
};

// Loop task: a changed session, token, host or port makes the sender end its
// connection after the current frame and reconnect with the new values.
void setEndpoint(const Endpoint& endpoint);
void clearEndpoint();
// Ends the current connection after the current frame and reconnects within
// the same session (mode change).
void requestReconnect();

// Capture worker: allocates the frame slots, creates the sender task on first
// use and lets it connect. false when memory or the task is unavailable.
bool start(int core);
// Asks the sender to end the upload (END when between frames, then close).
void stop();
// true once the sender left its session; bounded wait in 10 ms steps.
bool waitIdle(uint32_t timeout_ms);
// Frees the slots; ignored while the sender is still busy.
void releaseBuffers();

// true after an accepted hello until the connection ends.
bool connected();
// true while the sender task runs a session (connected or reconnecting).
bool busy();

// true while a published frame still waits for the sender (capture worker:
// encoding another frame now would only replace it).
bool framePending();

// Copies a complete JPEG into the slot that is not being sent. A ready frame
// that was never sent is replaced (latest frame wins); *replaced reports it.
// false when the frame does not fit or no slot exists.
bool publishFrame(const uint8_t* jpeg, uint32_t bytes, bool* replaced);

// Adds the sender's counters since the last call to window and resets them.
void takeWindow(local_camera_stream::StreamWindow* window);

uint32_t framesSentTotal();
uint32_t reconnectsTotal();

}  // namespace local_camera_upload
