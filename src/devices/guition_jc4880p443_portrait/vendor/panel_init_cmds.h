#pragma once

#include <stddef.h>

#include "src/devices/device_select.h"

#if defined(DEVICE_GUITION_JC4880P443_PORTRAIT)
#include "src/devices/guition_jc4880p443_portrait/vendor/st7701/esp_lcd_st7701.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const st7701_lcd_init_cmd_t kGuitionSt7701Init4880P443[];
extern const size_t kGuitionSt7701Init4880P443Count;

#ifdef __cplusplus
}
#endif
#endif  // defined(DEVICE_GUITION_JC4880P443_PORTRAIT)
