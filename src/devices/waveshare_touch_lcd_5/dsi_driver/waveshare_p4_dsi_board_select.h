#pragma once

// Maps the active device to its description for the shared Waveshare ESP32-P4
// MIPI-DSI driver. Only the driver and the board files include this header.
// A further board adds its define to DEVICE_WAVESHARE_P4_DSI_BOARD in
// device_select.h and one branch here.

#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_5)
#include "src/devices/waveshare_touch_lcd_5/board.h"
namespace waveshare_p4_dsi {
inline constexpr const Board& kActiveBoard = DeviceWaveshareTouchLCD5Board::kBoard;
}  // namespace waveshare_p4_dsi
#elif defined(DEVICE_WAVESHARE_P4_DSI_BOARD)
#error "DEVICE_WAVESHARE_P4_DSI_BOARD needs a board description"
#endif
