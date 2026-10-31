#pragma once

#include "src/devices/hardware_io_profile.h"

namespace DeviceGoldenSecurityGST3EV24 {

// PCB v2.4 (ZIGBEE_4CP_WNB_V2.4, 2024.09.09) relay pins:
//   Relay 1 = GPIO 45, Relay 2 = GPIO 46, Relay 3 = GPIO 2
// GPIO45 and GPIO46 are relays on every published variant. GPIO2 replaces
// GPIO44 (v2.3's third relay, which is also UART0 RX).
// Source: ESPHome TuYa T3E device page for PCB v2.4.
inline constexpr Device::HardwareIoPinOption kHardwareIoPins[] = {
    {45, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinOnboard,
     "Relay (GPIO 45)", nullptr, Device::HardwareIoOutputLogic::ActiveHigh},
    {46, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinOnboard,
     "Relay (GPIO 46)", nullptr, Device::HardwareIoOutputLogic::ActiveHigh},
    {2,  Device::kHardwareIoPinSwitch | Device::kHardwareIoPinOnboard,
     "Relay (GPIO 2)",  nullptr, Device::HardwareIoOutputLogic::ActiveHigh},
};

inline constexpr Device::HardwareIoProfile kHardwareIoProfile =
    Device::makeHardwareIoProfile(kHardwareIoPins);

}  // namespace DeviceGoldenSecurityGST3EV24
