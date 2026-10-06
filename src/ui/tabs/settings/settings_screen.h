#pragma once

#include <lvgl.h>

// The Settings screen of the approved mockup
// (build/design-mockups/settings/settings-menu.html): bar, categories, the
// page card and its pages. Values and actions come from settings_model.h.
namespace settings_screen {

// Builds the frame on `panel` (bar, categories, the empty card). The panel
// carries the grid margins as its padding.
void build(lv_obj_t* panel);
// Before the panel shows: rebuilds the frame after a change of tile color,
// Circle strength or language, then builds the open page.
void prepare_show();
// After the panel hid: releases the page.
void did_hide();
// The category lines and icons (brightness, network, time, update).
void refresh_lines();
// The rotation segment follows a rotation changed elsewhere.
void sync_rotation();
// The texts changed: the frame is rebuilt now while it shows, else when it
// opens next.
void texts_changed();

}  // namespace settings_screen
