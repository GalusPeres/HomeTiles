#include "src/types/scene/picture.h"

#include <esp_heap_caps.h>
#include <libs/tjpgd/tjpgd.h>

#include <cstring>
#include <new>

#include "src/devices/device.h"

namespace scene_picture {
namespace {

// Decoded pixels kept for reuse. A 1x1 tile on a 1280x800 panel is ~200 x 160
// px (64 KB); the S3 panels' tiles are smaller and their PSRAM is shorter.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
constexpr size_t kCacheBudgetBytes = 2u * 1024u * 1024u;
#else
constexpr size_t kCacheBudgetBytes = 384u * 1024u;
#endif
constexpr size_t kMaxEntries = 16;
// LVGL's TJpgDec is built without decode-time scaling (JD_USE_SCALE 0), so a
// picture is decoded at its own size first (Web Admin uploads are at most
// 512 px: 512 KB) and then reduced. Larger sources are refused.
constexpr size_t kMaxSourceBytes = 1024u * 1024u;
constexpr size_t kWorkBytes = 4096;

struct Entry {
  String path;
  uint32_t file_size = 0;
  uint16_t min_w = 0;
  uint16_t min_h = 0;
  uint16_t max_side = 0;
  lv_image_dsc_t dsc{};
  uint16_t refs = 0;
  uint32_t last_used = 0;
  bool used = false;
};

// The entry table lives in PSRAM, made on first use (no static DRAM).
Entry* g_entries = nullptr;
uint32_t g_clock = 0;

bool ensure_entries() {
  if (g_entries) return true;
  void* memory = heap_caps_malloc(sizeof(Entry) * kMaxEntries, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = heap_caps_malloc(sizeof(Entry) * kMaxEntries, MALLOC_CAP_8BIT);
  if (!memory) return false;
  Entry* entries = static_cast<Entry*>(memory);
  for (size_t i = 0; i < kMaxEntries; ++i) new (&entries[i]) Entry();
  g_entries = entries;
  return true;
}
// The last file that failed: a card is rebuilt with every grid, so a bad file
// is reported once until another file fails.
String g_last_failure;

bool first_failure(const String& path) {
  if (g_last_failure == path) return false;
  g_last_failure = path;
  return true;
}

// A file that decodes again may report its next failure.
void forget_failure(const String& path) {
  if (g_last_failure == path) g_last_failure = String();
}

size_t cached_bytes() {
  size_t total = 0;
  for (size_t i = 0; i < kMaxEntries; ++i) {
    if (g_entries[i].used) total += g_entries[i].dsc.data_size;
  }
  return total;
}

void drop(Entry& e) {
  if (e.dsc.data) heap_caps_free(const_cast<uint8_t*>(e.dsc.data));
  e = Entry();
}

// Frees unreferenced pictures, oldest first, until `need` more bytes fit.
void make_room(size_t need) {
  while (cached_bytes() + need > kCacheBudgetBytes) {
    Entry* oldest = nullptr;
    for (size_t i = 0; i < kMaxEntries; ++i) {
      Entry& e = g_entries[i];
      if (e.used && e.refs == 0 && (!oldest || e.last_used < oldest->last_used)) oldest = &e;
    }
    if (!oldest) return;
    drop(*oldest);
  }
}

struct DecodeCtx {
  File* file = nullptr;
  uint16_t* buf = nullptr;
  uint16_t w = 0;
  uint16_t h = 0;
};

size_t jpeg_input(JDEC* jd, uint8_t* buff, size_t ndata) {
  DecodeCtx* ctx = static_cast<DecodeCtx*>(jd->device);
  if (!ctx || !ctx->file) return 0;
  if (buff) return ctx->file->read(buff, ndata);
  ctx->file->seek(ctx->file->position() + ndata);
  return ndata;
}

// TJpgDec delivers RGB888 blocks in output (scaled) coordinates.
int jpeg_output(JDEC* jd, void* bitmap, JRECT* rect) {
  DecodeCtx* ctx = static_cast<DecodeCtx*>(jd->device);
  if (!ctx || !ctx->buf || !bitmap) return 0;
  const uint8_t* src = static_cast<const uint8_t*>(bitmap);
  const uint16_t rw = rect->right - rect->left + 1;
  for (uint16_t y = rect->top; y <= rect->bottom && y < ctx->h; ++y) {
    for (uint16_t x = rect->left; x <= rect->right && x < ctx->w; ++x) {
      const size_t si = (static_cast<size_t>(y - rect->top) * rw + (x - rect->left)) * 3;
      const uint8_t b = src[si], g = src[si + 1], r = src[si + 2];
      const uint16_t c = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      ctx->buf[static_cast<size_t>(y) * ctx->w + x] = static_cast<uint16_t>((c >> 8) | (c << 8));
    }
  }
  return 1;
}

uint16_t reduced(uint16_t size, uint8_t factor) {
  return static_cast<uint16_t>((size + factor - 1u) / factor);
}

// RGB565 (byte-swapped, as LVGL's RGB565_SWAPPED) <-> 8-bit channels.
inline void unpack565(uint16_t swapped, uint32_t& r, uint32_t& g, uint32_t& b) {
  const uint16_t c = static_cast<uint16_t>((swapped >> 8) | (swapped << 8));
  r = (c >> 11) & 0x1F;
  g = (c >> 5) & 0x3F;
  b = c & 0x1F;
}
inline uint16_t pack565(uint32_t r, uint32_t g, uint32_t b) {
  const uint16_t c = static_cast<uint16_t>((r << 11) | (g << 5) | b);
  return static_cast<uint16_t>((c >> 8) | (c << 8));
}

// Area average by an integer factor (edge blocks average what they cover).
uint16_t* box_reduce(const uint16_t* src, uint16_t w, uint16_t h, uint8_t factor, uint16_t& out_w,
                     uint16_t& out_h) {
  out_w = reduced(w, factor);
  out_h = reduced(h, factor);
  uint16_t* out = static_cast<uint16_t*>(
      heap_caps_malloc(static_cast<size_t>(out_w) * out_h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!out) return nullptr;
  for (uint16_t oy = 0; oy < out_h; ++oy) {
    const uint16_t y0 = static_cast<uint16_t>(oy * factor);
    const uint16_t y1 = static_cast<uint16_t>(y0 + factor < h ? y0 + factor : h);
    for (uint16_t ox = 0; ox < out_w; ++ox) {
      const uint16_t x0 = static_cast<uint16_t>(ox * factor);
      const uint16_t x1 = static_cast<uint16_t>(x0 + factor < w ? x0 + factor : w);
      uint32_t rs = 0, gs = 0, bs = 0, n = 0;
      for (uint16_t y = y0; y < y1; ++y) {
        for (uint16_t x = x0; x < x1; ++x) {
          uint32_t r, g, b;
          unpack565(src[static_cast<size_t>(y) * w + x], r, g, b);
          rs += r;
          gs += g;
          bs += b;
          ++n;
        }
      }
      out[static_cast<size_t>(oy) * out_w + ox] = n ? pack565(rs / n, gs / n, bs / n) : 0;
    }
  }
  return out;
}

// Decodes into a new PSRAM buffer; false on any failure (logged once per call).
bool decode(const String& path, uint16_t min_w, uint16_t min_h, uint16_t max_side, lv_image_dsc_t& out,
            uint32_t& file_size) {
  File f = Device::storageFS().open(path, FILE_READ);
  if (!f) {
    if (first_failure(path)) Serial.printf("[ScenePicture] Missing %s\n", path.c_str());
    return false;
  }
  file_size = static_cast<uint32_t>(f.size());
  uint8_t* work = static_cast<uint8_t*>(heap_caps_malloc(kWorkBytes, MALLOC_CAP_8BIT));
  if (!work) {
    f.close();
    return false;
  }
  DecodeCtx ctx;
  ctx.file = &f;
  JDEC jd;
  JRESULT rc = jd_prepare(&jd, jpeg_input, work, kWorkBytes, &ctx);
  if (rc != JDR_OK) {
    // Progressive or otherwise unsupported JPEG (the Web Admin re-encodes
    // uploads as baseline JPEG).
    if (first_failure(path)) {
      Serial.printf("[ScenePicture] Not a baseline JPEG: %s (%d)\n", path.c_str(), static_cast<int>(rc));
    }
    heap_caps_free(work);
    f.close();
    return false;
  }
  ctx.w = jd.width;
  ctx.h = jd.height;
  const size_t bytes = static_cast<size_t>(ctx.w) * ctx.h * 2;
  if (!ctx.w || !ctx.h || bytes > kMaxSourceBytes) {
    if (first_failure(path)) Serial.printf("[ScenePicture] Too large: %s %ux%u\n", path.c_str(), jd.width, jd.height);
    heap_caps_free(work);
    f.close();
    return false;
  }
  make_room(bytes);
  ctx.buf = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!ctx.buf) ctx.buf = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  if (!ctx.buf) {
    if (first_failure(path)) Serial.printf("[ScenePicture] No memory for %u bytes\n", static_cast<unsigned>(bytes));
    heap_caps_free(work);
    f.close();
    return false;
  }
  memset(ctx.buf, 0, bytes);
  rc = jd_decomp(&jd, jpeg_output, 0);
  heap_caps_free(work);
  f.close();
  if (rc != JDR_OK) {
    if (first_failure(path)) Serial.printf("[ScenePicture] Decode failed: %s (%d)\n", path.c_str(), static_cast<int>(rc));
    heap_caps_free(ctx.buf);
    return false;
  }

  uint16_t out_w = ctx.w;
  uint16_t out_h = ctx.h;
  uint16_t* pixels = ctx.buf;
  // Covers: the largest integer reduction that still covers min_w x min_h,
  // so the card scales the picture down at most by less than two.
  if (!max_side) {
    uint8_t factor = 1;
    while (factor < 16 && reduced(out_w, factor + 1) >= min_w && reduced(out_h, factor + 1) >= min_h) ++factor;
    if (factor > 1) {
      uint16_t rw = 0, rh = 0;
      if (uint16_t* small = box_reduce(pixels, out_w, out_h, factor, rw, rh)) {
        heap_caps_free(pixels);
        pixels = small;
        out_w = rw;
        out_h = rh;
      }
    }
  }
  // Icons: an area average close to max_side, then the exact size by
  // nearest neighbour, keeping the aspect ratio.
  if (max_side) {
    const uint16_t longest = out_w >= out_h ? out_w : out_h;
    const uint8_t factor = static_cast<uint8_t>(longest / max_side > 15 ? 15 : longest / max_side);
    if (factor > 1) {
      uint16_t rw = 0, rh = 0;
      if (uint16_t* small = box_reduce(pixels, out_w, out_h, factor, rw, rh)) {
        heap_caps_free(pixels);
        pixels = small;
        out_w = rw;
        out_h = rh;
      }
    }
  }
  if (max_side && (out_w > max_side || out_h > max_side)) {
    const uint16_t dw = out_w >= out_h ? max_side
                                       : static_cast<uint16_t>(static_cast<uint32_t>(out_w) * max_side / out_h);
    const uint16_t dh = out_h > out_w ? max_side
                                      : static_cast<uint16_t>(static_cast<uint32_t>(out_h) * max_side / out_w);
    uint16_t* small = static_cast<uint16_t*>(
        heap_caps_malloc(static_cast<size_t>(dw ? dw : 1) * (dh ? dh : 1) * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (small) {
      const uint16_t sw = dw ? dw : 1, sh = dh ? dh : 1;
      for (uint16_t y = 0; y < sh; ++y) {
        const uint16_t sy = static_cast<uint16_t>(static_cast<uint32_t>(y) * out_h / sh);
        for (uint16_t x = 0; x < sw; ++x) {
          const uint16_t sx = static_cast<uint16_t>(static_cast<uint32_t>(x) * out_w / sw);
          small[static_cast<size_t>(y) * sw + x] = pixels[static_cast<size_t>(sy) * out_w + sx];
        }
      }
      heap_caps_free(pixels);
      pixels = small;
      out_w = sw;
      out_h = sh;
    }
  }

  memset(&out, 0, sizeof(out));
  out.header.magic = LV_IMAGE_HEADER_MAGIC;
  out.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  out.header.w = out_w;
  out.header.h = out_h;
  out.header.stride = out_w * 2;
  out.data_size = static_cast<uint32_t>(out_w) * out_h * 2;
  out.data = reinterpret_cast<const uint8_t*>(pixels);
  forget_failure(path);
  Serial.printf("[ScenePicture] Decoded %s %ux%u -> %ux%u\n", path.c_str(), jd.width, jd.height, out_w, out_h);
  return true;
}

}  // namespace

bool valid_path(const String& path) {
  if (path.length() <= strlen(kDir) + 1 || path.length() > kMaxPathLength) return false;
  if (!path.startsWith(String(kDir) + "/")) return false;
  const String name = path.substring(strlen(kDir) + 1);
  if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.indexOf("..") >= 0) return false;
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

const lv_image_dsc_t* acquire(const String& path, uint16_t min_w, uint16_t min_h, uint16_t max_side) {
  if (!valid_path(path) || !Device::storageReady() || !ensure_entries()) return nullptr;
  ++g_clock;
  // A file replaced under the same name has another size: decode it again.
  uint32_t current_size = 0;
  {
    File f = Device::storageFS().open(path, FILE_READ);
    if (!f) return nullptr;
    current_size = static_cast<uint32_t>(f.size());
    f.close();
  }
  for (size_t i = 0; i < kMaxEntries; ++i) {
    Entry& e = g_entries[i];
    if (e.used && e.path == path && e.min_w == min_w && e.min_h == min_h && e.max_side == max_side &&
        e.file_size == current_size) {
      ++e.refs;
      e.last_used = g_clock;
      return &e.dsc;
    }
  }
  Entry* slot = nullptr;
  for (size_t i = 0; i < kMaxEntries && !slot; ++i) {
    if (!g_entries[i].used) slot = &g_entries[i];
  }
  if (!slot) {
    for (size_t i = 0; i < kMaxEntries; ++i) {
      Entry& e = g_entries[i];
      if (e.refs == 0 && (!slot || e.last_used < slot->last_used)) slot = &e;
    }
    if (!slot) return nullptr;  // every slot is on screen
    drop(*slot);
  }
  lv_image_dsc_t dsc;
  uint32_t file_size = 0;
  if (!decode(path, min_w, min_h, max_side, dsc, file_size)) return nullptr;
  slot->used = true;
  slot->path = path;
  slot->file_size = file_size;
  slot->min_w = min_w;
  slot->min_h = min_h;
  slot->max_side = max_side;
  slot->dsc = dsc;
  slot->refs = 1;
  slot->last_used = g_clock;
  return &slot->dsc;
}

void release(const lv_image_dsc_t* dsc) {
  if (!dsc || !g_entries) return;
  for (size_t i = 0; i < kMaxEntries; ++i) {
    Entry& e = g_entries[i];
    if (e.used && &e.dsc == dsc) {
      if (e.refs) --e.refs;
      return;
    }
  }
}

void release_on_delete(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  release(static_cast<const lv_image_dsc_t*>(lv_event_get_user_data(e)));
}

}  // namespace scene_picture
