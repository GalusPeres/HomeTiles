#pragma once

#include <stdint.h>

#include "src/devices/device_select.h"
#include "src/devices/device_types.h"
#include "src/devices/waveshare_touch_lcd_5/hardware_io_profile.h"

namespace DeviceWaveshareTouchLCD5Profile {

// GD25Q256 NOR flash and the ESP32-P4NRW32 in-package PSRAM (Waveshare
// docs/HARDWARE.md, checked against the board schematic).
inline constexpr uint32_t kFlashSizeBytes = 32U * 1024U * 1024U;
inline constexpr uint32_t kPsramSizeBytes = 32U * 1024U * 1024U;

// Optional OV5647 module on the MIPI-CSI connector (HOMETILES_LOCAL_CAMERA
// in device_select.h); off until the user enables it in the Web Admin.
#if defined(HOMETILES_LOCAL_CAMERA)
inline constexpr bool kBuiltinCamera = true;
#else
inline constexpr bool kBuiltinCamera = false;
#endif

// The 720 x 1280 portrait panel shows HomeTiles like the M5Stack Tab5 and
// the Waveshare 7-inch (the same panel size): a 1280 x 720 landscape
// dashboard with the 7 x 4 grid, turned a quarter into the panel, and the
// upright layout drawn straight. The backlight is a direct LEDC PWM like
// Waveshare's BSP (raw 1..255 = 1..100 %).
inline constexpr Device::Profile kProfile{
    "waveshare_touch_lcd_5",
    "Waveshare Touch LCD 5",
    1280,
    720,
    7,
    4,
    16,
    4,
    168,
    166,
    4,
    1,
    Device::RotationStepMode::FlipOnly,
    0,
    2,
    Device::Capabilities{false, false, false, false, true, false, kBuiltinCamera},
    kHardwareIoProfile,
};

}  // namespace DeviceWaveshareTouchLCD5Profile
