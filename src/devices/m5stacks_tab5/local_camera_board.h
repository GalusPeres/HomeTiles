#pragma once

// Built-in camera board file of the M5Stack Tab5 profile, camera beta builds
// only (contract: src/video/local_camera/camera_driver.h). Board facts from
// M5Stack's Tab5 schematic, espressif/esp-bsp m5stack_tab5 and the
// M5Tab5-UserDemo (see the SC202CS PROVENANCE.md):
//   - SC2356 = esp_cam_sensor SC202CS, SCCB 0x36 on the system I2C bus
//     (I2C0, SDA 31, SCL 32) shared with touch, IO expanders, audio and RTC.
//     M5Unified owns that bus (m5gfx::i2c, per-port mutex); the sensor goes
//     through M5.In_I2C, never through a second I2C driver on the port.
//   - CAM_RST is pin P6 of the PI4IOE5V6408 at 0x43 (M5.getIOExpander(0)),
//     released high before the first access; no power-down line
//   - 24 MHz clock from an oscillator on the board (GPIO36 path not fitted)
//   - 1 MIPI data lane; the CSI/DSI PHY supply is LDO channel 3 at 2500 mV,
//     also used by the display (M5GFX), so it is shared rather than reconfigured
//   - RAW8 1280x720 BGGR, the demo's default mode, BGGR in every orientation
//   - in the HomeTiles landscape UI the image is upside down with the demo's
//     mirror alone (hardware 2026-09-25): the board adds a 180 degree turn

#include "src/video/local_camera/camera_select.h"

#if defined(DEVICE_M5STACKS_TAB5) && defined(HOMETILES_LOCAL_CAMERA)

#include "src/video/local_camera/camera_driver.h"
#include "src/video/local_camera/sensors/sc202cs/sc202cs_sensor.h"

namespace local_camera_board {

// The sensor talks through the board's M5Unified transport.
using SccbBus = sc202cs::Transport*;
using Sensor = sc202cs::Sensor;

inline constexpr local_camera::SensorMode kMode = {
    sc202cs::kName,
    sc202cs::kChipId,
    static_cast<uint8_t>(sc202cs::kSccbAddress),
    static_cast<uint16_t>(sc202cs::kFrameWidth),
    static_cast<uint16_t>(sc202cs::kFrameHeight),
    // The sensor window is the JPEG size (whole 16x8 MCUs), no crop.
    static_cast<uint16_t>(sc202cs::kFrameWidth),
    static_cast<uint16_t>(sc202cs::kFrameHeight),
    static_cast<uint8_t>(sc202cs::kDataLanes),
    static_cast<uint16_t>(sc202cs::kLaneBitRateMbps),
    true,   // Mirrored like the M5Stack demo shows its front camera.
    true,   // Upside down in the landscape UI (hardware 2026-09-25).
    false,  // Landscape sensor: the frame is the image.
    COLOR_RAW_ELEMENT_ORDER_BGGR,
    false,  // The mode sends no MIPI line-sync packets.
    16,     // esp_cam_sensor SC202CS tuning: black level 16 in RAW8.
    sc202cs::kFrameMs,
    sc202cs::kDefaultExposureLines,
    sc202cs::kMinGainX16,
    sc202cs::kMinExposureLines,
    sc202cs::kMaxExposureLines,
    sc202cs::kMinGainX16,
    sc202cs::kMaxGainX16,
    sc202cs::kMaxTotalGainX16,
    sc202cs::kRawBits,
};

// Releases the camera reset, takes a shared reference on the MIPI PHY LDO
// and returns the M5Unified SCCB transport. Never touches the I2C port setup.
local_camera::BoardError acquire(SccbBus* sccb_bus);
void release();

}  // namespace local_camera_board

#endif  // defined(DEVICE_M5STACKS_TAB5) && defined(HOMETILES_LOCAL_CAMERA)
