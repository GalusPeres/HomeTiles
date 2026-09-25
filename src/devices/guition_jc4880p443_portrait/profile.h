#pragma once

#include "src/devices/device_select.h"
#include "src/devices/device_types.h"

namespace DeviceGuitionJC4880P443PortraitProfile {

// Optional OV02C10 on the CSI connector: enabled only in camera beta builds
// (HOMETILES_LOCAL_CAMERA in device_select.h) until hardware validation.
#if defined(HOMETILES_LOCAL_CAMERA)
inline constexpr bool kBuiltinCamera = true;
#else
inline constexpr bool kBuiltinCamera = false;
#endif

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
    Device::Capabilities{false, false, false, false, true, false, kBuiltinCamera},
    Device::kNoHardwareIoProfile,
};

}  // namespace DeviceGuitionJC4880P443PortraitProfile
