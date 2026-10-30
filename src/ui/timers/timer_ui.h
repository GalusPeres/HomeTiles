#pragma once

// Voice timers on screen (voice_timers.h holds the timers):
//   - the head bar's pill before the time: the timer icon, the next timer's
//     countdown and "+N" for more; a tap opens the Timers popup. Only with
//     the head bar layout and while a timer runs or is paused. A tap on the
//     head's time opens the popup too, so presets work without a timer.
//   - the Timers popup (popup shell): every timer with pause/resume, +1 min
//     and cancel, and presets that start a timer through Home Assistant.
//   - the alert card when a timer finishes: a swinging bell, the timer's
//     name, Stop and +1 min. It wakes the display, closes the screensaver,
//     rings the chosen sound (twice, then Home Assistant says which timer
//     finished, then again every few seconds for at most two minutes).
// Loop task only.

#include <lvgl.h>
#include <stddef.h>

#include "src/audio/voice_timer_contract.h"

namespace timer_ui {

// Awake loop: alerts, ringing, the countdowns, the heads when a pill comes,
// goes or changes its shape.
void service();
// Sleep loop: true when a finished timer woke the display.
bool serviceSleep();

// home_bar.cpp: the pill ending at `right` on the head's centre line, as
// high as the head's circle, over a head on `base`. Returns its left edge,
// or `right` when no pill shows.
int add_head_pill(lv_obj_t* bar, int right, int center_y, int height, uint32_t base);
// The head's time opens the popup (speaker boards only).
bool head_time_opens_popup();
void open_popup();
// A timer's name to show: its own, else its length ("15 min timer").
void timer_label(const voice_timer::Timer& timer, char* out, size_t size);
void hide_popup();
bool popup_visible();
// Theme change: the alert card was built in the old colors; it is built
// again (and shown again while a timer rings).
void theme_changed();

}  // namespace timer_ui
