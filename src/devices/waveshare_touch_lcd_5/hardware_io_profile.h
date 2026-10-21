#pragma once

#include "src/devices/hardware_io_profile.h"

namespace DeviceWaveshareTouchLCD5Profile {

// Waveshare's LCD-5 documentation (docs/IO.md) does not assign any header
// GPIO to application use and routes the touch reset and interrupt nets to
// GPIO23 and GPIO2. Keep every panel, touch, audio, SDMMC and ESP-Hosted
// signal reserved rather than exposing an unverified pin.
inline constexpr Device::HardwareIoProfile kHardwareIoProfile =
    Device::kNoHardwareIoProfile;

}  // namespace DeviceWaveshareTouchLCD5Profile
