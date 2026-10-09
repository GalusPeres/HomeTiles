#pragma once

#include <Arduino.h>
#include "src/ui/shared/ui_theme.h"

using PinPopupVerifyCallback = bool (*)(const char* pin, void* context);
using PinPopupSuccessCallback = void (*)(void* context);
using PinPopupDismissedCallback = void (*)(void* context);

struct PinPopupInit {
  String title;
  String icon_name;
  uint32_t bg_color = ui_theme::popup_card();
  // Header icon color: a Folder's PIN popup takes the folder tile's icon color.
  uint32_t icon_color = 0xFFFFFF;
  bool hide_on_success = true;
  PinPopupVerifyCallback verify = nullptr;
  PinPopupSuccessCallback success = nullptr;
  void* context = nullptr;
  // A device code (Lock, Alarm panel) instead of a PIN: the prompt and error
  // texts ("Enter code", "Wrong code") and the header state ("Locked");
  // empty keeps the PIN texts.
  String prompt;
  String error;
  String state;
  // Opened from a popup: the close button is a back arrow and `dismissed`
  // runs when the user leaves (close, back or the inactivity timeout)
  // without a successful code, like Home Assistant's code dialog over its
  // more-info dialog.
  bool back = false;
  PinPopupDismissedCallback dismissed = nullptr;
  // A PIN-protected Folder or Settings: the header icon carries the lock of
  // its tile (popup_shell_icon_lock).
  bool lock_mark = false;
};

void show_pin_popup(const PinPopupInit& init);
void preload_pin_popup();
void hide_pin_popup();
void resume_pin_popup_after_failed_success();
bool is_pin_popup_visible();
// The error line's text, e.g. a code the Bridge rejected or the wait time
// after too many wrong codes; `show` shows it (red) until the next key,
// false returns to the prompt.
void pin_popup_set_error(const char* text, bool show);
// True while the popup shows the code entry opened with this `context`.
bool is_pin_popup_for(const void* context);
// The device behind a code entry changed while it is open: the header icon,
// its color and the state follow, and the keys take the card's current
// color (the card follows the opening tile, tile_icon_source), so the popup
// keeps its three tones.
void pin_popup_set_state(const String& icon_name, uint32_t icon_color, const String& state);
// The header icon pulses while a sent code waits for its answer (Home
// Assistant's running-command pulse); a failed answer stops it.
void pin_popup_pulse_icon(bool on);
