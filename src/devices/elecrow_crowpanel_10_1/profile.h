#pragma once

#include "src/devices/device_select.h"
#include "src/devices/device_types.h"
#include "src/devices/elecrow_crowpanel_10_1/hardware_io_profile.h"

namespace DeviceElecrowCrowPanel10Profile {

inline constexpr uint32_t kFlashSizeBytes = 16U * 1024U * 1024U;
inline constexpr uint32_t kPsramSizeBytes = 32U * 1024U * 1024U;

inline constexpr Device::Profile kProfile{
    "elecrow_crowpanel_10_1",
    "Elecrow CrowPanel Advanced 10.1",
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
    Device::Capabilities{false, false, false, false, true, false, false},
    kHardwareIoProfile,
};

}  // namespace DeviceElecrowCrowPanel10Profile
