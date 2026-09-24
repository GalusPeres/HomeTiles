#pragma once

// Built-in camera board file of the exact Guition JC8012P4A1 V2 profile
// (contract: src/video/local_camera/camera_driver.h). Board facts come from
// Guition's video_lcd_display demo configuration (see the OV02C10 PROVENANCE.md):
//   - OV02C10 on the SCCB/I2C_NUM_0 bus shared with the GSL3680 touch
//     controller (SDA 7, SCL 8)
//   - no reset, power-down or XCLK pins; the module provides its 24 MHz clock
//   - MIPI CSI/DSI PHY supply is LDO channel 3 at 2500 mV, also used by the
//     display, so it is shared rather than reconfigured
//   - mirror on and GBRG Bayer order as in the demo; display rotation and
//     the mirror setting are sensor readout flips that keep GBRG
//   - no extra mounting rotation: the image is upright in the default
//     landscape orientation and turns with the display rotation setting
//   - sensor BLC target 0x40 of 1023 (table 0x4003), 16 in 8-bit units

#include "src/video/local_camera/camera_select.h"

#if defined(DEVICE_GUITION_JC8012P4A1_V2) && defined(HOMETILES_LOCAL_CAMERA)

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
    true,
    false,
    false,  // Landscape sensor: the frame is the image, no PPA pass.
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

// Takes a shared reference on the MIPI PHY LDO and returns the existing touch
// bus. Never creates, resets or deletes an I2C bus.
local_camera::BoardError acquire(i2c_master_bus_handle_t* sccb_bus);
void release();

}  // namespace local_camera_board

#endif  // defined(DEVICE_GUITION_JC8012P4A1_V2) && defined(HOMETILES_LOCAL_CAMERA)
