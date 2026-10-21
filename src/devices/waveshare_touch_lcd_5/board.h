#pragma once

// The Waveshare ESP32-P4-WIFI6-Touch-LCD-5 for its own MIPI-DSI driver
// (src/devices/waveshare_touch_lcd_5/dsi_driver). Sources, all from
// Waveshare: the board BSP waveshare/esp32_p4_wifi6_touch_lcd_5 1.0.4
// (waveshareteam/Waveshare-ESP32-components, bsp/esp32_p4_wifi6_touch_lcd_5,
// commit 23a6012d5e87d27399cf90fb851cab3689e29159), its HX8394 driver
// waveshare/esp_lcd_hx8394 2.1.0 (HX8394_720_1280_PANEL_30HZ_DPI_CONFIG) and
// the board repository waveshareteam/ESP32-P4-WIFI6-Touch-LCD-5 (commit
// d144b34d8f384e84d2442e8733523560216a2f63: docs/IO.md, docs/HARDWARE.md
// and examples/arduino/libraries/displays/displays_config.h, which agree).

#include <sd_protocol_types.h>

#include "src/devices/waveshare_touch_lcd_5/dsi_driver/waveshare_p4_dsi_board.h"
#include "src/devices/waveshare_touch_lcd_5/profile.h"

namespace DeviceWaveshareTouchLCD5Board {

// The HX8394 on the shared DSI bus (board.cpp).
esp_err_t createHx8394Panel(esp_lcd_panel_io_handle_t io,
                            esp_lcd_panel_dev_config_t panel_config,
                            esp_lcd_dsi_bus_handle_t dsi_bus,
                            const esp_lcd_dpi_panel_config_t* dpi_config,
                            uint8_t lane_count,
                            esp_lcd_panel_handle_t* panel);

inline constexpr waveshare_p4_dsi::Board kBoard{
    "WaveshareTouchLCD5",
    DeviceWaveshareTouchLCD5Profile::kProfile,
    {
        "HX8394",
        createHx8394Panel,
        // BSP_LCD_H_RES / BSP_LCD_V_RES: 720 x 1280 portrait.
        720,
        1280,
        // BSP_LCD_MIPI_DSI_LANE_NUM 2, BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS 700.
        2,
        700,
        // HX8394_720_1280_PANEL_30HZ_DPI_CONFIG: 58 MHz DPI clock, hsync
        // pulse/back/front 20/20/40, vsync 4/10/24.
        58000000,
        20,
        20,
        40,
        4,
        10,
        24,
        // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN 3 (LDO_VO3 = VDD_MIPI_DPHY), 2500 mV.
        3,
        2500,
        // BSP_LCD_RST GPIO27; the BSP sets flags.reset_active_high = 1 (the
        // HX8394 driver asserts the reset with a high level).
        GPIO_NUM_27,
        true,
    },
    // BSP_LCD_BACKLIGHT GPIO26: LEDC 5 kHz, 10-bit, duty rises with the
    // brightness (bsp_display_brightness_init/_set). The backlight circuit
    // is AC-coupled: a static level does not light it (displays_config.h).
    {GPIO_NUM_26, 5000, 10, false},
    // BSP_I2C_SDA GPIO7, BSP_I2C_SCL GPIO8 (GT911 touch, ES8311, ES7210).
    GPIO_NUM_7,
    GPIO_NUM_8,
    // bsp_sdcard_mount(): SDMMC slot 0 4-bit, card power from on-chip LDO
    // channel 4. The BSP mounts at SDMMC_FREQ_HIGHSPEED (40 MHz); HomeTiles
    // keeps 20 MHz like the hardware-tested Waveshare 8-inch, whose LCD-X BSP
    // has the same slot, pins, LDO and 40 MHz.
    4,
    SDMMC_FREQ_DEFAULT,
};

}  // namespace DeviceWaveshareTouchLCD5Board
