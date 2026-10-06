#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "src/ui/tabs/settings/settings_model.h"

// The WiFi entry (mockup sheet()): joining a found network or adding one by
// name, with round fields, a line for "Connecting..." or an error, and the
// keyboard, whose check connects (no extra Connect button). It fills a card
// over the card's other content: the Settings card (a plain title, the
// categories stay) or the setup's popup card (the step head and the popups'
// X).
namespace settings_entry {

struct Spec {
  lv_obj_t* card;
  // The global tile color (settings_style::colors).
  uint32_t card_color;
  // nullptr: the Settings card's plain title; else the setup's step head
  // with this line under the title ("Step 2 of 4").
  const char* step_line;
  // The X closed the entry (never while it connects); runs inside the touch.
  void (*closed)();
};

void open(const Spec& spec, bool manual, const char* ssid);
// Inside a touch on the entry it goes after the event.
void close(bool from_event = false);
bool is_open();
// Follows the connection attempt: a failure shows on the entry; true once
// the network came up (the entry closed itself then).
bool tick(const settings_model::WifiValues& v);

}  // namespace settings_entry
