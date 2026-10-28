#pragma once

// A table that would sit in internal RAM as a static array, placed in PSRAM
// instead. The SRAM draw band gets the internal RAM left above its reserve at
// display start (display_manager.cpp), so every internal byte a static table
// takes is a byte less for the band: on the S3, 17 KB of new tables had cut
// it from 50 rows to 31 (b605), and each row less means more bands per frame.
//
// Use as `static T (&table)[N] = psram_static_array<T, N>();`: the reference
// indexes and sizes like the array it replaces. The block is made once while
// static objects are constructed (PSRAM joins the heap before that, Arduino
// sets CONFIG_SPIRAM_BOOT_INIT) and is never freed; without PSRAM it falls
// back to internal RAM, as before.

#include <esp_heap_caps.h>
#include <stddef.h>
#include <stdlib.h>

#include <new>

template <typename T, size_t N>
T (&psram_static_array())[N] {
  void* block = heap_caps_malloc(sizeof(T) * N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!block) block = heap_caps_malloc(sizeof(T) * N, MALLOC_CAP_8BIT);
  if (!block) abort();
  T* items = static_cast<T*>(block);
  for (size_t i = 0; i < N; ++i) new (&items[i]) T();
  return *reinterpret_cast<T(*)[N]>(items);
}
