#pragma once

#include <lvgl.h>

#include "src/ui/tabs/settings/settings_model.h"

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
// Opens a category's page (0 Display, 1 WiFi, 2 Localization, 3 System).
void show_category(uint8_t index);
// The update, pairing or password state changed: the System page and the
// setup follow now (they also poll them while they show).
void system_changed();
// The first-start setup started, ended or changed its step: Settings shows
// it (or the frame again) now while Settings shows, else when it opens.
void setup_changed();
// Closes an open option list, dialog or network entry (navigation closes
// every popup).
void close_overlays();

// Shared with the setup: a Localization row's name, icon and value, and the
// rotation segment (quarter turns where the panel supports them).
const char* locale_title(settings_model::LocaleList list);
const char* locale_icon(settings_model::LocaleList list);
const char* locale_value(settings_model::LocaleList list);
lv_obj_t* rotation_segment(lv_obj_t* row, uint32_t track);
// The width of Pair and Allow: the longest of the two in every language.
int switch_on_width();
// The Networks heading with Search (Searching... while a scan runs); `first`
// drops the space above it.
lv_obj_t* networks_heading(lv_obj_t* parent, bool scanning, bool first, lv_event_cb_t on_search, void* user_data);

}  // namespace settings_screen
