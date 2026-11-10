#pragma once

#include "src/devices/guition_jc1060p470c_v2/device_guition_jc1060p470c_v2.h"

namespace DeviceGuitionJC1060P470CV3 {

using namespace DeviceGuitionJC1060P470CV2;

#if defined(HOMETILES_LOCAL_CAMERA)
inline constexpr bool kBuiltinCamera = true;
#else
inline constexpr bool kBuiltinCamera = false;
#endif

inline constexpr Device::Profile kProfile{
    "guition_jc1060p470c_v3",
    "Guition JC1060P470C V3",
    1024,
    600,
    6,
    4,
    16,
    4,
    156,
    136,
    5,
    121,
    Device::RotationStepMode::FlipOnly,
    0,
    2,
    Device::Capabilities{false, false, false, false, false, false, kBuiltinCamera},
    Device::kNoHardwareIoProfile,
};

}  // namespace DeviceGuitionJC1060P470CV3
