#pragma once

#include "src/devices/hardware_io_profile.h"

namespace DeviceElecrowCrowPanel10Profile {

inline constexpr Device::HardwareIoPinOption kHardwareIoPins[] = {
    {2, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 2"},
    {3, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 3"},
    {4, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 4"},
    {5, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 5"},
    {7, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 7 (I2C SDA)"},
    {8, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 8 (I2C SCL)"},
    {9, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 9 (Touch INT)"},
    {26, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 26 (Backlight)"},
    {42, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 42"},
    {43, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 43"},
    {44, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 44"},
    {45, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 45"},
    {46, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 46"},
    {47, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 47"},
    {48, Device::kHardwareIoPinSwitch | Device::kHardwareIoPinTemperature,
     "GPIO 48"},
};

inline constexpr Device::HardwareIoProfile kHardwareIoProfile =
    Device::makeHardwareIoProfile(kHardwareIoPins);

}  // namespace DeviceElecrowCrowPanel10Profile
