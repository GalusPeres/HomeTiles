#include "src/ui/shared/rounded_corners.h"

#include <lvgl_private.h>
#include <string.h>

#include "src/core/display/display_flush_gate.h"
#include "src/ui/shared/kept_shadow.h"

namespace rounded_corners {
namespace {

// Corner sets kept at once: the card fill and its line, the icon discs of
// both sizes and a few pressed or fading opacities.
constexpr int kMaxSets = 24;
// LVGL draws a border's corners together with its straight parts when they
// are closer than this (lv_draw_sw_border.c SPLIT_LIMIT); those stay with LVGL.
constexpr int32_t kSplitLimit = 50;

enum Kind : uint8_t { kFill, kBorder };

struct CornerSet {
  uint8_t kind;
  uint8_t level;  // fill: the opacity in its coverage; border: its width
  int16_t radius;
  uint32_t used_frame;
  uint8_t* coverage;  // four radius x radius A8 squares: top left, top right, bottom left, bottom right
  lv_image_dsc_t corner[4];
};

// In LVGL's heap (PSRAM on the panels), made with the first corner set: the
// SRAM draw band gets the internal RAM left above its reserve (S3 b605).
CornerSet* g_sets = nullptr;
int g_set_count = 0;
uint32_t g_frame = 1;
Stats g_stats = {};
bool g_frame_hook = false;

void frame_cb(lv_event_t*) { ++g_frame; }

// The four corners of a box of side 2r + 4 (the corners never touch), each row
// started and masked exactly like lv_draw_sw_fill / lv_draw_sw_border do it.
bool build(CornerSet& set) {
  const int32_t r = set.radius;
  const uint32_t square = static_cast<uint32_t>(r) * static_cast<uint32_t>(r);
  uint8_t* coverage = static_cast<uint8_t*>(lv_malloc(square * 4));
  if (!coverage) return false;
  const int32_t side = 2 * r + 4;
  const lv_area_t outer = {0, 0, side - 1, side - 1};
  lv_draw_sw_mask_radius_param_t outer_mask;
  lv_draw_sw_mask_radius_param_t inner_mask;
  void* masks[3] = {nullptr, nullptr, nullptr};
  uint8_t start = LV_OPA_COVER;
  if (set.kind == kFill) {
    lv_draw_sw_mask_radius_init(&outer_mask, &outer, r, false);
    masks[0] = &outer_mask;
    start = set.level;
  } else {
    const int32_t width = set.level;
    const lv_area_t inner = {width, width, side - 1 - width, side - 1 - width};
    lv_draw_sw_mask_radius_init(&inner_mask, &inner, r > width ? r - width : 0, true);
    lv_draw_sw_mask_radius_init(&outer_mask, &outer, r, false);
    masks[0] = &inner_mask;
    masks[1] = &outer_mask;
  }
  const int32_t corner_x[4] = {0, side - r, 0, side - r};
  const int32_t corner_y[4] = {0, 0, side - r, side - r};
  for (int c = 0; c < 4; ++c) {
    uint8_t* data = coverage + square * c;
    for (int32_t row = 0; row < r; ++row) {
      uint8_t* line = data + row * r;
      memset(line, start, r);
      // A row LVGL skips as transparent draws nothing.
      if (lv_draw_sw_mask_apply(masks, line, corner_x[c], corner_y[c] + row, r) == LV_DRAW_SW_MASK_RES_TRANSP) {
        memset(line, 0, r);
      }
    }
    lv_image_dsc_t& image = set.corner[c];
    memset(&image, 0, sizeof(image));
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_A8;
    image.header.w = r;
    image.header.h = r;
    image.header.stride = r;
    image.data = data;
    image.data_size = square;
  }
  if (set.kind == kBorder) lv_draw_sw_mask_free_param(&inner_mask);
  lv_draw_sw_mask_free_param(&outer_mask);
  set.coverage = coverage;
  g_stats.bytes += square * 4;
  ++g_stats.built;
  return true;
}

void release(CornerSet& set) {
  if (!set.coverage) return;
  // LVGL keeps image headers and decodes by source address; this set's
  // addresses get new corners.
  for (lv_image_dsc_t& image : set.corner) {
    lv_image_cache_drop(&image);
    lv_image_header_cache_drop(&image);
  }
  g_stats.bytes -= static_cast<uint32_t>(set.radius) * static_cast<uint32_t>(set.radius) * 4;
  lv_free(set.coverage);
  set.coverage = nullptr;
}

// A kept set, or a new one. When all sets are kept, the one used longest ago
// is replaced, but never one used in this frame: its corners may still be
// queued (a frame finishes all its draw tasks before the next one starts).
const CornerSet* find(Kind kind, int32_t radius, uint8_t level) {
  if (!g_sets) g_sets = static_cast<CornerSet*>(lv_malloc_zeroed(sizeof(CornerSet) * kMaxSets));
  if (!g_sets) {
    ++g_stats.declined;
    return nullptr;
  }
  CornerSet* oldest = nullptr;
  for (int i = 0; i < g_set_count; ++i) {
    CornerSet& set = g_sets[i];
    if (set.coverage && set.kind == kind && set.radius == radius && set.level == level) {
      set.used_frame = g_frame;
      return &set;
    }
    if (set.used_frame != g_frame && (!oldest || set.used_frame < oldest->used_frame)) oldest = &set;
  }
  CornerSet* slot = nullptr;
  if (g_set_count < kMaxSets) {
    slot = &g_sets[g_set_count++];
  } else if (oldest) {
    release(*oldest);
    slot = oldest;
  }
  if (!slot) {
    ++g_stats.declined;
    return nullptr;
  }
  slot->kind = kind;
  slot->level = level;
  slot->radius = static_cast<int16_t>(radius);
  slot->used_frame = g_frame;
  if (!build(*slot)) return nullptr;
  return slot;
}

int32_t clamped_radius(int32_t radius, const lv_area_t& a) {
  const int32_t short_side = LV_MIN(lv_area_get_width(&a), lv_area_get_height(&a));
  return radius > (short_side >> 1) ? (short_side >> 1) : radius;
}

// The unmasked parts as lv_draw_sw_fill blends a rectangle without radius.
void blend_plain(lv_draw_task_t* task, lv_color_t color, lv_opa_t opa, const lv_area_t& area) {
  if (area.x1 > area.x2 || area.y1 > area.y2) return;
  lv_draw_sw_blend_dsc_t blend;
  lv_memzero(&blend, sizeof(blend));
  blend.color = color;
  blend.opa = opa;
  blend.blend_area = &area;
  lv_draw_sw_blend(task, &blend);
}

// A corner as LVGL blends an A8 image of its coverage in the corner's color.
void blend_corner(lv_draw_task_t* task, lv_color_t color, const lv_image_dsc_t& corner, const lv_area_t& area) {
  lv_draw_sw_blend_dsc_t blend;
  lv_memzero(&blend, sizeof(blend));
  blend.opa = LV_OPA_COVER;
  blend.color = color;
  blend.mask_buf = corner.data;
  blend.mask_area = &area;
  blend.mask_stride = corner.header.stride;
  blend.mask_res = LV_DRAW_SW_MASK_RES_CHANGED;
  blend.blend_area = &area;
  lv_draw_sw_blend(task, &blend);
}

void blend_corners(lv_draw_task_t* task, const CornerSet& set, lv_color_t color, const lv_area_t& a) {
  const int32_t r = set.radius;
  blend_corner(task, color, set.corner[0], {a.x1, a.y1, a.x1 + r - 1, a.y1 + r - 1});
  blend_corner(task, color, set.corner[1], {a.x2 - r + 1, a.y1, a.x2, a.y1 + r - 1});
  blend_corner(task, color, set.corner[2], {a.x1, a.y2 - r + 1, a.x1 + r - 1, a.y2});
  blend_corner(task, color, set.corner[3], {a.x2 - r + 1, a.y2 - r + 1, a.x2, a.y2});
}

// lv_draw_sw_fill with its corner rows from the kept coverage: the rows
// between the corners and the part between the corners of each corner row are
// full.
void draw_fill(lv_draw_task_t* task, const CornerSet& set) {
  const auto* fill = static_cast<const lv_draw_fill_dsc_t*>(task->draw_dsc);
  const lv_area_t& a = task->area;
  const int32_t r = set.radius;
  const lv_opa_t opa = fill->opa >= LV_OPA_MAX ? LV_OPA_COVER : fill->opa;
  blend_plain(task, fill->color, opa, {a.x1, a.y1 + r, a.x2, a.y2 - r});
  blend_plain(task, fill->color, opa, {a.x1 + r, a.y1, a.x2 - r, a.y1 + r - 1});
  blend_plain(task, fill->color, opa, {a.x1 + r, a.y2 - r + 1, a.x2 - r, a.y2});
  blend_corners(task, set, fill->color, a);
}

// lv_draw_sw_border with its corners apart from the straight parts: the
// straight lines unmasked, each corner square from the kept coverage of the
// inner and outer rounded box.
void draw_border(lv_draw_task_t* task, const CornerSet& set) {
  const auto* border = static_cast<const lv_draw_border_dsc_t*>(task->draw_dsc);
  const lv_area_t& a = task->area;
  const int32_t rout = set.radius;
  const int32_t width = set.level;
  const lv_area_t inner = {a.x1 + width, a.y1 + width, a.x2 - width, a.y2 - width};
  const lv_area_t core = {LV_MAX(a.x1 + rout, inner.x1), LV_MAX(a.y1 + rout, inner.y1),
                          LV_MIN(a.x2 - rout, inner.x2), LV_MIN(a.y2 - rout, inner.y2)};
  blend_plain(task, border->color, LV_OPA_COVER, {core.x1, a.y1, core.x2, inner.y1 - 1});
  blend_plain(task, border->color, LV_OPA_COVER, {core.x1, inner.y2 + 1, core.x2, a.y2});
  blend_plain(task, border->color, LV_OPA_COVER, {a.x1, core.y1, inner.x1 - 1, core.y2});
  blend_plain(task, border->color, LV_OPA_COVER, {inner.x2 + 1, core.y1, a.x2, core.y2});
  blend_corners(task, set, border->color, a);
}

// The kept corners a task was marked with (draw_task_cb), else null.
const CornerSet* marked(const lv_draw_task_t* task) {
  if (task->type != LV_DRAW_TASK_TYPE_FILL && task->type != LV_DRAW_TASK_TYPE_BORDER) return nullptr;
  const auto* base = static_cast<const lv_draw_dsc_base_t*>(task->draw_dsc);
  const uintptr_t at = base ? reinterpret_cast<uintptr_t>(base->user_data) : 0;
  const uintptr_t first = reinterpret_cast<uintptr_t>(g_sets);
  if (!g_sets || at < first || at >= first + sizeof(CornerSet) * kMaxSets || (at - first) % sizeof(CornerSet)) {
    return nullptr;
  }
  const CornerSet* set = reinterpret_cast<const CornerSet*>(at);
  return set->coverage ? set : nullptr;
}

int32_t (*g_next_dispatch)(lv_draw_unit_t*, lv_layer_t*) = nullptr;

// With one software draw unit and no OS, LVGL hands the unit the layer's first
// waiting task, which it draws before returning (lv_draw_sw.c dispatch). A
// marked task and a box shadow with kept corners (kept_shadow.h) are drawn
// here the same way, every other one by LVGL.
int32_t corner_dispatch(lv_draw_unit_t* unit, lv_layer_t* layer) {
  // The last band's copy to the panel may still read the draw buffer
  // (display_flush_gate.h); this draws marked tasks itself, so it waits too.
  if (!display_flush_gate::ready()) {
    lv_draw_dispatch_request();
    return 0;
  }
  lv_draw_task_t* task = lv_draw_get_available_task(layer, nullptr, 0);
  const CornerSet* set = task ? marked(task) : nullptr;
  const kept_shadow::Set* shadow = task && !set ? kept_shadow::find(task) : nullptr;
  if (!set && !shadow) return g_next_dispatch(unit, layer);
  if (!lv_draw_layer_alloc_buf(layer)) {
    task->state = LV_DRAW_TASK_STATE_FAILED;
    return LV_DRAW_UNIT_IDLE;
  }
  task->state = LV_DRAW_TASK_STATE_IN_PROGRESS;
  if (shadow) {
    kept_shadow::draw(task, *shadow);
  } else if (task->type == LV_DRAW_TASK_TYPE_FILL) {
    draw_fill(task, *set);
  } else {
    draw_border(task, *set);
  }
  task->state = LV_DRAW_TASK_STATE_FINISHED;
  if (set) ++g_stats.drawn;
  lv_draw_dispatch_request();
  return 1;
}

void install_dispatch() {
  static bool checked = false;
  if (checked) return;
  checked = true;
  lv_draw_unit_t* unit = LV_GLOBAL_DEFAULT()->draw_info.unit_head;
  // Another draw unit could take the task; then LVGL draws every corner.
  if (!unit || unit->next || !unit->dispatch_cb) return;
  g_next_dispatch = unit->dispatch_cb;
  unit->dispatch_cb = corner_dispatch;
}

// Marks a main fill whose corners can come from a kept set.
void mark_fill(lv_draw_task_t* task, lv_draw_fill_dsc_t* fill) {
  if (fill->grad.dir != LV_GRAD_DIR_NONE || fill->opa <= LV_OPA_MIN || fill->radius <= 0) return;
  lv_area_t a;
  lv_draw_task_get_area(task, &a);
  const int32_t r = clamped_radius(fill->radius, a);
  if (r < 2) return;
  const lv_opa_t opa = fill->opa >= LV_OPA_MAX ? LV_OPA_COVER : fill->opa;
  const CornerSet* set = find(kFill, r, opa);
  if (!set) return;
  fill->base.user_data = const_cast<CornerSet*>(set);
  ++g_stats.fills;
}

// Marks a main border that LVGL draws with its corners apart (its own split).
void mark_border(lv_draw_task_t* task, lv_draw_border_dsc_t* border) {
  if (border->side != LV_BORDER_SIDE_FULL || border->opa != LV_OPA_COVER || border->width <= 0 ||
      border->width > 255 || border->radius <= 0) {
    return;
  }
  lv_area_t a;
  lv_draw_task_get_area(task, &a);
  const int32_t rout = clamped_radius(border->radius, a);
  const int32_t width = border->width;
  if (rout < 2 || rout < width) return;
  const lv_area_t inner = {a.x1 + width, a.y1 + width, a.x2 - width, a.y2 - width};
  const int32_t core_w = LV_MIN(a.x2 - rout, inner.x2) - LV_MAX(a.x1 + rout, inner.x1) + 1;
  if (core_w < kSplitLimit || inner.x1 >= inner.x2) return;
  const CornerSet* set = find(kBorder, rout, static_cast<uint8_t>(width));
  if (!set) return;
  border->base.user_data = const_cast<CornerSet*>(set);
  ++g_stats.borders;
}

// LVGL sends this when it adds a draw task, before drawing it.
void draw_task_cb(lv_event_t* e) {
  if (!g_next_dispatch) return;
  lv_draw_task_t* task = lv_event_get_draw_task(e);
  if (!task) return;
  auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
  if (!base || base->part != LV_PART_MAIN || base->drop_shadow_opa || base->user_data) return;
  const lv_draw_task_type_t type = lv_draw_task_get_type(task);
  if (type == LV_DRAW_TASK_TYPE_FILL) {
    mark_fill(task, static_cast<lv_draw_fill_dsc_t*>(static_cast<void*>(base)));
  } else if (type == LV_DRAW_TASK_TYPE_BORDER) {
    mark_border(task, static_cast<lv_draw_border_dsc_t*>(static_cast<void*>(base)));
  }
}

}  // namespace

void attach(lv_obj_t* obj) {
  if (!obj) return;
  const uint32_t events = lv_obj_get_event_count(obj);
  for (uint32_t i = 0; i < events; ++i) {
    lv_event_dsc_t* dsc = lv_obj_get_event_dsc(obj, i);
    if (dsc && lv_event_dsc_get_cb(dsc) == draw_task_cb) return;
  }
  install_dispatch();
  if (!g_frame_hook) {
    if (lv_display_t* display = lv_obj_get_display(obj)) {
      lv_display_add_event_cb(display, frame_cb, LV_EVENT_REFR_START, nullptr);
      g_frame_hook = true;
    }
  }
  lv_obj_add_flag(obj, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
  lv_obj_add_event_cb(obj, draw_task_cb, LV_EVENT_DRAW_TASK_ADDED, nullptr);
}

namespace {

// Every rounded object below `obj`, a few levels deep.
void attach_rounded_children(lv_obj_t* obj, int depth) {
  const uint32_t count = lv_obj_get_child_count(obj);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(obj, static_cast<int32_t>(i));
    if (lv_obj_get_style_radius(child, LV_PART_MAIN) > 0) attach(child);
    if (depth < 6) attach_rounded_children(child, depth + 1);
  }
}

}  // namespace

void attach_tree(lv_obj_t* root) {
  if (!root) return;
  attach(root);
  attach_rounded_children(root, 0);
}

Stats stats() {
  Stats out = g_stats;
  out.masks = 0;
  for (int i = 0; i < g_set_count; ++i) {
    if (g_sets[i].coverage) ++out.masks;
  }
  return out;
}

}  // namespace rounded_corners
