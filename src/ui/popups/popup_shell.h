#pragma once

#include <lvgl.h>

struct PopupShellParts {
  lv_obj_t* overlay;
  lv_obj_t* card;
  lv_obj_t* title;
  lv_obj_t* icon;
  lv_obj_t* close;
};

// Build a cached content owner with the standard Light geometry. Header
// objects retain each type's state; show_popup_shell presents the shared header.
PopupShellParts create_popup_body(lv_event_cb_t close_handler, void* context,
                                 uint32_t color = 0x2A2A2A);

// The UI owns one visible frame/header. Type-specific resident cards supply
// only their cached content and keep their existing event/state ownership.
void show_popup_shell(lv_obj_t* owner_overlay, lv_obj_t* body,
                      lv_obj_t* title, lv_obj_t* icon, lv_obj_t* close,
                     void (*dismiss)() = nullptr);
void hide_popup_shell(lv_obj_t* body);
void sync_popup_shell();

// Register an existing background tree, once after construction. Opaque popup
// pixels can skip its covered draw calls without hiding or rebuilding widgets.
void register_popup_background(lv_obj_t* root);
