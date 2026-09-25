#pragma once

// Camera board file of the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B profiles
// (pre-v3 and v3.1 silicon), camera beta builds only (contract:
// src/video/local_camera/camera_driver.h). Board facts come from the 7B
// schematic and Waveshare's 7B camera examples (see the OV5647 PROVENANCE.md):
//   - OV5647 module of the 7B-C kit on the 15-pin 1.0 mm CSI connector J2;
//     the standard 7B has no module and reports the sensor as not detected
//   - SCCB on the board I2C bus (I2C0, SDA 7, SCL 8) that the GT911 touch
//     driver already owns
//   - no reset, power-down or XCLK pins (CSI_IO0 is pulled up, the module is
//     always powered); the module provides its 24 MHz clock
//   - 2 MIPI data lanes; the CSI/DSI PHY supply is LDO channel 3 at 2500 mV,
//     also used by the display, so it is shared rather than reconfigured
//   - GBRG Bayer order with the table's mirrored readout; display rotation
//     and the mirror setting are sensor readout flips that keep GBRG
//   - black level: the OV5647 default BLC target 0x10 of 1023, 4 in 8-bit units

#include "src/video/local_camera/camera_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) && defined(HOMETILES_LOCAL_CAMERA)

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
    // The sensor window is the JPEG size (544x960 portrait, whole 16x16 MCUs
    // for the Bridge's lossless quarter turn), no crop.
    static_cast<uint16_t>(ov5647::kFrameWidth),
    static_cast<uint16_t>(ov5647::kFrameHeight),
    static_cast<uint8_t>(ov5647::kDataLanes),
    static_cast<uint16_t>(ov5647::kLaneBitRateMbps),
    true,   // The table reads out mirrored, like the Waveshare example.
    false,
    // 8-inch default: mounted a quarter turn from the landscape image (544x960
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

#endif  // defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) && defined(HOMETILES_LOCAL_CAMERA)
