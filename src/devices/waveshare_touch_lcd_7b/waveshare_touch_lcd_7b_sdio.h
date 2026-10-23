#pragma once

#include "src/devices/device_select.h"
#include <sdkconfig.h>

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) && CONFIG_ESP_REV_MIN_FULL >= 300
// After a restart that reset only the CPUs (software, panic, watchdog), one
// full system reset, so the SDIO host starts clean (issue #41). Called first
// in the board's init; returns at once after any other start.
void waveshare_7b_reset_system_after_cpu_restart();
#endif
