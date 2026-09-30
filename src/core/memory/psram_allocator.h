#pragma once

#include <stdlib.h>
#include <string>
#include <vector>
#include <esp_heap_caps.h>

// Allocator that puts everything into PSRAM (MALLOC_CAP_SPIRAM). The default
// malloc ALWAYS routes small allocations into the internal heap, so long-lived
// containers would otherwise consume and fragment the scarce internal SRAM
// reserved for the UI render band and WiFi (Guition S3: about 44 KB free).
template <typename T>
struct PsramAllocator {
  using value_type = T;
  PsramAllocator() noexcept = default;
  template <typename U>
  PsramAllocator(const PsramAllocator<U>&) noexcept {}
  T* allocate(size_t n) {
    void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // Last resort, the internal heap: an allocator must never return nullptr
    // because the container would then write to address 0.
    if (!p) p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_8BIT);
    if (!p) abort();
    return static_cast<T*>(p);
  }
  void deallocate(T* p, size_t) noexcept { heap_caps_free(p); }
  template <typename U>
  bool operator==(const PsramAllocator<U>&) const noexcept { return true; }
  template <typename U>
  bool operator!=(const PsramAllocator<U>&) const noexcept { return false; }
};

// std::string with the PSRAM allocator: short values (<=15 characters, SSO)
// live directly in their owner, and the allocator takes longer buffers from
// PSRAM as well. Arduino String cannot do this; its buffers always come from
// the internal heap.
using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

// std::vector whose element buffer lives in PSRAM. Elements that own heap
// memory themselves (Arduino String) still allocate that part internally.
template <typename T>
using PsVector = std::vector<T, PsramAllocator<T>>;
