#pragma once

#include "src/devices/hardware_io_profile.h"

namespace DeviceBTTPandaTouch {

// The I2C expansion header P2 (BIGTREETECH PandaTouch_PlatformIO
// docs/PINOUT.md 2.6: SDA1 GPIO 4, SCL1 GPIO 3, 3.3 V, GND) carries nothing
// else on the board. The panel, touch (I2C0), backlight, USB host (GPIO 19/20)
// and debug UART (GPIO 43/44) signals stay reserved.
inline constexpr Device::HardwareIoPinOption kHardwareIoPins[] = {
    {4, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "Expansion header P2 GPIO 4 (SDA1)"},
    {3, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "Expansion header P2 GPIO 3 (SCL1)"},
};

inline constexpr Device::HardwareIoProfile kHardwareIoProfile =
    Device::makeHardwareIoProfile(kHardwareIoPins);

}  // namespace DeviceBTTPandaTouch
