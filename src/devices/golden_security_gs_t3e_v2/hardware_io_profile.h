#pragma once

#include "src/devices/hardware_io_profile.h"

namespace DeviceGoldenSecurityGST3EV2 {

// The relays of the PCB v2.3 (openHASP `[odm-t3e-v2]`: GPIO45/46/44). One
// board has all of them, so none needs a board variant: a save holds one
// variant, and a variant per relay let the panel take only one of them at a
// time (issue #93, "GPIO 46 is not available"). GPIO45 and GPIO46 are relays
// on every published variant (CN 4-gang GPIO1/2/46/45, ESPHome EU v2.4
// GPIO45/46/2, "inverted: false"). GPIO44 is the third relay of the v2.3,
// confirmed by a board owner in issue #93; it is also UART0 RX, so while it
// drives the relay the serial console takes no input (the log output on
// GPIO43 stays).
inline constexpr Device::HardwareIoPinOption kHardwareIoPins[] = {
    {45, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinOnboard,
     "Relay (GPIO 45)", nullptr, Device::HardwareIoOutputLogic::ActiveHigh},
    {46, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinOnboard,
     "Relay (GPIO 46)", nullptr, Device::HardwareIoOutputLogic::ActiveHigh},
    {44, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinOnboard,
     "Relay (GPIO 44)", nullptr, Device::HardwareIoOutputLogic::ActiveHigh},
};

inline constexpr Device::HardwareIoProfile kHardwareIoProfile =
    Device::makeHardwareIoProfile(kHardwareIoPins);

}  // namespace DeviceGoldenSecurityGST3EV2
