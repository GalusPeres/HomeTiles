#pragma once

// Beta builds log every visible label whose text does not fit: cut,
// ellipsized or scrolled in its box, taller than a fixed box, or wider than
// a parent that clips it. Used to check translations on the device (user
// 2026-10-03: French and Polish must fit every screen). Release builds
// compile it out.
#if defined(HOMETILES_TEST_BETA) || defined(HOMETILES_ISSUE38_BETA) || \
    defined(HOMETILES_CAMERA_BETA)
#define HOMETILES_TEXT_FIT_PROBE 1
#endif

namespace text_fit_probe {

// Starts the periodic check on an LVGL timer; call from the LVGL context.
void start();

}  // namespace text_fit_probe
