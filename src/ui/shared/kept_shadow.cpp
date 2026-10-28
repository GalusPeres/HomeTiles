#include "src/ui/shared/kept_shadow.h"

#include <lvgl_private.h>
#include <string.h>

namespace kept_shadow {

#if defined(LV_DRAW_SW_SHADOW_CACHE_SIZE) && LV_DRAW_SW_SHADOW_CACHE_SIZE > 0 && LV_DRAW_SW_COMPLEX

namespace {

// Shadows kept at once: the popup card's and a few menus'.
constexpr int kMaxSets = 4;
// Rows of a side strip blended at once (a band is taller only on narrow areas).
constexpr int32_t kStripRows = 64;

}  // namespace

// LVGL's corner blur of one shadow (lv_draw_sw_box_shadow's sh_buf, size x
// size) laid out as each part of the shadow reads it: top right as LVGL keeps
// it, bottom right with its rows from the end, the left corners mirrored, and
// the side strips as kStripRows copies of the row LVGL repeats on every line.
struct Set {
  int32_t size;
  int32_t radius;
  uint32_t used;
  uint8_t* data;
  const uint8_t* top_right;
  const uint8_t* bottom_right;
  const uint8_t* top_left;
  const uint8_t* bottom_left;
  const uint8_t* right;
  const uint8_t* left;
};

namespace {

Set g_sets[kMaxSets] = {};
uint32_t g_use = 0;
Stats g_stats = {};

uint32_t set_bytes(int32_t size) {
  return static_cast<uint32_t>(size) * static_cast<uint32_t>(size) * 4U +
         2U * static_cast<uint32_t>(kStripRows) * static_cast<uint32_t>(size);
}

void release(Set& set) {
  if (!set.data) return;
  g_stats.bytes -= set_bytes(set.size);
  lv_free(set.data);
  set.data = nullptr;
}

bool build(Set& set, int32_t size, int32_t radius, const uint8_t* blur) {
  const uint32_t square = static_cast<uint32_t>(size) * static_cast<uint32_t>(size);
  uint8_t* data = static_cast<uint8_t*>(lv_malloc(set_bytes(size)));
  if (!data) return false;
  uint8_t* top_right = data;
  uint8_t* bottom_right = top_right + square;
  uint8_t* top_left = bottom_right + square;
  uint8_t* bottom_left = top_left + square;
  uint8_t* right = bottom_left + square;
  uint8_t* left = right + kStripRows * size;
  for (int32_t row = 0; row < size; ++row) {
    for (int32_t col = 0; col < size; ++col) {
      const uint8_t value = blur[row * size + col];
      top_right[row * size + col] = value;
      bottom_right[(size - 1 - row) * size + col] = value;
      top_left[row * size + (size - 1 - col)] = value;
      bottom_left[(size - 1 - row) * size + (size - 1 - col)] = value;
    }
  }
  for (int32_t row = 0; row < kStripRows; ++row) {
    memcpy(right + row * size, top_right + (size - 1) * size, size);
    memcpy(left + row * size, top_left + (size - 1) * size, size);
  }
  set.size = size;
  set.radius = radius;
  set.data = data;
  set.top_right = top_right;
  set.bottom_right = bottom_right;
  set.top_left = top_left;
  set.bottom_left = bottom_left;
  set.right = right;
  set.left = left;
  g_stats.bytes += set_bytes(size);
  ++g_stats.built;
  return true;
}

// The geometry lv_draw_sw_box_shadow computes for a task.
struct Geometry {
  lv_area_t shadow;  // its bounding box
  lv_area_t bg;      // the background, 1 px smaller
  int32_t r_bg;
  int32_t r_sh;
  int32_t size;      // the corner size
  int32_t w_half;
  int32_t h_half;
};

int32_t clamp_radius(int32_t radius, const lv_area_t& area) {
  const int32_t short_side = LV_MIN(lv_area_get_width(&area), lv_area_get_height(&area));
  return radius > (short_side >> 1) ? (short_side >> 1) : radius;
}

Geometry geometry(const lv_draw_task_t* task) {
  const auto* dsc = static_cast<const lv_draw_box_shadow_dsc_t*>(task->draw_dsc);
  const lv_area_t& coords = task->area;
  const lv_area_t core = {coords.x1 + dsc->ofs_x - dsc->spread, coords.y1 + dsc->ofs_y - dsc->spread,
                          coords.x2 + dsc->ofs_x + dsc->spread, coords.y2 + dsc->ofs_y + dsc->spread};
  Geometry g;
  g.shadow = {core.x1 - dsc->width / 2 - 1, core.y1 - dsc->width / 2 - 1, core.x2 + dsc->width / 2 + 1,
              core.y2 + dsc->width / 2 + 1};
  g.bg = coords;
  lv_area_increase(&g.bg, -1, -1);
  g.r_bg = clamp_radius(dsc->radius, g.bg);
  g.r_sh = clamp_radius(dsc->radius, core);
  g.size = dsc->width + g.r_sh;
  g.w_half = g.shadow.x1 + lv_area_get_width(&g.shadow) / 2;
  g.h_half = g.shadow.y1 + lv_area_get_height(&g.shadow) / 2;
  return g;
}

// A part of the shadow in the task's clip area, unless the background covers
// it (lv_draw_sw_box_shadow skips such parts).
bool shows(const lv_draw_task_t* task, const Geometry& g, const lv_area_t& part, lv_area_t& shown) {
  return lv_area_intersect(&shown, &part, &task->clip_area) && !lv_area_is_in(&shown, &g.bg, g.r_bg);
}

// A part blended in one step from its kept values, the image's top left at
// (x, y); LVGL blends the same values row by row.
void blend_masked(lv_draw_task_t* task, const lv_draw_box_shadow_dsc_t* dsc, const lv_area_t& shown,
                  const uint8_t* image, int32_t x, int32_t y, int32_t stride, int32_t rows) {
  const lv_area_t image_area = {x, y, x + stride - 1, y + rows - 1};
  lv_draw_sw_blend_dsc_t blend;
  lv_memzero(&blend, sizeof(blend));
  blend.color = dsc->color;
  blend.opa = dsc->opa;
  blend.mask_buf = image;
  blend.mask_area = &image_area;
  blend.mask_stride = stride;
  blend.mask_res = LV_DRAW_SW_MASK_RES_CHANGED;
  blend.blend_area = &shown;
  lv_draw_sw_blend(task, &blend);
}

// A side strip: the same row on every line, kStripRows lines per blend.
void blend_strip(lv_draw_task_t* task, const lv_draw_box_shadow_dsc_t* dsc, const lv_area_t& shown,
                 const uint8_t* strip, int32_t x, int32_t size) {
  for (int32_t y = shown.y1; y <= shown.y2; y += kStripRows) {
    const lv_area_t part = {shown.x1, y, shown.x2, LV_MIN(y + kStripRows - 1, shown.y2)};
    blend_masked(task, dsc, part, strip, x, y, size, kStripRows);
  }
}

// The top or bottom side: one value per row (the corner blur's first column,
// counted from that edge), blended as the row's opacity like LVGL does.
void blend_side(lv_draw_task_t* task, const lv_draw_box_shadow_dsc_t* dsc, const lv_area_t& shown,
                const uint8_t* blur, int32_t size, int32_t edge_y, bool top) {
  const bool cover = dsc->opa > LV_OPA_MAX;
  lv_area_t row = shown;
  lv_draw_sw_blend_dsc_t blend;
  lv_memzero(&blend, sizeof(blend));
  blend.color = dsc->color;
  blend.blend_area = &row;
  for (int32_t y = shown.y1; y <= shown.y2; ++y) {
    const uint8_t value = blur[(top ? y - edge_y : edge_y - y) * size];
    blend.opa = cover ? value : static_cast<lv_opa_t>((static_cast<int32_t>(value) * dsc->opa) >> 8);
    row.y1 = y;
    row.y2 = y;
    lv_draw_sw_blend(task, &blend);
  }
}

}  // namespace

const Set* find(const lv_draw_task_t* task) {
  if (task->type != LV_DRAW_TASK_TYPE_BOX_SHADOW) return nullptr;
  const auto* dsc = static_cast<const lv_draw_box_shadow_dsc_t*>(task->draw_dsc);
  if (!dsc || !dsc->bg_cover || dsc->width <= 0) {
    ++g_stats.lvgl;
    return nullptr;
  }
  const Geometry g = geometry(task);
  const lv_draw_sw_shadow_cache_t& cache = LV_GLOBAL_DEFAULT()->sw_shadow_cache;
  // LVGL keeps only corners that fit its cache; larger ones it blurs anew.
  if (g.size <= 0 || static_cast<uint32_t>(g.size) * static_cast<uint32_t>(g.size) >= sizeof(cache.cache)) {
    ++g_stats.lvgl;
    return nullptr;
  }
  // A centre the background does not cover LVGL draws with its own masks.
  const lv_area_t centre = {g.shadow.x1 + g.size, LV_MIN(g.shadow.y1 + g.size, g.h_half + 1), g.shadow.x2 - g.size,
                            LV_MAX(g.shadow.y2 - g.size, g.h_half)};
  if (centre.x1 <= centre.x2 && centre.y1 <= centre.y2 && !lv_area_is_in(&centre, &g.bg, g.r_bg)) {
    ++g_stats.lvgl;
    return nullptr;
  }
  Set* oldest = nullptr;
  for (Set& set : g_sets) {
    if (set.data && set.size == g.size && set.radius == g.r_sh) {
      set.used = ++g_use;
      return &set;
    }
    if (!oldest || !set.data || (oldest->data && set.used < oldest->used)) oldest = &set;
  }
  // The blur LVGL computed for this corner, once it has drawn a band of it.
  if (cache.cache_size != g.size || cache.cache_r != g.r_sh) {
    ++g_stats.lvgl;
    return nullptr;
  }
  release(*oldest);
  if (!build(*oldest, g.size, g.r_sh, cache.cache)) {
    ++g_stats.lvgl;
    return nullptr;
  }
  oldest->used = ++g_use;
  return oldest;
}

// lv_draw_sw_box_shadow's parts in its order, each in one blend (the top and
// bottom side one per row, as LVGL); the bounds are LVGL's, so no part
// overlaps another and each pixel gets LVGL's value.
void draw(lv_draw_task_t* task, const Set& set) {
  ++g_stats.drawn;
  const auto* dsc = static_cast<const lv_draw_box_shadow_dsc_t*>(task->draw_dsc);
  const Geometry g = geometry(task);
  lv_area_t shown;
  if (!lv_area_intersect(&shown, &g.shadow, &task->clip_area)) return;
  const lv_area_t& a = g.shadow;
  const int32_t s = set.size;
  if (shows(task, g, {LV_MAX(a.x2 - s + 1, g.w_half), a.y1, a.x2, LV_MIN(a.y1 + s - 1, g.h_half)}, shown)) {
    blend_masked(task, dsc, shown, set.top_right, a.x2 - s + 1, a.y1, s, s);
  }
  if (shows(task, g, {LV_MAX(a.x2 - s + 1, g.w_half), LV_MAX(a.y2 - s + 1, g.h_half + 1), a.x2, a.y2}, shown)) {
    blend_masked(task, dsc, shown, set.bottom_right, a.x2 - s + 1, a.y2 - s + 1, s, s);
  }
  if (shows(task, g, {a.x1 + s, a.y1, a.x2 - s, LV_MIN(a.y1 + s - 1, g.h_half)}, shown)) {
    blend_side(task, dsc, shown, set.top_right, s, a.y1, true);
  }
  if (shows(task, g, {a.x1 + s, LV_MAX(a.y2 - s + 1, g.h_half + 1), a.x2 - s, a.y2}, shown)) {
    blend_side(task, dsc, shown, set.top_right, s, a.y2, false);
  }
  if (shows(task, g, {LV_MAX(a.x2 - s + 1, g.w_half), LV_MIN(a.y1 + s, g.h_half + 1), a.x2, LV_MAX(a.y2 - s, g.h_half)},
            shown)) {
    blend_strip(task, dsc, shown, set.right, a.x2 - s + 1, s);
  }
  if (shows(task, g, {a.x1, LV_MIN(a.y1 + s, g.h_half + 1), LV_MIN(a.x1 + s - 1, g.w_half - 1), LV_MAX(a.y2 - s, g.h_half)},
            shown)) {
    blend_strip(task, dsc, shown, set.left, a.x1, s);
  }
  if (shows(task, g, {a.x1, a.y1, LV_MIN(a.x1 + s - 1, g.w_half - 1), LV_MIN(a.y1 + s - 1, g.h_half)}, shown)) {
    blend_masked(task, dsc, shown, set.top_left, a.x1, a.y1, s, s);
  }
  if (shows(task, g, {a.x1, LV_MAX(a.y2 - s + 1, g.h_half + 1), LV_MIN(a.x1 + s - 1, g.w_half - 1), a.y2}, shown)) {
    blend_masked(task, dsc, shown, set.bottom_left, a.x1, a.y2 - s + 1, s, s);
  }
}

Stats stats() {
  Stats out = g_stats;
  out.sets = 0;
  for (const Set& set : g_sets) out.sets += set.data ? 1 : 0;
  return out;
}

#else

struct Set {};

const Set* find(const lv_draw_task_t*) { return nullptr; }
void draw(lv_draw_task_t*, const Set&) {}
Stats stats() { return {}; }

#endif

}  // namespace kept_shadow
