#pragma once

#include <lvgl.h>

// The first-start setup of the approved mockup
// (build/design-mockups/settings/settings-menu.html, setup A): four steps in
// the popup card on the Settings panel - language and display, WiFi, Home
// Assistant, tiles. Values and actions come from settings_model.h; the step
// is kept there, so a restart resumes it.
namespace setup_screen {

// Builds the card for the stored step on `panel` (the Settings panel, which
// carries the grid margins); again after a new step, language or tile color.
void show(lv_obj_t* panel);
void hide();
bool shown();
// The network or pairing state changed (it also polls them while it shows).
void refresh();
// The rotation segment follows a rotation changed elsewhere.
void sync_rotation();
// Closes an open option list, dialog or the WiFi entry.
void close_overlays();

}  // namespace setup_screen
