#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "src/ui/tabs/settings/settings_style.h"

// The keyboard of the WiFi entry (mockup keyboard()): four rows of rounded
// keys on a ten-key grid. Letters in the panel's layout (QWERTY, QWERTZ,
// AZERTY), Shift, Backspace, ?123, the accent key, space, the dot and the
// check that connects. The Russian JCUKEN layout has eleven letters in its
// first two rows and nine beside Shift, on an eleven-key grid like the
// common Russian phone keyboards; its accent key switches to QWERTY and
// back (ABC / АБВ), since Wi-Fi passwords and addresses are Latin. Nothing the former keyboard could type is lost: the
// accent key shows the language's accented letters in the letter rows (as
// the former keyboard's language key did, user 2026-10-06), holding a letter
// offers its accented forms too, and Shift on the ?123 page opens a second
// page with the remaining symbols. The letters come from the language's
// LocaleProfile (keyboard_letters.h), so a language needs no code here.
namespace settings_keyboard {

// The values of LocaleProfile::keyboard_layout.
enum class Layout : uint8_t { Qwerty, Qwertz, Azerty, Jcuken };

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

// `letters`: the language's own letters (LocaleProfile::keyboard_letters),
// first on the accent page and among a held letter's forms.
lv_obj_t* create(lv_obj_t* parent, const Geometry& geometry, Layout layout, const char* letters,
                 const settings_style::Colors& colors, uint32_t accent, const Handler& handler);
// Keys ignore touches while a connection is being made.
void set_enabled(lv_obj_t* keyboard, bool enabled);

}  // namespace settings_keyboard
