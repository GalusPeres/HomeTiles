#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <esp_lcd_panel_vendor.h>

esp_err_t elecrow_1024x600_init(esp_lcd_panel_handle_t* panel);

#ifdef __cplusplus
}
#endif
