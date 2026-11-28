#pragma once

#include "src/devices/device_types.h"
#include "src/devices/panlee_zx3d95ce01s/hardware_io_profile.h"
#include "src/devices/panlee_zx3d95ce01s/gc9503v_s3_board.h"
#include "src/devices/panlee_zx3d95ce01s/gc9503v_s3_driver.h"

// Panlee ZX3D95CE01S (V12): 86-box wall panel with a WT32-S3-WROVER-N16R8
// (16 MB quad flash, 8 MB octal PSRAM), a 3.95-inch 480x480 GC9503V RGB panel,
// FT6336 touch and, depending on the variant, SHT20, RS485 and USB. It has
// its own driver (device_panlee_zx3d95ce01s.cpp, derived from the GS-T3E v2
// one) with the description below; pins and their sources are in README.md.
namespace DevicePanleeZX3D95CE01S {

inline constexpr Device::Profile kProfile{
    "panlee_zx3d95ce01s",
    "Panlee ZX3D95CE01S",
    480,
    480,
    4,
    4,
    10,
    3,
    111,
    111,
    4,
    // Same 150 Hz PWM and calibrated floor as the other S3 480x480 boards;
    // not measured on this board yet.
    39,
    Device::RotationStepMode::QuarterTurns,
    // openHASP runs panel and touch unrotated. Settings flips it.
    0,
    2,
    Device::Capabilities{false, false, false, false, false, false},
    kHardwareIoProfile,
};

inline constexpr Gc9503vS3Board::Description kBoard{
    "Panlee ZX3D95CE01S",
    // GC9503V SPI command bus: CS38, SCK45, MOSI48. SCK and MOSI are also RGB
    // B0/B1: the driver sends the init table before the RGB panel takes the
    // pins and never addresses the panel afterwards.
    38,
    45,
    48,
    // openHASP names the VSYNC pin (41) as TFT_RST: the panel resets itself
    // (RC) and the pin is only pulsed before the RGB panel takes it over.
    41,
    Gc9503vS3Board::RgbPins{
        40,  // DE
        41,  // VSYNC
        42,  // HSYNC
        39,  // PCLK
        {18, 8, 3, 46, 10},
        {14, 13, 12, 11, 16, 17},
        {45, 48, 47, 0, 21},
    },
    // openHASP's TR-4848 environment (hsync 8/10/40, vsync 10/10/40).
    Gc9503vS3Board::SyncTiming{8, 10, 40},
    Gc9503vS3Board::SyncTiming{10, 10, 40},
    // openHASP never sets TFT_PCLK_ACTIVE_NEG for this board: rising edge.
    false,
    // Backlight GPIO5 per openHASP ("datasheet states 4, schematics 5"; the
    // UR datasheet's 4 is the touch INT on this board). 150 Hz like the other
    // S3 boards; the best rate is unmeasured.
    5,
    150,
    // FT6336 I2C: SDA15, SCL6, address 0x38; INT is GPIO4 and unused.
    15,
    6,
    0x38,
    // No microSD slot is documented for this board.
    false,
    Gc9503vS3Board::kNoSdCard,
    // Not described here, because nobody could test them: some variants carry
    // an SHT20 temperature/humidity sensor (I2C, address 0x40) and an RS485
    // chip (GPIO1/GPIO2 on the UR datasheet). Neither is read or driven, and
    // the I2C pins of the SHT20 are not documented for this revision. A
    // board owner who can test them adds the sensor here.
};

}  // namespace DevicePanleeZX3D95CE01S
