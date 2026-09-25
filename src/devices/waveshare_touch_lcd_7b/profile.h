#pragma once

#include <stdint.h>

#include "src/devices/device_select.h"
#include "src/devices/device_types.h"
#include "src/devices/waveshare_touch_lcd_7b/hardware_io_profile.h"

namespace DeviceWaveshareTouchLCD7BProfile {

inline constexpr uint32_t kFlashSizeBytes = 32U * 1024U * 1024U;
inline constexpr uint32_t kPsramSizeBytes = 32U * 1024U * 1024U;

// OV5647 on the CSI connector (7B-C kit): enabled only in camera beta builds
// (HOMETILES_LOCAL_CAMERA in device_select.h) until hardware validation.
#if defined(HOMETILES_LOCAL_CAMERA)
inline constexpr bool kBuiltinCamera = true;
#else
inline constexpr bool kBuiltinCamera = false;
#endif

inline constexpr Device::Profile kProfile{
    "waveshare_touch_lcd_7b",
    "Waveshare Touch LCD 7B / 7B-C",
    1024,
    600,
    6,
    4,
    16,
    4,
    156,
    136,
    5,
    1,
    Device::RotationStepMode::FlipOnly,
    2,
    0,
    Device::Capabilities{false, false, false, false, true, false, kBuiltinCamera},
    kHardwareIoProfile,
};

}  // namespace DeviceWaveshareTouchLCD7BProfile
