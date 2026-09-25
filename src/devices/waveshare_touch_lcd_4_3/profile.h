#pragma once

#include "src/devices/device_select.h"
#include "src/devices/device_types.h"

namespace DeviceWaveshareTouchLCD4_3Profile {

// OV5647 on the CSI connector (-C kit): enabled only in camera beta builds
// (HOMETILES_LOCAL_CAMERA in device_select.h) until hardware validation.
#if defined(HOMETILES_LOCAL_CAMERA)
inline constexpr bool kBuiltinCamera = true;
#else
inline constexpr bool kBuiltinCamera = false;
#endif

inline constexpr Device::Profile kProfile{
    "waveshare_touch_lcd_4_3",
    "Waveshare Touch LCD 4.3",
    800,
    480,
    5,
    4,
    10,
    3,
    150,
    111,
    4,
    1,
    Device::RotationStepMode::FlipOnly,
    0,
    2,
    Device::Capabilities{false, false, false, false, true, false, kBuiltinCamera},
    Device::kNoHardwareIoProfile,
};

}  // namespace DeviceWaveshareTouchLCD4_3Profile
