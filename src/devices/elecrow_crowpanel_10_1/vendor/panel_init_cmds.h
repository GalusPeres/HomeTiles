#pragma once

#include <stddef.h>

#include "src/devices/device_select.h"
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
#include "src/devices/elecrow_crowpanel_10_1/vendor/jd9365/esp_lcd_jd9365_10_1.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
extern const jd9365_lcd_init_cmd_t kWaveshareJd9365Init10_1[];
extern const size_t kWaveshareJd9365Init10_1Count;
#endif

#ifdef __cplusplus
}
#endif
