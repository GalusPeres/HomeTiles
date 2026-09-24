#include "src/video/local_camera/local_camera_upload.h"

#include "src/video/local_camera/camera_select.h"

#if defined(HOMETILES_LOCAL_CAMERA)

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include <atomic>
#include <cerrno>
#include <cstring>

#include "src/network/network_manager.h"
#include "src/video/camera_stream.h"
#include "src/video/tcp_ack_socket.h"

namespace local_camera_upload {
namespace {

using namespace local_camera_stream;

constexpr uint32_t kSenderStackBytes = 8192;
// Same core and idle priority as the capture worker, the MQTT worker and the
// HA camera decoder: never outrank the MQTT worker.
constexpr UBaseType_t kSenderPriority = tskIDLE_PRIORITY;
constexpr uint32_t kSlotCount = 2;
constexpr uint32_t kSlotBytes = kMaxFrameBytes;
constexpr int kReceiveBufferBytes = 4 * 1024;
constexpr uint32_t kConnectTimeoutMs = 4000;
constexpr uint32_t kSocketPollMs = 250;
constexpr uint32_t kFrameWaitMs = 50;
constexpr uint32_t kHeadroomWaitStepMs = 10;
constexpr uint32_t kLogIntervalMs = 10000;

enum class SlotState : uint8_t { Free, Writing, Ready, Sending };

struct Slot {
  uint8_t* data = nullptr;
  uint32_t bytes = 0;
  uint32_t sequence = 0;
  SlotState state = SlotState::Free;
};

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
Slot g_slots[kSlotCount];
uint32_t g_sequence = 0;  // Under g_mux.

// Endpoint written by the loop task under g_mux; generation tells the sender
// that it changed.
Endpoint g_endpoint;
bool g_endpoint_valid = false;
std::atomic<uint32_t> g_generation{0};

TaskHandle_t g_sender = nullptr;
std::atomic<bool> g_run{false};
std::atomic<bool> g_busy{false};
std::atomic<bool> g_connected{false};

// Window counters (sender -> capture worker).
std::atomic<uint32_t> g_win_sent{0};
std::atomic<uint32_t> g_win_bytes{0};
std::atomic<uint32_t> g_win_chunks{0};
std::atomic<uint32_t> g_win_ack_ms{0};
std::atomic<uint32_t> g_win_ack_max{0};
std::atomic<uint32_t> g_win_dma{0};
std::atomic<uint32_t> g_win_dma_min{UINT32_MAX};
std::atomic<uint32_t> g_frames_total{0};
std::atomic<uint32_t> g_reconnects_total{0};

// Sender-task only.
uint32_t g_error_log_ms = 0;
uint32_t g_reconnect_log_ms = 0;

bool logDue(uint32_t* last_ms) {
  const uint32_t now_ms = millis();
  if (*last_ms != 0 && static_cast<uint32_t>(now_ms - *last_ms) < kLogIntervalMs) {
    return false;
  }
  *last_ms = now_ms ? now_ms : 1;
  return true;
}

void atomicMax(std::atomic<uint32_t>& target, uint32_t value) {
  uint32_t current = target.load();
  while (value > current && !target.compare_exchange_weak(current, value)) {
  }
}

void atomicMin(std::atomic<uint32_t>& target, uint32_t value) {
  uint32_t current = target.load();
  while (value < current && !target.compare_exchange_weak(current, value)) {
  }
}

// The display camera popup has priority: its stream stops the upload at the
// next socket poll and keeps it from starting.
bool stopNow() {
  return !g_run.load() || camera_stream_is_active();
}

bool stopNowCallback(void*) { return stopNow(); }

size_t dmaHeadroomBytes() {
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) +
         networkManager.mqttDmaReserveBytes();
}

// Socket adapter for sendJpegFrame()/sendControl().
struct SocketIo {
  int fd = -1;
  bool ignore_stop = false;  // For the final END header only.
  tcp_ack::DmaHeadroomGuard guard;

  bool stopRequested() { return !ignore_stop && stopNow(); }
  uint32_t nowMs() { return millis(); }
  bool send(const void* data, size_t bytes, uint32_t deadline_ms) {
    return tcp_ack::sendAll(fd, data, bytes, ignore_stop ? nullptr : stopNowCallback,
                            nullptr, deadline_ms);
  }
  tcp_ack::SocketReadResult receive(void* data, size_t bytes, uint32_t deadline_ms) {
    return tcp_ack::receiveExact(fd, data, bytes, stopNowCallback, nullptr, deadline_ms);
  }
  bool headroomForHeader() {
    const size_t headroom = dmaHeadroomBytes();
    atomicMin(g_win_dma_min, static_cast<uint32_t>(headroom));
    guard.reset();
    return headroom >= tcp_ack::kMinCameraDmaHeadroomBytes;
  }
  bool waitHeadroomForChunk() {
    for (;;) {
      const size_t headroom = dmaHeadroomBytes();
      atomicMin(g_win_dma_min, static_cast<uint32_t>(headroom));
      const tcp_ack::DmaHeadroomGuard::Result result = guard.check(headroom, millis());
      if (result == tcp_ack::DmaHeadroomGuard::Result::Ok) return true;
      if (result == tcp_ack::DmaHeadroomGuard::Result::Exhausted) {
        if (!logDue(&g_error_log_ms)) return false;
        Serial.printf(
            "[LocalCamStream] Safety stop: DMA headroom %u KB for %u ms; "
            "MQTT/Wi-Fi remain active\n",
            static_cast<unsigned>(headroom / 1024U),
            static_cast<unsigned>(guard.lowForMs(millis())));
        return false;
      }
      if (stopNow()) return false;
      vTaskDelay(pdMS_TO_TICKS(kHeadroomWaitStepMs));
    }
  }
  void yieldAfterAck() { taskYIELD(); }
};

bool snapshotEndpoint(Endpoint* out, uint32_t* generation) {
  portENTER_CRITICAL(&g_mux);
  const bool valid = g_endpoint_valid;
  if (valid) *out = g_endpoint;
  *generation = g_generation.load();
  portEXIT_CRITICAL(&g_mux);
  return valid;
}

Slot* takeReadySlot() {
  Slot* best = nullptr;
  portENTER_CRITICAL(&g_mux);
  for (Slot& slot : g_slots) {
    if (slot.state == SlotState::Ready && (!best || slot.sequence > best->sequence)) {
      best = &slot;
    }
  }
  if (best) best->state = SlotState::Sending;
  portEXIT_CRITICAL(&g_mux);
  return best;
}

void releaseSlot(Slot* slot) {
  portENTER_CRITICAL(&g_mux);
  slot->state = SlotState::Free;
  portEXIT_CRITICAL(&g_mux);
}

// Frames that were ready when a connection ended are stale for the next one.
void dropReadySlots() {
  portENTER_CRITICAL(&g_mux);
  for (Slot& slot : g_slots) {
    if (slot.state == SlotState::Ready) slot.state = SlotState::Free;
  }
  portEXIT_CRITICAL(&g_mux);
}

// Waits in short slices and returns early when the upload must stop.
void sleepUnlessStopped(uint32_t ms) {
  const uint32_t started_ms = millis();
  while (!stopNow()) {
    const uint32_t elapsed = millis() - started_ms;
    if (elapsed >= ms) return;
    const uint32_t step = ms - elapsed < kFrameWaitMs ? ms - elapsed : kFrameWaitMs;
    vTaskDelay(pdMS_TO_TICKS(step ? step : 1));
  }
}

enum class ConnectionEnd : uint8_t { Stopped, Reconnect, Failed };

// One TCP connection: handshake, then frames until stop, endpoint change or
// an error. *acked reports whether any frame was fully acknowledged.
ConnectionEnd runConnection(const Endpoint& endpoint, uint32_t generation,
                            bool first_connection, bool* acked) {
  *acked = false;
  const uint32_t connect_started_ms = millis();
  tcp_ack::ConnectOptions options;
  options.log_prefix = "[LocalCamStream]";
  options.receive_buffer_bytes = kReceiveBufferBytes;
  options.connect_timeout_ms = kConnectTimeoutMs;
  options.poll_timeout_ms = kSocketPollMs;
  options.send_timeout = true;
  options.stop = stopNowCallback;
  const int fd = tcp_ack::connectTo(endpoint.host, endpoint.port, options);
  if (fd < 0) {
    if (stopNow()) return ConnectionEnd::Stopped;
    if (logDue(&g_error_log_ms)) {
      Serial.printf("[LocalCamStream] TCP connection to %s:%u failed: errno=%d\n",
                    endpoint.host, static_cast<unsigned>(endpoint.port), errno);
    }
    return ConnectionEnd::Failed;
  }

  SocketIo io;
  io.fd = fd;
  char line[kUploadHandshakeMaxBytes + 1];
  const size_t line_bytes =
      buildUploadHandshake(line, sizeof(line), endpoint.session, endpoint.token);
  uint8_t hello[tcp_ack::kHelloBytes] = {};
  uint16_t chunk = 0;
  const bool handshake_sent =
      line_bytes != 0 &&
      io.send(line, line_bytes, deadlineAfter(millis(), kHelloTimeoutMs));
  const tcp_ack::SocketReadResult hello_result =
      handshake_sent
          ? io.receive(hello, sizeof(hello), deadlineAfter(millis(), kHelloTimeoutMs))
          : tcp_ack::SocketReadResult::Error;
  if (hello_result != tcp_ack::SocketReadResult::Ok || !helloAccepted(hello, &chunk)) {
    tcp_ack::closeSocket(fd);
    if (stopNow()) return ConnectionEnd::Stopped;
    if (logDue(&g_error_log_ms)) {
      uint16_t status = 0;
      uint16_t announced = 0;
      tcp_ack::decodeHello(hello, &status, &announced);
      Serial.printf(
          "[LocalCamStream] Handshake rejected: result=%u status=%u chunk=%u\n",
          static_cast<unsigned>(hello_result), static_cast<unsigned>(status),
          static_cast<unsigned>(announced));
    }
    return ConnectionEnd::Failed;
  }

  g_connected.store(true);
  if (first_connection || logDue(&g_reconnect_log_ms)) {
    Serial.printf("[LocalCamStream] Connected after %u ms, chunk=%u\n",
                  static_cast<unsigned>(millis() - connect_started_ms),
                  static_cast<unsigned>(chunk));
  }

  ConnectionEnd end = ConnectionEnd::Failed;
  uint32_t last_header_ms = millis();
  uint32_t last_sequence = 0;
  for (;;) {
    if (stopNow()) {
      end = ConnectionEnd::Stopped;
      break;
    }
    if (g_generation.load() != generation) {
      end = ConnectionEnd::Reconnect;
      break;
    }
    Slot* slot = takeReadySlot();
    if (!slot) {
      if (static_cast<uint32_t>(millis() - last_header_ms) >= kFlushIntervalMs) {
        if (!sendControl(io, tcp_ack::kMessageFlush, last_sequence, kChunkAckTimeoutMs)) {
          end = stopNow() ? ConnectionEnd::Stopped : ConnectionEnd::Failed;
          break;
        }
        last_header_ms = millis();
      }
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kFrameWaitMs));
      continue;
    }

    FrameSendReport report;
    const FrameSendResult result =
        sendJpegFrame(io, slot->sequence, slot->data, slot->bytes, chunk, &report);
    last_sequence = slot->sequence;
    const uint32_t frame_bytes = slot->bytes;
    releaseSlot(slot);
    g_win_chunks.fetch_add(report.chunks);
    g_win_ack_ms.fetch_add(report.ack_ms_total);
    atomicMax(g_win_ack_max, report.ack_ms_max);
    if (result == FrameSendResult::Sent) {
      last_header_ms = millis();
      g_win_sent.fetch_add(1);
      g_win_bytes.fetch_add(frame_bytes);
      const uint32_t total = g_frames_total.fetch_add(1) + 1;
      if (!*acked && (first_connection || total == 1)) {
        Serial.printf(
            "[LocalCamStream] First frame acked: %u bytes in %u chunks, "
            "int=%uKB dma=%uKB\n",
            static_cast<unsigned>(frame_bytes), static_cast<unsigned>(report.chunks),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
            static_cast<unsigned>(
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024U));
      }
      *acked = true;
      // Block for at least one tick so lower-priority work and the MQTT
      // worker on this core always get the CPU between frames.
      vTaskDelay(1);
      continue;
    }
    if (result == FrameSendResult::DmaDropped) {
      g_win_dma.fetch_add(1);
      vTaskDelay(1);
      continue;
    }
    if (result == FrameSendResult::Stopped) {
      // Mid-frame: an END header would be read as payload, so just close.
      tcp_ack::closeSocket(fd);
      g_connected.store(false);
      return ConnectionEnd::Stopped;
    }
    if (result != FrameSendResult::DmaSafetyStop && logDue(&g_error_log_ms)) {
      Serial.printf("[LocalCamStream] Frame %u failed: %s after %u of %u bytes, errno=%d\n",
                    static_cast<unsigned>(last_sequence), frameSendResultName(result),
                    static_cast<unsigned>(report.bytes_acked),
                    static_cast<unsigned>(frame_bytes), errno);
    }
    tcp_ack::closeSocket(fd);
    g_connected.store(false);
    return ConnectionEnd::Failed;
  }

  // Between frames: announce the end (bounded), then close.
  io.ignore_stop = true;
  sendControl(io, tcp_ack::kMessageEnd, last_sequence, kEndSendTimeoutMs);
  tcp_ack::closeSocket(fd);
  g_connected.store(false);
  return end;
}

// Runs until the capture worker clears g_run. A camera popup stream closes the
// connection at once (stopNow) and keeps the sender from reconnecting; the
// worker notices the popup itself and ends the run.
void runSession() {
  uint32_t failures = 0;
  bool first_connection = true;
  while (g_run.load()) {
    if (camera_stream_is_active()) {
      vTaskDelay(pdMS_TO_TICKS(kFrameWaitMs));
      continue;
    }
    Endpoint endpoint;
    uint32_t generation = 0;
    if (!snapshotEndpoint(&endpoint, &generation)) {
      sleepUnlessStopped(kSocketPollMs);
      continue;
    }
    bool acked = false;
    const ConnectionEnd end = runConnection(endpoint, generation, first_connection, &acked);
    dropReadySlots();
    if (end == ConnectionEnd::Stopped || stopNow()) continue;
    first_connection = false;
    g_reconnects_total.fetch_add(1);
    if (end == ConnectionEnd::Reconnect) {
      failures = 0;
      continue;
    }
    failures = acked ? 1 : failures + 1;
    const uint32_t delay_ms = reconnectDelayMs(failures);
    if (logDue(&g_reconnect_log_ms)) {
      Serial.printf("[LocalCamStream] Reconnect attempt %u in %u ms\n",
                    static_cast<unsigned>(failures), static_cast<unsigned>(delay_ms));
    }
    sleepUnlessStopped(delay_ms);
  }
  g_connected.store(false);
}

void senderMain(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!g_run.load()) continue;
    g_busy.store(true);
    runSession();
    g_connected.store(false);
    g_busy.store(false);
  }
}

bool ensureSlots() {
  for (Slot& slot : g_slots) {
    if (slot.data) continue;
    slot.data = static_cast<uint8_t*>(
        heap_caps_malloc(kSlotBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!slot.data) return false;
  }
  // A sender that did not finish its previous session may still read a slot;
  // it keeps running with the new session. Its slots stay allocated and are
  // reused here; the end of the next run releases them once it is idle.
  if (g_busy.load()) return true;
  portENTER_CRITICAL(&g_mux);
  for (Slot& slot : g_slots) {
    slot.state = SlotState::Free;
    slot.bytes = 0;
  }
  portEXIT_CRITICAL(&g_mux);
  return true;
}

}  // namespace

void setEndpoint(const Endpoint& endpoint) {
  portENTER_CRITICAL(&g_mux);
  const bool changed = !g_endpoint_valid ||
                       strcmp(g_endpoint.session, endpoint.session) != 0 ||
                       strcmp(g_endpoint.token, endpoint.token) != 0 ||
                       strcmp(g_endpoint.host, endpoint.host) != 0 ||
                       g_endpoint.port != endpoint.port;
  if (changed) {
    g_endpoint = endpoint;
    g_endpoint_valid = true;
    g_generation.fetch_add(1);
  }
  portEXIT_CRITICAL(&g_mux);
}

void clearEndpoint() {
  portENTER_CRITICAL(&g_mux);
  if (g_endpoint_valid) {
    g_endpoint_valid = false;
    g_generation.fetch_add(1);
  }
  portEXIT_CRITICAL(&g_mux);
}

void requestReconnect() {
  g_generation.fetch_add(1);
}

bool start(int core) {
  if (!ensureSlots()) {
    releaseBuffers();
    return false;
  }
  if (!g_sender &&
      xTaskCreatePinnedToCoreWithCaps(senderMain, "localCamUp", kSenderStackBytes, nullptr,
                                      kSenderPriority, &g_sender, core,
                                      MALLOC_CAP_SPIRAM) != pdPASS) {
    g_sender = nullptr;
    releaseBuffers();
    return false;
  }
  g_win_sent.store(0);
  g_win_bytes.store(0);
  g_win_chunks.store(0);
  g_win_ack_ms.store(0);
  g_win_ack_max.store(0);
  g_win_dma.store(0);
  g_win_dma_min.store(UINT32_MAX);
  g_run.store(true);
  xTaskNotifyGive(g_sender);
  return true;
}

void stop() {
  g_run.store(false);
  if (g_sender) xTaskNotifyGive(g_sender);
}

bool waitIdle(uint32_t timeout_ms) {
  const uint32_t started_ms = millis();
  while (g_busy.load()) {
    if (static_cast<uint32_t>(millis() - started_ms) >= timeout_ms) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return true;
}

void releaseBuffers() {
  if (g_busy.load()) return;
  portENTER_CRITICAL(&g_mux);
  uint8_t* buffers[kSlotCount];
  for (uint32_t i = 0; i < kSlotCount; ++i) {
    buffers[i] = g_slots[i].data;
    g_slots[i] = Slot{};
  }
  portEXIT_CRITICAL(&g_mux);
  for (uint8_t* buffer : buffers) {
    if (buffer) heap_caps_free(buffer);
  }
}

bool connected() { return g_connected.load(); }

bool busy() { return g_busy.load(); }

bool framePending() {
  bool pending = false;
  portENTER_CRITICAL(&g_mux);
  for (const Slot& slot : g_slots) {
    if (slot.data && slot.state == SlotState::Ready) pending = true;
  }
  portEXIT_CRITICAL(&g_mux);
  return pending;
}

bool publishFrame(const uint8_t* jpeg, uint32_t bytes, bool* replaced) {
  if (replaced) *replaced = false;
  if (!jpeg || bytes < kMinFrameBytes || bytes > kSlotBytes) return false;
  Slot* target = nullptr;
  bool was_ready = false;
  portENTER_CRITICAL(&g_mux);
  for (Slot& slot : g_slots) {
    if (slot.data && slot.state == SlotState::Free) {
      target = &slot;
      break;
    }
  }
  if (!target) {
    for (Slot& slot : g_slots) {
      if (slot.data && slot.state == SlotState::Ready) {
        target = &slot;
        was_ready = true;
        break;
      }
    }
  }
  if (target) target->state = SlotState::Writing;
  portEXIT_CRITICAL(&g_mux);
  if (!target) return false;

  memcpy(target->data, jpeg, bytes);
  portENTER_CRITICAL(&g_mux);
  target->bytes = bytes;
  target->sequence = ++g_sequence;
  target->state = SlotState::Ready;
  portEXIT_CRITICAL(&g_mux);
  if (replaced) *replaced = was_ready;
  if (g_sender) xTaskNotifyGive(g_sender);
  return true;
}

void takeWindow(StreamWindow* window) {
  if (!window) return;
  window->sent += g_win_sent.exchange(0);
  window->bytes_sent += g_win_bytes.exchange(0);
  window->chunks += g_win_chunks.exchange(0);
  window->ack_ms_total += g_win_ack_ms.exchange(0);
  const uint32_t ack_max = g_win_ack_max.exchange(0);
  if (ack_max > window->ack_ms_max) window->ack_ms_max = ack_max;
  window->dma += g_win_dma.exchange(0);
  const uint32_t dma_min = g_win_dma_min.exchange(UINT32_MAX);
  if (dma_min < window->dma_min_bytes) window->dma_min_bytes = dma_min;
}

uint32_t framesSentTotal() { return g_frames_total.load(); }

uint32_t reconnectsTotal() { return g_reconnects_total.load(); }

}  // namespace local_camera_upload

#endif  // defined(HOMETILES_LOCAL_CAMERA)
