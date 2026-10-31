#pragma once

// Built-in camera board file of the Elecrow CrowPanel Advanced 10.1 (contract:
// src/video/local_camera/camera_driver.h), for the "With 2MP Camera" variant.
// Board facts from Elecrow's IDF lesson 13 (bsp_camera, sdkconfig):
//   - SC2336 on the MIPI CSI connector, SCCB on I2C port 1, SCL 13 / SDA 12,
//     its own bus (touch uses I2C0 on 45/46); no reset or power-down line
//   - 2 data lanes (Elecrow runs the 2-lane RAW8 1024x600 mode; HomeTiles the
//     2-lane RAW8 1280x720 mode of the same driver)
//   - the CSI/DSI PHY supply is LDO channel 3 at 2500 mV, shared with the
//     display by reference count
// Mirror and mounting turn still need a board owner's hardware check.

#include "src/video/local_camera/camera_select.h"

#if defined(DEVICE_ELECROW_CROWPANEL_10_1) && defined(HOMETILES_LOCAL_CAMERA)

#include "src/video/local_camera/camera_driver.h"
#include "src/video/local_camera/sensors/sc2336/sc2336_sensor.h"

namespace local_camera_board {

using SccbBus = sc2336::Transport*;
using Sensor = sc2336::Sensor;

inline constexpr local_camera::SensorMode kMode = {
    sc2336::kName,
    sc2336::kChipId,
    static_cast<uint8_t>(sc2336::kSccbAddress),
    static_cast<uint16_t>(sc2336::kFrameWidth),
    static_cast<uint16_t>(sc2336::kFrameHeight),
    static_cast<uint16_t>(sc2336::kFrameWidth),
    static_cast<uint16_t>(sc2336::kFrameHeight),
    static_cast<uint8_t>(sc2336::kDataLanes),
    static_cast<uint16_t>(sc2336::kLaneBitRateMbps),
    true,   // Mirrored like a front camera (hardware check pending).
    false,  // Mounting turn unknown until a board owner checks it.
    false,  // Landscape sensor: the frame is the image.
    COLOR_RAW_ELEMENT_ORDER_BGGR,
    false,  // The mode sends no MIPI line-sync packets (upstream line_sync_en false).
    16,     // Black level as the SC202CS of the same family (tuning pending).
    sc2336::kFrameMs,
    sc2336::kDefaultExposureLines,
    sc2336::kMinGainX16,
    sc2336::kMinExposureLines,
    sc2336::kMaxExposureLines,
    sc2336::kMinGainX16,
    sc2336::kMaxGainX16,
    sc2336::kMaxTotalGainX16,
    sc2336::kRawBits,
};

// Opens the camera's own SCCB bus, takes a shared reference on the MIPI PHY
// LDO and returns the transport.
local_camera::BoardError acquire(SccbBus* sccb_bus);
void release();

}  // namespace local_camera_board

#endif  // defined(DEVICE_ELECROW_CROWPANEL_10_1) && defined(HOMETILES_LOCAL_CAMERA)
