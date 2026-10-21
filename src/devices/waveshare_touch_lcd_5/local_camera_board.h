#pragma once

// Camera board file of the Waveshare ESP32-P4-WIFI6-Touch-LCD-5 profile,
// (contract: src/video/local_camera/camera_driver.h).
// Board facts come from Waveshare's LCD-5 repository (commit
// d144b34d8f384e84d2442e8733523560216a2f63: README.md "2-lane MIPI-CSI
// interface with optional OV5647 camera", docs/HARDWARE.md, the
// 09_video_lcd_display example and the 06_Camera_Preview sketch; see the
// OV5647 PROVENANCE.md):
//   - optional OV5647 module on the MIPI-CSI connector; without a module the
//     sensor is reported as not detected
//   - SCCB on the board I2C bus (SDA 7, SCL 8, CONFIG_EXAMPLE_MIPI_CSI_SCCB_*
//     and CAMERA_SCCB_*) that the GT911 touch driver already owns
//   - no reset, power-down or XCLK pins (reset_pin = -1, pwdn_pin = -1); the
//     module provides its 24 MHz clock
//   - 2 MIPI data lanes; the CSI/DSI PHY supply is LDO channel 3 at 2500 mV,
//     also used by the display, so it is shared rather than reconfigured
//   - GBRG Bayer order with the table's mirrored readout (the example sets
//     VFLIP and HFLIP); display rotation and the mirror setting are sensor
//     readout flips that keep GBRG
//   - black level: the OV5647 default BLC target 0x10 of 1023, 4 in 8-bit units

#include "src/video/local_camera/camera_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_5) && defined(HOMETILES_LOCAL_CAMERA)

#include <driver/i2c_master.h>

#include "src/video/local_camera/camera_driver.h"
#include "src/video/local_camera/sensors/ov5647/ov5647_sensor.h"

namespace local_camera_board {

// The sensor is an ESP-IDF i2c_master device on the board bus.
using SccbBus = i2c_master_bus_handle_t;
using Sensor = ov5647::Sensor;

inline constexpr local_camera::SensorMode kMode = {
    ov5647::kName,
    ov5647::kChipId,
    static_cast<uint8_t>(ov5647::kSccbAddress),
    static_cast<uint16_t>(ov5647::kFrameWidth),
    static_cast<uint16_t>(ov5647::kFrameHeight),
    // The sensor window is the JPEG size (720x960 portrait, whole 16x16 MCUs
    // for the Bridge's lossless quarter turn), no crop.
    static_cast<uint16_t>(ov5647::kFrameWidth),
    static_cast<uint16_t>(ov5647::kFrameHeight),
    static_cast<uint8_t>(ov5647::kDataLanes),
    static_cast<uint16_t>(ov5647::kLaneBitRateMbps),
    true,   // The table reads out mirrored, like the Waveshare example.
    false,
    // 8-inch default: mounted a quarter turn from the landscape image (720x960
    // portrait JPEG, turned by the Bridge); orientation pending hardware check.
    true,
    COLOR_RAW_ELEMENT_ORDER_GBRG,
    true,   // Stream-on enables line-sync packets (0x4800 = 0x14).
    4,
    ov5647::kFrameMs,
    ov5647::kDefaultExposureLines,
    ov5647::kMinGainX16,
    ov5647::kMinExposureLines,
    ov5647::kMaxExposureLines,
    ov5647::kMinGainX16,
    ov5647::kMaxGainX16,
    ov5647::kMaxTotalGainX16,
};

// Takes a shared reference on the MIPI PHY LDO and returns the existing board
// I2C bus. Never creates, resets or deletes an I2C bus.
local_camera::BoardError acquire(i2c_master_bus_handle_t* sccb_bus);
void release();

}  // namespace local_camera_board

#endif  // defined(DEVICE_WAVESHARE_TOUCH_LCD_5) && defined(HOMETILES_LOCAL_CAMERA)
