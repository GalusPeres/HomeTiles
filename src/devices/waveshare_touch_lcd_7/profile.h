#pragma once

#include "src/devices/device_select.h"
#include "src/devices/device_types.h"
#include "src/devices/waveshare_touch_lcd_7/hardware_io_profile.h"

namespace DeviceWaveshareTouchLCD7Profile {

// Built-in OV5647 front camera: enabled only in camera beta builds
// (HOMETILES_LOCAL_CAMERA in device_select.h) until hardware validation.
#if defined(HOMETILES_LOCAL_CAMERA)
inline constexpr bool kBuiltinCamera = true;
#else
inline constexpr bool kBuiltinCamera = false;
#endif

inline constexpr Device::Profile kProfile{
    "waveshare_touch_lcd_7",
    "Waveshare Touch LCD 7",
    1280,
    720,
    7,
    4,
    16,
    4,
    168,
    166,
    4,
    121,
    Device::RotationStepMode::FlipOnly,
    0,
    2,
    Device::Capabilities{false, false, false, false, true, false, kBuiltinCamera},
    kHardwareIoProfile,
};

}  // namespace DeviceWaveshareTouchLCD7Profile
