#pragma once

#include "src/devices/hardware_io_profile.h"

namespace DeviceBTTPandaTouch {

// Every GPIO of this board belongs to the display, the touch controller or
// the USB host port, so the panel offers no configurable hardware I/O.
inline constexpr Device::HardwareIoProfile kHardwareIoProfile =
    Device::kNoHardwareIoProfile;

}  // namespace DeviceBTTPandaTouch
