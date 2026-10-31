#pragma once

#include "src/devices/hardware_io_profile.h"

namespace DeviceElecrowCrowPanel10Profile {

// Elecrow documents the panel, touch, backlight and SDMMC signals, but does
// not identify any header GPIO as a general-purpose HomeTiles I/O. Keep every
// known signal reserved instead of exposing an unverified pin.
inline constexpr Device::HardwareIoProfile kHardwareIoProfile =
    Device::kNoHardwareIoProfile;

}  // namespace DeviceElecrowCrowPanel10Profile
