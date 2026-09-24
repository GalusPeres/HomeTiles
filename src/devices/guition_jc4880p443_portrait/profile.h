#pragma once

#include "src/devices/device_types.h"

namespace DeviceGuitionJC4880P443PortraitProfile {

inline constexpr Device::Profile kProfile{
    "guition_jc4880p443_portrait",
    "Guition JC4880P443 Portrait",
    480,
    800,
    4,
    6,
    10,
    3,
    111,
    124,
    4,
    1,
    Device::RotationStepMode::FlipOnly,
    0,
    2,
    Device::Capabilities{false, false, false, false, true, false},
    Device::kNoHardwareIoProfile,
};

}  // namespace DeviceGuitionJC4880P443PortraitProfile
