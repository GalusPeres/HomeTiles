#pragma once

#include "src/devices/device_select.h"
#include "src/devices/device_types.h"
#include "src/devices/waveshare_touch_lcd_10_1/hardware_io_profile.h"

namespace DeviceWaveshareTouchLCD10Profile {

// Built-in OV5647 front camera: enabled only in camera beta builds
// (HOMETILES_LOCAL_CAMERA in device_select.h) until hardware validation.
#if defined(HOMETILES_LOCAL_CAMERA)
inline constexpr bool kBuiltinCamera = true;
#else
inline constexpr bool kBuiltinCamera = false;
#endif

inline constexpr Device::Profile kProfile{
    "waveshare_touch_lcd_10_1",
    "Waveshare Touch LCD 10.1",
    1280,
    800,
    7,
    5,
    16,
    4,
    168,
    145,
    5,
    121,
    Device::RotationStepMode::FlipOnly,
    2,
    0,
    Device::Capabilities{false, false, false, false, true, false, kBuiltinCamera},
    kHardwareIoProfile,
};

}  // namespace DeviceWaveshareTouchLCD10Profile
