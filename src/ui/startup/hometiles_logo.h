#ifndef HOMETILES_LOGO_H
#define HOMETILES_LOGO_H

#include <lvgl.h>

// Compiled-in bitmap of docs/images/logo.svg (144x144, ARGB8888) -- an exact
// pixel reproduction of the real logo, not a hand-drawn LVGL-primitive
// approximation. See hometiles_logo.cpp for how it was generated.
extern const lv_image_dsc_t hometiles_logo_dsc;

// The logo for the UI theme (ui_theme.h): the light theme's copy draws the
// white tiles in the dark text color (made once on first use), the colored
// plus stays. The boot splash keeps the original on black.
const lv_image_dsc_t* hometiles_logo_for_theme();

#endif  // HOMETILES_LOGO_H
