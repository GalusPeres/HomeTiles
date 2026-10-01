#pragma once

#include "src/devices/hardware_io_profile.h"

namespace DeviceAdafruitQualiaS3720 {

// Every Qualia header pin not consumed by the RGB bus, expander or touch bus
// is unverified for HomeTiles use, so no configurable GPIOs are exposed.
inline constexpr Device::HardwareIoProfile kHardwareIoProfile{};

}  // namespace DeviceAdafruitQualiaS3720
