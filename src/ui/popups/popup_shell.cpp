#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/tiles/icons/mdi_icons.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <new>
#include <cstring>

namespace {
struct Binding {
  lv_obj_t* owner = nullptr;
  lv_obj_t* body = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* close = nullptr;
  int32_t border_width = 0, radius = 0, shadow_width = 0, shadow_spread = 0;
  lv_color_t border_color{}, shadow_color{};
  void (*dismiss)() = nullptr;
  bool owner_deleting = false;
  lv_opa_t border_opa = LV_OPA_TRANSP, shadow_opa = LV_OPA_TRANSP;
};
struct Shell {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* frame = nullptr;
  lv_obj_t* header = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* close = nullptr;
  Binding* active = nullptr;
} shell;

class SceneChange {
 public:
  SceneChange() : display_(lv_obj_get_display(shell.overlay)) {
    lv_display_enable_invalidation(display_, false);
  }
  ~SceneChange() { lv_display_enable_invalidation(display_, true); }
 private:
  lv_display_t* display_;
};

void invalidate_shell() {
  // Transparent input overlays cover the display for hit testing only. Drawing
  // their full bounds would repaint every cached tile behind a small popup.
  if (lv_obj_get_style_bg_opa(shell.overlay, LV_PART_MAIN) != LV_OPA_TRANSP)
    lv_obj_invalidate(shell.overlay);
  else lv_obj_invalidate(shell.frame);
}

void draw_popup_background(lv_event_t* event) {
  if (!shell.active) return;
  // A shadow makes LVGL's refresh band wider than the opaque frame, so its
  // whole-band cover test cannot skip the background. Check each widget's
  // clipped draw area instead, before decoding its text or drawing its fill.
  const auto* layer = lv_event_get_layer(event);
  // Child layers may be translucent or use transformed coordinates.
  if (layer->parent) return;
  const auto& clip = layer->_clip_area;
  lv_area_t frame;
  lv_obj_get_coords(shell.frame, &frame);
  if (clip.x1 < frame.x1 || clip.x2 > frame.x2 ||
      clip.y1 < frame.y1 || clip.y2 > frame.y2) return;
  const int radius = lv_obj_get_style_radius(shell.frame, LV_PART_MAIN);
  if ((clip.x1 >= frame.x1 + radius && clip.x2 <= frame.x2 - radius) ||
      (clip.y1 >= frame.y1 + radius && clip.y2 <= frame.y2 - radius))
    lv_event_stop_processing(event);
}

void create_header(lv_obj_t* parent, lv_obj_t*& title, lv_obj_t*& icon,
                   lv_obj_t*& close, lv_event_cb_t handler, void* context) {
  title = lv_label_create(parent);
  lv_obj_set_width(title, LV_PCT(62));
  lv_obj_set_style_text_font(title, popup_layout::headerTitleFont(), 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_label_set_text(title, "");
  icon = lv_label_create(parent);
  lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(icon);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);
  lv_label_set_text(icon, "");
  close = popup_layout::createCloseButton(parent, handler, context);
  for (auto* object : {title, icon, close})
    lv_obj_add_flag(object, LV_OBJ_FLAG_IGNORE_LAYOUT);
  popup_layout::alignHeader(parent, title, icon);
}

void detach() {
  auto* binding = shell.active;
  shell.active = nullptr;
  if (!binding) return;
  cancel_popup_open(binding->body);
  lv_obj_add_flag(binding->body, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_parent(binding->body, binding->owner);
  lv_obj_center(binding->body);
  lv_obj_remove_flag(binding->owner, LV_OBJ_FLAG_CLICKABLE);
}

void owner_deleted(lv_event_t* event) {
  auto* binding = static_cast<Binding*>(lv_event_get_user_data(event));
  binding->owner_deleting = true;
  if (lv_obj_get_parent(binding->body) != binding->owner)
    lv_obj_delete(binding->body);
}

void body_deleted(lv_event_t* event) {
  auto* binding = static_cast<Binding*>(lv_event_get_user_data(event));
  if (shell.active == binding) {
    shell.active = nullptr;
    if (shell.overlay) lv_obj_add_flag(shell.overlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (!binding->owner_deleting)
    lv_obj_remove_event_cb_with_user_data(binding->owner, owner_deleted, binding);
  binding->~Binding();
  heap_caps_free(binding);
}

void shell_deleted(lv_event_t*) {
  // Preserve cached bodies if the active screen itself is replaced.
  detach();
  shell = {};
}

void close_clicked(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !shell.active) return;
  auto* close = shell.active->close;
  // Existing callbacks still own command completion, PIN handling and cleanup.
  if (close) lv_obj_send_event(close, LV_EVENT_CLICKED, nullptr);
  // The original handler may refuse closing (PIN) or navigate back (Wi-Fi).
  sync_popup_shell();
}

Binding* bind(lv_obj_t* owner, lv_obj_t* body, lv_obj_t* title,
               lv_obj_t* icon, lv_obj_t* close) {
  for (uint32_t i = 0; i < lv_obj_get_event_count(body); ++i) {
    auto* event = lv_obj_get_event_dsc(body, i);
    if (lv_event_dsc_get_cb(event) == body_deleted)
      return static_cast<Binding*>(lv_event_dsc_get_user_data(event));
  }
  void* memory = heap_caps_malloc(sizeof(Binding), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) return nullptr;
  auto* b = new (memory) Binding{};
  b->owner = owner; b->body = body; b->title = title; b->icon = icon; b->close = close;
  b->border_width = lv_obj_get_style_border_width(body, LV_PART_MAIN);
  b->border_color = lv_obj_get_style_border_color(body, LV_PART_MAIN);
  b->border_opa = lv_obj_get_style_border_opa(body, LV_PART_MAIN);
  b->radius = lv_obj_get_style_radius(body, LV_PART_MAIN);
  b->shadow_width = lv_obj_get_style_shadow_width(body, LV_PART_MAIN);
  b->shadow_spread = lv_obj_get_style_shadow_spread(body, LV_PART_MAIN);
  b->shadow_color = lv_obj_get_style_shadow_color(body, LV_PART_MAIN);
  b->shadow_opa = lv_obj_get_style_shadow_opa(body, LV_PART_MAIN);
  lv_obj_add_event_cb(body, body_deleted, LV_EVENT_DELETE, b);
  lv_obj_add_event_cb(owner, owner_deleted, LV_EVENT_DELETE, b);
  // Original header labels remain lightweight metadata holders for existing
  // cached-state paths. Their geometry is unchanged; only the shared header draws.
  for (auto* object : {title, icon, close})
    if (object) lv_obj_set_style_opa(object, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(body, LV_OPA_TRANSP, 0);
  return b;
}

void ensure_shell() {
  if (shell.overlay) return;
  shell.overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(shell.overlay);
  lv_obj_set_size(shell.overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(shell.overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(shell.overlay, LV_OBJ_FLAG_CLICKABLE);
  shell.frame = lv_obj_create(shell.overlay);
  lv_obj_remove_style_all(shell.frame);
  lv_obj_set_style_bg_opa(shell.frame, LV_OPA_COVER, 0);
  lv_obj_remove_flag(shell.frame, LV_OBJ_FLAG_SCROLLABLE);
  shell.header = lv_obj_create(shell.overlay);
  lv_obj_remove_style_all(shell.header);
  lv_obj_remove_flag(shell.header, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(shell.header, LV_OBJ_FLAG_SCROLLABLE);
  create_header(shell.header, shell.title, shell.icon, shell.close, close_clicked, nullptr);
  lv_obj_add_event_cb(shell.overlay, shell_deleted, LV_EVENT_DELETE, nullptr);
}

void copy_label(lv_obj_t* target, lv_obj_t* source, bool title) {
  if (!source || lv_obj_has_flag(source, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(target, LV_OBJ_FLAG_HIDDEN);
  const auto* font = lv_obj_get_style_text_font(source, LV_PART_MAIN);
  if (font != lv_obj_get_style_text_font(target, LV_PART_MAIN))
    lv_obj_set_style_text_font(target, font, 0);
  const char* text = title ? hometiles_title::text(source) : lv_label_get_text(source);
  const char* current = title ? hometiles_title::text(target) : lv_label_get_text(target);
  if (strcmp(text, current) != 0) {
    if (title) hometiles_title::set(target, text, true);
    else lv_label_set_text(target, text);
  }
  const auto color = lv_obj_get_style_text_color(source, LV_PART_MAIN);
  if (!lv_color_eq(color, lv_obj_get_style_text_color(target, LV_PART_MAIN)))
    lv_obj_set_style_text_color(target, color, 0);

}
}

void register_popup_background(lv_obj_t* root) {
  if (!root) return;
  lv_obj_add_event_cb(root, draw_popup_background,
      static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), nullptr);
  for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
    register_popup_background(lv_obj_get_child(root, i));
}

PopupShellParts create_popup_body(lv_event_cb_t close_handler, void* context,
                                 uint32_t color) {
  PopupShellParts parts{};
  parts.overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(parts.overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(parts.overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(parts.overlay, 0, 0);
  lv_obj_remove_flag(parts.overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(parts.overlay, LV_OBJ_FLAG_CLICKABLE);

  parts.card = lv_obj_create(parts.overlay);
  lv_obj_set_size(parts.card, popup_layout::kCardWidth, popup_layout::kCardHeight);
  lv_obj_center(parts.card);
  lv_obj_set_style_bg_color(parts.card, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(parts.card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(parts.card, popup_layout::kCardRadius, 0);
  lv_obj_set_style_border_width(parts.card, 0, 0);
  ui_surface_style::apply_global_tile_border(parts.card);
  lv_obj_set_style_pad_all(parts.card, popup_layout::kCardPad, 0);
  lv_obj_set_style_shadow_width(parts.card, popup_layout::scale480(28), 0);
  lv_obj_set_style_shadow_color(parts.card, lv_color_black(), 0);
  lv_obj_set_style_shadow_opa(parts.card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(parts.card, popup_layout::scale480(2), 0);
  lv_obj_remove_flag(parts.card, LV_OBJ_FLAG_SCROLLABLE);
  create_header(parts.card, parts.title, parts.icon, parts.close, close_handler, context);
  return parts;
}

void show_popup_shell(lv_obj_t* owner, lv_obj_t* body, lv_obj_t* title,
                       lv_obj_t* icon, lv_obj_t* close, void (*dismiss)()) {
  if (!owner || !body || !close) return;
  Binding* binding = bind(owner, body, title, icon, close);
  if (!binding) return;
  ensure_shell();
  if (shell.active && shell.active != binding) {
    auto dismiss_previous = shell.active->dismiss;
    if (dismiss_previous) dismiss_previous();
  }
  invalidate_shell();
  {
    // Reparenting and stacking also invalidate the full parent in LVGL. These
    // ownership changes are invisible; redraw just the old and new popup bounds.
    SceneChange change;
    if (shell.active && shell.active != binding) detach();
    binding->dismiss = dismiss;
    shell.active = binding;
    lv_obj_set_parent(owner, lv_layer_top());
    lv_obj_add_flag(owner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(owner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_parent(shell.overlay, lv_screen_active());
    lv_obj_set_style_pad_all(shell.overlay, lv_obj_get_style_pad_top(owner, LV_PART_MAIN), 0);
    lv_obj_set_style_bg_color(shell.overlay, lv_obj_get_style_bg_color(owner, LV_PART_MAIN), 0);
    lv_obj_set_style_bg_opa(shell.overlay, lv_obj_get_style_bg_opa(owner, LV_PART_MAIN), 0);
    lv_obj_set_parent(body, shell.overlay);
    lv_obj_center(body);
    lv_obj_clear_flag(shell.overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(shell.overlay);
    lv_obj_move_foreground(shell.header);
    for (auto* object : {shell.frame, shell.header}) {
      lv_obj_set_size(object, lv_obj_get_style_width(body, LV_PART_MAIN),
                       lv_obj_get_style_height(body, LV_PART_MAIN));
      lv_obj_center(object);
      lv_obj_set_style_pad_all(object, lv_obj_get_style_pad_top(body, LV_PART_MAIN), 0);
    }
    lv_obj_set_style_border_width(shell.header, binding->border_width, 0);
    lv_obj_set_style_border_opa(shell.header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(shell.frame, binding->radius, 0);
    lv_obj_set_style_border_width(shell.frame, binding->border_width, 0);
    lv_obj_set_style_border_color(shell.frame, binding->border_color, 0);
    lv_obj_set_style_border_opa(shell.frame, binding->border_opa, 0);
    lv_obj_set_style_shadow_width(shell.frame, binding->shadow_width, 0);
    lv_obj_set_style_shadow_spread(shell.frame, binding->shadow_spread, 0);
    lv_obj_set_style_shadow_color(shell.frame, binding->shadow_color, 0);
    lv_obj_set_style_shadow_opa(shell.frame, binding->shadow_opa, 0);
    sync_popup_shell();
  }
  invalidate_shell();
}

void hide_popup_shell(lv_obj_t* body) {
  if (!shell.active || shell.active->body != body) return;
  invalidate_shell();
  SceneChange change;
  detach();
  lv_obj_add_flag(shell.overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_parent(shell.overlay, lv_layer_top());
}

void sync_popup_shell() {
  if (!shell.active) return;
  auto* body = shell.active->body;
  if (lv_obj_has_flag(body, LV_OBJ_FLAG_HIDDEN)) { hide_popup_shell(body); return; }
  const auto color = lv_obj_get_style_bg_color(body, LV_PART_MAIN);
  if (!lv_color_eq(color, lv_obj_get_style_bg_color(shell.frame, LV_PART_MAIN)))
    lv_obj_set_style_bg_color(shell.frame, color, 0);
  copy_label(shell.title, shell.active->title, true);
  copy_label(shell.icon, shell.active->icon, false);
  popup_layout::alignHeader(shell.header, shell.title, shell.icon);
  copy_label(lv_obj_get_child(shell.close, 0), lv_obj_get_child(shell.active->close, 0), false);
  if (lv_obj_has_state(shell.active->close, LV_STATE_DISABLED))
    lv_obj_add_state(shell.close, LV_STATE_DISABLED);
  else lv_obj_remove_state(shell.close, LV_STATE_DISABLED);
}
