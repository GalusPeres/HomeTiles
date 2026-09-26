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
// `value` is an optional hidden label holding the entity's current value.
// While it has text, the header shows a smaller one-line title with the value
// below it; popups without a value keep the classic header.
void show_popup_shell(lv_obj_t* owner_overlay, lv_obj_t* body,
                      lv_obj_t* title, lv_obj_t* icon, lv_obj_t* close,
                     void (*dismiss)() = nullptr, lv_obj_t* value = nullptr);
void hide_popup_shell(lv_obj_t* body);
void sync_popup_shell();
// True from show_popup_shell() until the popup is hidden again. Every tile
// popup and the PIN pad open through the shell.
bool popup_shell_active();
// Sets the active popup body's background; the frame copies it on the next
// sync. Used when an open popup follows its tile's color.
void popup_shell_follow_tile_color(uint32_t color);
// The circle options of the tile that opens the next popup (Icon circle
// Off/Global/On and "Circle in icon color"), so the header disc looks like
// the tile's disc. The next show_popup_shell() takes them; popups opened
// without a tile keep the default disc (tinted by a colored icon, shown).
void popup_shell_use_tile_disc(bool off, bool follows_global, bool glow);

// Register an existing background tree, once after construction. Opaque popup
// pixels can skip its covered draw calls without hiding or rebuilding widgets.
void register_popup_background(lv_obj_t* root);
