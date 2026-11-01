#pragma once

// Turned on, it logs every visible label whose text does not fit: cut,
// ellipsized or scrolled in its box, taller than a fixed box, or wider than
// a parent that clips it. Used to check translations on the device (user
// 2026-10-03: French and Polish must fit every screen). Release builds
// compile it out.
//
// A translation check turns it on here: every 2 s it measures every visible
// label, and the test builds are judged by feel against the releases (user
// 2026-10-28: "die alte Version war ueberall schneller").
// #define HOMETILES_TEXT_FIT_PROBE 1

namespace text_fit_probe {

// Starts the periodic check on an LVGL timer; call from the LVGL context.
void start();

}  // namespace text_fit_probe
