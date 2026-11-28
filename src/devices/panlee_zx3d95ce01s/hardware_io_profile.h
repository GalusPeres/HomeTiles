#pragma once

#include "src/devices/hardware_io_profile.h"

namespace DevicePanleeZX3D95CE01S {

// The RS485 pair (GPIO1/2), the SHT20 bus and the sub board header are not
// verified for this exact revision yet, so no configurable GPIOs are offered.
inline constexpr Device::HardwareIoProfile kHardwareIoProfile{};

}  // namespace DevicePanleeZX3D95CE01S
