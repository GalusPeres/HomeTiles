#ifdef __cplusplus
extern "C" {
#endif

#include "src/devices/elecrow_crowpanel_10_1/vendor/panel_init_cmds.h"

#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>

static const char* TAG = "PanelInit";

esp_err_t elecrow_1024x600_init(esp_lcd_panel_handle_t* panel) {
    ESP_LOGI(TAG, "Panel init commands for 1024x600 (MIPI-DSI native)");
    
    if (!panel) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Panel initialization complete");
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
