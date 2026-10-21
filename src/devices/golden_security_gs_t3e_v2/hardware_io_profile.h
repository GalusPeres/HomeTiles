#pragma once

#include "src/devices/hardware_io_profile.h"

namespace DeviceGoldenSecurityGST3EV2 {

// GPIO45 and GPIO46 drive a relay on every published variant (openHASP: EU
// v2.3 GPIO45/46/44, CN 4-gang GPIO1/2/46/45; ESPHome: EU v2.4 GPIO45/46/2,
// "inverted: false"), so they are offered by GPIO number. The third and fourth
// relays differ between variants (GPIO44 and GPIO43 also carry the UART0 log
// on v2.3) and stay unexposed until a board owner confirms them.
inline constexpr Device::HardwareIoPinOption kHardwareIoPins[] = {
    {45, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinOnboard,
     "Relay (GPIO 45)", "gs_t3e_relay_gpio45",
     Device::HardwareIoOutputLogic::ActiveHigh},
    {46, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinOnboard,
     "Relay (GPIO 46)", "gs_t3e_relay_gpio46",
     Device::HardwareIoOutputLogic::ActiveHigh},
};

inline constexpr Device::HardwareIoProfile kHardwareIoProfile =
    Device::makeHardwareIoProfile(kHardwareIoPins);

}  // namespace DeviceGoldenSecurityGST3EV2
