#include "src/tiles/icons/mdi_icon_cache.h"

#include <string.h>

#include "src/fonts/glyph_cache.h"

namespace mdi_icon_cache {
namespace {

constexpr uint32_t kMaxEntries = 1024;
constexpr uint32_t kAverageGlyphs = 512;

struct Entry {
  uint32_t offset;
  uint16_t size;
  uint16_t gid;
};

struct State {
  uint32_t unknown_codepoint;
  bool has_source;
  Source source;
  uint16_t* slot_of_gid;  // 0: not cached, else entry index + 1
  Entry* entries;
  uint32_t count;
  uint8_t* bitmaps;
  Stats stats;
};

size_t align8(size_t n) { return (n + 7) & ~static_cast<size_t>(7); }

size_t table_bytes(const mdi_icon_font_data_t& data) {
  return align8(sizeof(State)) + align8(data.glyph_count * sizeof(uint16_t)) +
         align8(kMaxEntries * sizeof(Entry));
}

size_t bitmap_capacity(const mdi_icon_font_data_t& data) {
  const size_t average = data.glyph_count > 1 ? data.bitmap_bytes / (data.glyph_count - 1) : 0;
  return (average * kAverageGlyphs + 4095) & ~static_cast<size_t>(4095);
}

State* state_of(const mdi_icon_loader_t* loader) {
  return loader ? static_cast<State*>(loader->cache) : nullptr;
}

// A glyph's bitmap ends where the next one starts; the last one at the end.
uint32_t glyph_size(const mdi_icon_font_data_t& data, uint32_t gid) {
  const uint32_t start = data.glyph_dsc[gid].bitmap_index;
  const uint32_t end = gid + 1 < data.glyph_count ? data.glyph_dsc[gid + 1].bitmap_index : data.bitmap_bytes;
  return end > start ? end - start : 0;
}

const uint8_t* core_bitmap(const mdi_icon_font_data_t& data, uint32_t gid) {
  uint32_t lo = 0;
  uint32_t hi = data.core_count;
  while (lo < hi) {
    const uint32_t mid = (lo + hi) / 2;
    if (data.core_ids[mid] < gid) lo = mid + 1;
    else hi = mid;
  }
  if (lo < data.core_count && data.core_ids[lo] == gid) return data.core_bitmap + data.core_offsets[lo];
  return nullptr;
}

void clear(State* s) {
  for (uint32_t i = 0; i < s->count; i++) s->slot_of_gid[s->entries[i].gid] = 0;
  s->count = 0;
  s->stats.cached = 0;
  s->stats.bitmap_used = 0;
}

const uint8_t* load(State* s, const mdi_icon_font_data_t& data, uint32_t gid) {
  const uint32_t size = glyph_size(data, gid);
  if (size == 0 || size > UINT16_MAX || size > s->stats.bitmap_capacity) {
    s->stats.failures++;
    return nullptr;
  }
  if (s->count == kMaxEntries || s->stats.bitmap_used + size > s->stats.bitmap_capacity) {
    clear(s);
    s->stats.clears++;
  }
  uint8_t* out = s->bitmaps + s->stats.bitmap_used;
  if (!s->source.read(s->source.ctx, data.glyph_dsc[gid].bitmap_index, out, size)) {
    s->stats.failures++;
    return nullptr;
  }
  s->entries[s->count] = {s->stats.bitmap_used, static_cast<uint16_t>(size), static_cast<uint16_t>(gid)};
  s->slot_of_gid[gid] = static_cast<uint16_t>(s->count + 1);
  s->count++;
  s->stats.cached = s->count;
  s->stats.bitmap_used += size;
  s->stats.loads++;
  return out;
}

bool available(const mdi_icon_loader_t* loader, uint32_t gid) {
  if (!loader || !loader->data || gid >= loader->data->glyph_count) return false;
  if (core_bitmap(*loader->data, gid)) return true;
  const State* s = state_of(loader);
  return s && (s->has_source || s->slot_of_gid[gid] != 0);
}

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

uint32_t u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
         static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

}  // namespace

size_t block_bytes(const mdi_icon_font_data_t& data) { return table_bytes(data) + bitmap_capacity(data); }

bool attach(mdi_icon_loader_t* loader, void* block, size_t bytes, uint32_t unknown_codepoint) {
  if (!loader) return false;
  loader->cache = nullptr;
  if (!loader->data || !block) return false;
  const mdi_icon_font_data_t& data = *loader->data;
  const size_t tables = table_bytes(data);
  if (bytes <= tables) return false;
  memset(block, 0, tables);
  uint8_t* p = static_cast<uint8_t*>(block);
  State* s = reinterpret_cast<State*>(p);
  p += align8(sizeof(State));
  s->slot_of_gid = reinterpret_cast<uint16_t*>(p);
  p += align8(data.glyph_count * sizeof(uint16_t));
  s->entries = reinterpret_cast<Entry*>(p);
  p += align8(kMaxEntries * sizeof(Entry));
  s->bitmaps = p;
  s->stats.bitmap_capacity = static_cast<uint32_t>(bytes - tables);
  s->unknown_codepoint = unknown_codepoint;
  loader->cache = s;
  return true;
}

void set_source(mdi_icon_loader_t* loader, const Source* source) {
  State* s = state_of(loader);
  if (!s) return;
  clear(s);
  glyph_cache_clear();  // nothing unpacked from the old source stays
  s->has_source = source != nullptr && source->read != nullptr;
  s->source = s->has_source ? *source : Source{nullptr, nullptr};
}

bool has_source(const mdi_icon_loader_t* loader) {
  const State* s = state_of(loader);
  return s && s->has_source;
}

Stats stats(const mdi_icon_loader_t* loader) {
  const State* s = state_of(loader);
  return s ? s->stats : Stats{};
}

HeaderCheck check_header(const uint8_t* header, size_t header_len, uint32_t file_bytes,
                         const mdi_icon_font_data_t& data) {
  if (!header || header_len < kHeaderBytes) return HeaderCheck::Short;
  if (memcmp(header, "HTIF", 4) != 0) return HeaderCheck::Magic;
  if (crc32(0, header, 28) != u32(header + 28)) return HeaderCheck::HeaderCrc;
  if (u16(header + 4) != kFormat) return HeaderCheck::Format;
  if (u16(header + 6) != data.px) return HeaderCheck::PixelSize;
  if (u32(header + 8) != data.glyph_count) return HeaderCheck::GlyphCount;
  if (u32(header + 12) != data.bitmap_bytes || file_bytes != kHeaderBytes + data.bitmap_bytes) {
    return HeaderCheck::Length;
  }
  if (u32(header + 16) != data.table_crc) return HeaderCheck::Table;
  return HeaderCheck::Ok;
}

uint32_t header_bitmap_crc(const uint8_t* header) { return header ? u32(header + 20) : 0; }

const char* header_check_text(HeaderCheck check) {
  switch (check) {
    case HeaderCheck::Ok: return "ok";
    case HeaderCheck::Short: return "too short";
    case HeaderCheck::Magic: return "not an icon file";
    case HeaderCheck::HeaderCrc: return "damaged header";
    case HeaderCheck::Format: return "unknown format";
    case HeaderCheck::PixelSize: return "icons of another size";
    case HeaderCheck::GlyphCount: return "another icon set";
    case HeaderCheck::Length: return "wrong length";
    case HeaderCheck::Table: return "another icon set";
  }
  return "unknown";
}

uint32_t crc32(uint32_t crc, const uint8_t* bytes, size_t len) {
  static const uint32_t kNibble[16] = {
      0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
      0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
  };
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= bytes[i];
    crc = kNibble[crc & 0x0F] ^ (crc >> 4);
    crc = kNibble[crc & 0x0F] ^ (crc >> 4);
  }
  return ~crc;
}

}  // namespace mdi_icon_cache

extern "C" const uint8_t* mdi_icon_loader_bitmap(lv_font_fmt_txt_glyph_loader_t* base, uint32_t gid) {
  auto* loader = reinterpret_cast<mdi_icon_loader_t*>(base);
  if (!loader || !loader->data || gid == 0 || gid >= loader->data->glyph_count) return nullptr;
  const mdi_icon_font_data_t& data = *loader->data;
  if (const uint8_t* core = mdi_icon_cache::core_bitmap(data, gid)) return core;
  mdi_icon_cache::State* s = mdi_icon_cache::state_of(loader);
  if (!s) return nullptr;
  if (const uint16_t slot = s->slot_of_gid[gid]) return s->bitmaps + s->entries[slot - 1].offset;
  return s->has_source ? mdi_icon_cache::load(s, data, gid) : nullptr;
}

extern "C" bool mdi_icon_loader_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc_out,
                                          uint32_t letter, uint32_t letter_next) {
  if (!lv_font_get_glyph_dsc_fmt_txt(font, dsc_out, letter, letter_next)) return false;
  const auto* fdsc = static_cast<const lv_font_fmt_txt_dsc_t*>(font->dsc);
  const auto* loader = reinterpret_cast<const mdi_icon_loader_t*>(fdsc->glyph_bitmap);
  if (mdi_icon_cache::available(loader, dsc_out->gid.index)) return true;
  // Neither built in nor loadable: draw the unknown-name icon in its place.
  const mdi_icon_cache::State* s = mdi_icon_cache::state_of(loader);
  if (s && s->unknown_codepoint && letter != s->unknown_codepoint) {
    lv_font_get_glyph_dsc_fmt_txt(font, dsc_out, s->unknown_codepoint, 0);
  }
  return true;
}
