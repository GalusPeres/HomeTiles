#include "src/network/net_bench.h"

#include <Arduino.h>

#if defined(DEVICE_GUITION_JC8012P4A1_V2)

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

namespace {

struct BenchPort {
  uint16_t port;
  UBaseType_t priority;
  bool acknowledge;
  const char* name;
};

constexpr BenchPort kPorts[] = {
    {5001, 5, false, "plain, normal priority"},
    {5002, tskIDLE_PRIORITY, false, "plain, idle priority"},
    {5003, tskIDLE_PRIORITY, true, "8 KB acks, idle priority"},
};
constexpr size_t kBufferBytes = 16 * 1024;
constexpr size_t kAckChunkBytes = 8 * 1024;

size_t dma_free() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA); }

int open_listener(uint16_t port) {
  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) return -1;
  const int reuse = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(fd, 1) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

void bench_task(void* arg) {
  const BenchPort& bench = *static_cast<const BenchPort*>(arg);
  uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM));
  int listener = -1;
  while (!buffer || listener < 0) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (!buffer) buffer = static_cast<uint8_t*>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM));
    if (buffer && listener < 0) listener = open_listener(bench.port);
  }
  Serial.printf("[NetBench] Listening on port %u (%s)\n", static_cast<unsigned>(bench.port), bench.name);
  for (;;) {
    const int fd = accept(listener, nullptr, nullptr);
    if (fd < 0) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    const int no_delay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));
    const uint32_t started = millis();
    uint32_t window_started = started;
    uint64_t total = 0;
    uint64_t window_bytes = 0;
    size_t chunk = 0;
    size_t dma_min = dma_free();
    Serial.printf("[NetBench] %u connected (%s), DMA free %u KB\n", static_cast<unsigned>(bench.port),
                  bench.name, static_cast<unsigned>(dma_min / 1024));
    for (;;) {
      const size_t want = bench.acknowledge ? kAckChunkBytes - chunk : kBufferBytes;
      const int received = recv(fd, buffer, want, 0);
      if (received <= 0) break;
      total += static_cast<uint64_t>(received);
      window_bytes += static_cast<uint64_t>(received);
      if (bench.acknowledge) {
        chunk += static_cast<size_t>(received);
        if (chunk == kAckChunkBytes) {
          chunk = 0;
          const uint32_t acknowledged = static_cast<uint32_t>(total);
          if (send(fd, &acknowledged, sizeof(acknowledged), 0) != static_cast<int>(sizeof(acknowledged))) break;
        }
      }
      const size_t dma = dma_free();
      if (dma < dma_min) dma_min = dma;
      const uint32_t now = millis();
      if (now - window_started >= 2000) {
        Serial.printf("[NetBench] %u: %.1f Mbit/s, DMA free %u KB (min %u KB)\n",
                      static_cast<unsigned>(bench.port),
                      static_cast<double>(window_bytes) * 8.0 / static_cast<double>(now - window_started) / 1000.0,
                      static_cast<unsigned>(dma / 1024), static_cast<unsigned>(dma_min / 1024));
        window_started = now;
        window_bytes = 0;
      }
    }
    const uint32_t elapsed = millis() - started;
    Serial.printf("[NetBench] %u done (%s): %.1f MB in %.1f s = %.1f Mbit/s, DMA min %u KB\n",
                  static_cast<unsigned>(bench.port), bench.name, static_cast<double>(total) / 1e6,
                  static_cast<double>(elapsed) / 1000.0,
                  elapsed ? static_cast<double>(total) * 8.0 / static_cast<double>(elapsed) / 1000.0 : 0.0,
                  static_cast<unsigned>(dma_min / 1024));
    close(fd);
  }
}

}  // namespace

void net_bench_start() {
  // Core 0 like the camera task; stacks in PSRAM like the camera task's.
  for (const BenchPort& bench : kPorts) {
    xTaskCreatePinnedToCoreWithCaps(bench_task, "netBench", 4096, const_cast<BenchPort*>(&bench),
                                    bench.priority, nullptr, 0, MALLOC_CAP_SPIRAM);
  }
}

#else

void net_bench_start() {}

#endif
