#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "src/ui/tabs/settings/settings_style.h"

// The keyboard of the WiFi entry (mockup keyboard()): four rows of rounded
// keys on a ten-key grid. Letters in the panel's layout (QWERTY, QWERTZ,
// AZERTY), Shift, Backspace, ?123, the accent key, space, the dot and the
// check that connects. Nothing the former keyboard could type is lost: the
// accent key shows the language's accented letters in the letter rows (as
// the former keyboard's language key did, user 2026-10-06), holding a letter
// offers its accented forms too, and Shift on the ?123 page opens a second
// page with the remaining symbols.
namespace settings_keyboard {

enum class Layout : uint8_t { Qwerty, Qwertz, Azerty };
// The accent page: German and other European letters, French, Polish.
enum class Accents : uint8_t { European, French, Polish };

struct Geometry {
  int x;        // left edge of the rows
  int y;        // top of the first row
  int width;    // the rows' width
  int key_h;    // key height
  int step;     // from one row's top to the next
  int gap;      // between keys
  int radius;   // key rounding at the maximum tile radius
};

struct Handler {
  void (*text)(const char* utf8);
  void (*backspace)();
  void (*ok)();
};

lv_obj_t* create(lv_obj_t* parent, const Geometry& geometry, Layout layout, Accents accents,
                 const settings_style::Colors& colors, uint32_t accent, const Handler& handler);
// Keys ignore touches while a connection is being made.
void set_enabled(lv_obj_t* keyboard, bool enabled);

}  // namespace settings_keyboard
