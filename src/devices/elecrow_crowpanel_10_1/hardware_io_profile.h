#pragma once

#include "src/devices/hardware_io_profile.h"

namespace DeviceElecrowCrowPanel10Profile {

// The UART1 header (PH2.0-4P, Elecrow wiki: RX IO48, TX IO47, 3.3 V) carries
// nothing else on the board. The I2C header shares the touch bus (SDA 45,
// SCL 46) and UART3-IN is the power input (RX 33, TX 34); both stay reserved,
// as do the panel, touch, backlight, audio, microphone, camera, SD, ESP-Hosted
// and wireless-module socket signals.
inline constexpr Device::HardwareIoPinOption kHardwareIoPins[] = {
    {47, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "UART1 header GPIO 47"},
    {48, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "UART1 header GPIO 48"},
};

inline constexpr Device::HardwareIoProfile kHardwareIoProfile =
    Device::makeHardwareIoProfile(kHardwareIoPins);

}  // namespace DeviceElecrowCrowPanel10Profile
