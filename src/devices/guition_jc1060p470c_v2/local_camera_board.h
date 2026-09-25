#pragma once

// Camera board file of the Guition JC1060P470C_I_W_Y V2 profile, camera
// beta builds only (contract: src/video/local_camera/camera_driver.h). Board
// facts come from Guition's official JC1060P470C_I_W_Y schematic V1.0 and
// video_lcd_display demo (see the OV02C10 PROVENANCE.md):
//   - optional OV02C10 module on the 15-pin 0.3 mm CSI connector FPC2; boards
//     without the module report the sensor as not detected
//   - SCCB on the board I2C bus (I2C0, SDA 7, SCL 8) shared with GT911 touch,
//     RTC and codec
//   - landscape panel: Guition's demo shows the frame without rotation
//   - no reset, power-down or XCLK pins (CSI_IO0 is pulled up, the module is
//     always powered); the module provides its 24 MHz clock
//   - MIPI CSI/DSI PHY supply is LDO channel 3 at 2500 mV, also used by the
//     display, so it is shared rather than reconfigured
//   - GBRG Bayer order; display rotation and the mirror setting are sensor
//     readout flips that keep GBRG
//   - sensor BLC target 0x40 of 1023 (table 0x4003), 16 in 8-bit units

#include "src/video/local_camera/camera_select.h"

#if defined(DEVICE_GUITION_JC1060P470C_V2) && defined(HOMETILES_LOCAL_CAMERA)

#include <driver/i2c_master.h>

#include "src/video/local_camera/camera_driver.h"
#include "src/video/local_camera/sensors/ov02c10/ov02c10_sensor.h"

namespace local_camera_board {

// The sensor is an ESP-IDF i2c_master device on the board bus.
using SccbBus = i2c_master_bus_handle_t;
using Sensor = ov02c10::Sensor;

inline constexpr local_camera::SensorMode kMode = {
    ov02c10::kName,
    ov02c10::kChipId,
    static_cast<uint8_t>(ov02c10::kSccbAddress),
    static_cast<uint16_t>(ov02c10::kFrameWidth),
    static_cast<uint16_t>(ov02c10::kFrameHeight),
    // The sensor window is the JPEG size (whole 16x8 MCUs), no crop.
    static_cast<uint16_t>(ov02c10::kFrameWidth),
    static_cast<uint16_t>(ov02c10::kFrameHeight),
    static_cast<uint8_t>(ov02c10::kDataLanes),
    static_cast<uint16_t>(ov02c10::kLaneBitRateMbps),
    true,   // V2 default; orientation pending hardware check.
    false,
    false,  // Landscape like the demo, no quarter turn.
    COLOR_RAW_ELEMENT_ORDER_GBRG,
    true,  // The table enables line-sync packets (0x4800 = 0x64).
    16,
    34,    // VTS 1164 at 30 fps.
    ov02c10::kTableExposureLines,
    static_cast<uint16_t>(ov02c10::kTableAnalogGainReg >> 4),
    ov02c10::kMinExposureLines,
    ov02c10::kMaxExposureLines,
    ov02c10::kMinGainX16,
    ov02c10::kMaxGainX16,
    ov02c10::kMaxTotalGainX16,
};

// Takes a shared reference on the MIPI PHY LDO and returns the existing board
// I2C bus. Never creates, resets or deletes an I2C bus.
local_camera::BoardError acquire(i2c_master_bus_handle_t* sccb_bus);
void release();

}  // namespace local_camera_board

#endif  // defined(DEVICE_GUITION_JC1060P470C_V2) && defined(HOMETILES_LOCAL_CAMERA)
