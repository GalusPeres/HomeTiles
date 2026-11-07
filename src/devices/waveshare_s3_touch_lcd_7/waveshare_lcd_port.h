#ifndef __LCD_PORT_H
#define __LCD_PORT_H

#pragma once

#include <Arduino.h>

// I2C Pin define
#define I2C_MASTER_NUM I2C_NUM_0 // I2C master number
#define I2C_MASTER_SDA_IO 8       // I2C data line
#define I2C_MASTER_SCL_IO 9       // I2C clock line

// Extend IO Pin define
#define TP_RST 1
#define LCD_BL 2
#define LCD_RST 3
#define SD_CS 4
#define USB_SEL 5     // USB select pin

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Currently, the library supports the following RGB (without 3-wire SPI) LCDs:
 *      - ST7262
 */
#define EXAMPLE_LCD_NAME                        ST7262 // LCD model name
#define EXAMPLE_LCD_WIDTH                       (800) // LCD width in pixels
#define EXAMPLE_LCD_HEIGHT                      (480) // LCD height in pixels
#define EXAMPLE_LCD_COLOR_BITS                  (24)  // Color depth in bits
#define EXAMPLE_LCD_RGB_DATA_WIDTH              (16)  // Width of RGB data
#define EXAMPLE_LCD_RGB_COLOR_BITS          (16)    // |      24      |      16       |

// Exact values from the vendor board configuration and the working ESP-IDF
// LVGL demo for this panel (NuttX and ESPHome ports of the same board agree):
// 16 MHz PCLK, 4/8/8 porches. The panel latches data on the rising PCLK edge,
// so the peripheral must launch it on the falling edge
// (`pclk_active_neg = true`, applied in device_waveshare_s3_touch_lcd_7.cpp).
// The library's default flags launch on the rising edge, which leaves the
// panel no data setup time.
#define EXAMPLE_LCD_RGB_TIMING_FREQ_HZ          (16 * 1000 * 1000) // RGB timing frequency
#define EXAMPLE_LCD_RGB_TIMING_HPW              (4)   // Horizontal pulse width
#define EXAMPLE_LCD_RGB_TIMING_HBP              (8)   // Horizontal back porch
#define EXAMPLE_LCD_RGB_TIMING_HFP              (8)   // Horizontal front porch
#define EXAMPLE_LCD_RGB_TIMING_VPW              (4)   // Vertical pulse width
#define EXAMPLE_LCD_RGB_TIMING_VBP              (8)   // Vertical back porch
#define EXAMPLE_LCD_RGB_TIMING_VFP              (8)   // Vertical front porch
// 20 lines, as in the vendor's ESP-IDF LVGL demo for this panel
// (width * 10 * 16 / 8 passed to esp_lcd as a pixel count). Espressif's RGB
// application note asks for at least 20 lines: the bounce buffers are
// refilled from PSRAM in the GDMA EOF interrupt, and a larger buffer absorbs
// Wi-Fi/LVGL PSRAM stalls before the LCD FIFO underruns. Keep 20 lines: a
// 40-line buffer was tried earlier and did not initialize on this board.
#define EXAMPLE_LCD_RGB_BOUNCE_BUFFER_SIZE  (EXAMPLE_LCD_WIDTH * 20)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your board spec ////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_RGB_IO_DISP            (-1)  // RGB display pin number
#define EXAMPLE_LCD_RGB_IO_VSYNC           (3)   // VSYNC pin number
#define EXAMPLE_LCD_RGB_IO_HSYNC           (46)  // HSYNC pin number
#define EXAMPLE_LCD_RGB_IO_DE              (5)   // Data enable pin number
#define EXAMPLE_LCD_RGB_IO_PCLK            (7)   // Pixel clock pin number
#define EXAMPLE_LCD_RGB_IO_DATA0           (14)  // RGB data pin 0
#define EXAMPLE_LCD_RGB_IO_DATA1           (38)  // RGB data pin 1
#define EXAMPLE_LCD_RGB_IO_DATA2           (18)  // RGB data pin 2
#define EXAMPLE_LCD_RGB_IO_DATA3           (17)  // RGB data pin 3
#define EXAMPLE_LCD_RGB_IO_DATA4           (10)  // RGB data pin 4
#define EXAMPLE_LCD_RGB_IO_DATA5           (39)  // RGB data pin 5
#define EXAMPLE_LCD_RGB_IO_DATA6           (0)   // RGB data pin 6
#define EXAMPLE_LCD_RGB_IO_DATA7           (45)  // RGB data pin 7
#if EXAMPLE_LCD_RGB_DATA_WIDTH > 8
#define EXAMPLE_LCD_RGB_IO_DATA8           (48)  // RGB data pin 8
#define EXAMPLE_LCD_RGB_IO_DATA9           (47)  // RGB data pin 9
#define EXAMPLE_LCD_RGB_IO_DATA10          (21)  // RGB data pin 10
#define EXAMPLE_LCD_RGB_IO_DATA11          (1)   // RGB data pin 11
#define EXAMPLE_LCD_RGB_IO_DATA12          (2)   // RGB data pin 12
#define EXAMPLE_LCD_RGB_IO_DATA13          (42)  // RGB data pin 13
#define EXAMPLE_LCD_RGB_IO_DATA14          (41)  // RGB data pin 14
#define EXAMPLE_LCD_RGB_IO_DATA15          (40)  // RGB data pin 15
#endif
#define EXAMPLE_LCD_RST_IO                 (-1)  // Reset pin number
#define EXAMPLE_LCD_BL_IO            (-1)  // Backlight pin number
#define EXAMPLE_LCD_BL_ON_LEVEL           (1)   // Backlight ON level
#define EXAMPLE_LCD_BL_OFF_LEVEL !EXAMPLE_LCD_BL_ON_LEVEL // Backlight OFF level

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////// Please update the following configuration according to your test ///////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif // End of IO_PORT_H
