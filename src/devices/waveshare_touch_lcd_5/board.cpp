#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_5)

#include "src/devices/waveshare_touch_lcd_5/board.h"
#include "src/devices/waveshare_touch_lcd_5/vendor/hx8394/esp_lcd_hx8394.h"

namespace DeviceWaveshareTouchLCD5Board {

// Like the BSP's bsp_display_new_with_handles(): the HX8394 with its own
// default init table (vendor/hx8394), the 2-lane DSI link and the DPI
// configuration the shared driver built from kBoard.
esp_err_t createHx8394Panel(esp_lcd_panel_io_handle_t io,
                            esp_lcd_panel_dev_config_t panel_config,
                            esp_lcd_dsi_bus_handle_t dsi_bus,
                            const esp_lcd_dpi_panel_config_t* dpi_config,
                            uint8_t lane_count,
                            esp_lcd_panel_handle_t* panel) {
  hx8394_vendor_config_t vendor_config = {};
  vendor_config.init_cmds = nullptr;
  vendor_config.init_cmds_size = 0;
  vendor_config.mipi_config.dsi_bus = dsi_bus;
  vendor_config.mipi_config.dpi_config = dpi_config;
  vendor_config.mipi_config.lane_num = lane_count;
  panel_config.vendor_config = &vendor_config;
  return esp_lcd_new_panel_hx8394(io, &panel_config, panel);
}

}  // namespace DeviceWaveshareTouchLCD5Board

#endif  // defined(DEVICE_WAVESHARE_TOUCH_LCD_5)
