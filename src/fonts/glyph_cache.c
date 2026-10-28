#include "src/fonts/glyph_cache.h"

#include <stdbool.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <sdkconfig.h>
#else
#include <stdlib.h>
#endif

// Room for the icons and texts of every cached page. A 48 px icon unpacks to
// at most 48 x 50 bytes, a 20 px text letter to about 14 x 20; V2 b598 kept
// 76 icons in 91 KB, and the texts add a few hundred letters over all sizes.
// The P4 has 32 MB PSRAM; the S3's 8 MB hold half. When the cache is full
// every glyph is dropped and unpacked again on its next use, as without it.
#if !defined(ESP_PLATFORM) || defined(CONFIG_IDF_TARGET_ESP32P4)
#define GLYPH_CACHE_BYTES (512u * 1024u)
#define GLYPH_CACHE_ENTRIES 1024u
#define GLYPH_CACHE_SLOTS 2048u  // hash slots, a power of two above twice the entries
#else
#define GLYPH_CACHE_BYTES (256u * 1024u)
#define GLYPH_CACHE_ENTRIES 512u
#define GLYPH_CACHE_SLOTS 1024u
#endif

typedef struct {
  const lv_font_t* font;
  uint32_t gid;
  uint32_t offset;
  uint32_t size;
} glyph_entry_t;

typedef struct {
  uint16_t slot[GLYPH_CACHE_SLOTS];  // 0: free, else entry index + 1
  glyph_entry_t entry[GLYPH_CACHE_ENTRIES];
  uint32_t count;
  uint32_t used;
  uint8_t bytes[GLYPH_CACHE_BYTES];
} glyph_cache_t;

static glyph_cache_t* s_cache;
static glyph_cache_stats_t s_stats;
// The buffer a kept glyph is handed out in (glyph_cache_bitmap).
static lv_draw_buf_t s_hit;

// The cache lives in PSRAM, allocated with the first glyph. LVGL draws in one
// thread (LV_USE_OS none), so no lock is needed.
static bool ready(void) {
  if (s_cache) return true;
  if (s_stats.off) return false;
#if defined(ESP_PLATFORM)
  s_cache = (glyph_cache_t*)heap_caps_malloc(sizeof(glyph_cache_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  s_cache = (glyph_cache_t*)malloc(sizeof(glyph_cache_t));
#endif
  if (!s_cache) {
    s_stats.off = 1;
    return false;
  }
  memset(s_cache->slot, 0, sizeof(s_cache->slot));
  s_cache->count = 0;
  s_cache->used = 0;
  return true;
}

static uint32_t slot_of(const lv_font_t* font, uint32_t gid) {
  uint32_t h = (uint32_t)(uintptr_t)font ^ (gid * 0x9E3779B1u);
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  return h & (GLYPH_CACHE_SLOTS - 1u);
}

static const glyph_entry_t* find(const lv_font_t* font, uint32_t gid) {
  // At most GLYPH_CACHE_ENTRIES slots are taken, so a free one ends the search.
  for (uint32_t i = slot_of(font, gid);; i = (i + 1u) & (GLYPH_CACHE_SLOTS - 1u)) {
    const uint16_t n = s_cache->slot[i];
    if (n == 0) return NULL;
    const glyph_entry_t* e = &s_cache->entry[n - 1u];
    if (e->font == font && e->gid == gid) return e;
  }
}

static void drop_all(void) {
  memset(s_cache->slot, 0, sizeof(s_cache->slot));
  s_cache->count = 0;
  s_cache->used = 0;
}

static void keep(const lv_font_t* font, uint32_t gid, const uint8_t* data, uint32_t size) {
  const uint32_t room = (size + 3u) & ~3u;
  if (room > GLYPH_CACHE_BYTES) return;
  if (s_cache->count == GLYPH_CACHE_ENTRIES || s_cache->used + room > GLYPH_CACHE_BYTES) {
    drop_all();
    s_stats.clears++;
  }
  glyph_entry_t* e = &s_cache->entry[s_cache->count];
  e->font = font;
  e->gid = gid;
  e->offset = s_cache->used;
  e->size = size;
  memcpy(s_cache->bytes + e->offset, data, size);
  uint32_t i = slot_of(font, gid);
  while (s_cache->slot[i]) i = (i + 1u) & (GLYPH_CACHE_SLOTS - 1u);
  s_cache->slot[i] = (uint16_t)(++s_cache->count);
  s_cache->used += room;
}

const void* glyph_cache_bitmap(lv_font_glyph_dsc_t* g_dsc, lv_draw_buf_t* draw_buf) {
  if (g_dsc->req_raw_bitmap || !draw_buf || !g_dsc->gid.index || !ready()) {
    return lv_font_get_bitmap_fmt_txt(g_dsc, draw_buf);
  }
  // LVGL unpacks into rows of lv_draw_buf_width_to_stride(box_w, A8) bytes.
  const lv_font_t* font = g_dsc->resolved_font;
  const uint32_t gid = g_dsc->gid.index;
  const uint32_t size = lv_draw_buf_width_to_stride(g_dsc->box_w, LV_COLOR_FORMAT_A8) * (uint32_t)g_dsc->box_h;
  if (size == 0 || size > draw_buf->data_size) return lv_font_get_bitmap_fmt_txt(g_dsc, draw_buf);
  const glyph_entry_t* e = find(font, gid);
  if (e && e->size == size) {
    // LVGL blends the glyph straight from the buffer this returns (its
    // draw_buf->data and header.stride, lv_draw_sw_letter.c) right away, before
    // the next glyph is asked for: the kept bytes serve as they are, no copy.
    s_hit = *draw_buf;
    s_hit.header.flags &= ~LV_IMAGE_FLAGS_ALLOCATED;
    s_hit.header.cf = LV_COLOR_FORMAT_A8;
    s_hit.header.w = g_dsc->box_w;
    s_hit.header.h = g_dsc->box_h;
    s_hit.header.stride = lv_draw_buf_width_to_stride(g_dsc->box_w, LV_COLOR_FORMAT_A8);
    s_hit.data = s_cache->bytes + e->offset;
    s_hit.unaligned_data = s_hit.data;
    s_hit.data_size = size;
    s_stats.hits++;
    return &s_hit;
  }
  const void* out = lv_font_get_bitmap_fmt_txt(g_dsc, draw_buf);
  if (out == draw_buf && !e) {
    keep(font, gid, draw_buf->data, size);
    s_stats.misses++;
  }
  return out;
}

void glyph_cache_clear(void) {
  if (s_cache) drop_all();
}

glyph_cache_stats_t glyph_cache_stats(void) {
  glyph_cache_stats_t stats = s_stats;
  stats.glyphs = s_cache ? s_cache->count : 0;
  stats.bytes = s_cache ? s_cache->used : 0;
  return stats;
}
