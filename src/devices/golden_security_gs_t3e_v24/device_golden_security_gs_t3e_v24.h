#pragma once

#include "src/devices/device_types.h"
#include "src/devices/golden_security_gs_t3e_v24/hardware_io_profile.h"
#include "src/devices/golden_security_gs_t3e_v24/st7701_s3_board.h"
#include "src/devices/golden_security_gs_t3e_v24/st7701_s3_driver.h"

// Golden Security GS-T3E v2 PCB v2.4 (ZIGBEE_4CP_WNB_V2.4, 2024.09.09):
// ESP32-S3-WROOM-1 N16R8, 480x480 ST7701S RGB panel and GT911 touch, built
// like the Guition ESP32-4848S040. PCB v2.4 removes the Micro-USB port and
// changes the third relay pin from GPIO44 (v2.3) to GPIO2. Build profile
// golden_security_gs_t3e_v24; pins and their sources are in README.md.
// Hardware validation is pending: no maintainer hardware.
namespace DeviceGoldenSecurityGST3EV24 {

inline constexpr Device::Profile kProfile{
    "golden_security_gs_t3e_v24",
    "Golden Security GS-T3E v2.4",
    480,
    480,
    4,
    4,
    10,
    3,
    111,
    111,
    4,
    // The Guition ESP32-4848S040's calibrated visible floor (raw 39) with
    // the same 150 Hz drive; not measured on this board yet.
    39,
    Device::RotationStepMode::QuarterTurns,
    // openHASP's gs-t3e and ESPHome's TuYa T3E configurations both run the
    // panel and GT911 unrotated; the Guition default (180 degrees) is not
    // carried over. Unconfirmed on hardware; Settings flips it.
    0,
    2,
    Device::Capabilities{false, false, false, false, false, false},
    kHardwareIoProfile,
};

inline constexpr St7701S3Board::Description kBoard{
    "GS-T3E v2.4",
    // ST7701 SPI command bus: CS4, SCK15, MOSI48. SCK and MOSI are also RGB
    // B0/B1: the driver sends the init table before the RGB panel takes the
    // pins and never addresses the panel afterwards. openHASP lists GPIO4 as
    // TFT_RST as well, so there is no separate reset line: the driver's
    // software reset (0x01) runs instead.
    4,
    15,
    48,
    St7701S3Board::RgbPins{
        40,  // DE
        39,  // VSYNC
        38,  // HSYNC
        41,  // PCLK
        {8, 18, 17, 16, 0},
        {13, 12, 11, 10, 9, 3},
        {15, 48, 47, 21, 14},
    },
    // openHASP's generic ST7701 branch uses this table; ESPHome's default
    // ST7701S sequence writes the same registers (CD=0x08) and its
    // invert_colors: true adds the same INVON.
    St7701S3Board::PanelInit::Type1,
    "type1",
    // openHASP's gs-t3e.ini sets TFT_PCLK_ACTIVE_NEG=1, but its ST7701
    // branch never passes it to Arduino_GFX (default 0), and ESPHome's T3E
    // configuration sets pclk_inverted: false. Both run the rising edge,
    // like the Guition board.
    false,
    // Backlight GPIO42, active high in openHASP and ESPHome. The best PWM
    // rate is unknown: openHASP's 20 kHz default cuts the screen off below
    // about 200/255 (the Guition symptom), ESPHome uses 1 kHz. Start with
    // the Guition's silent 150 Hz.
    42,
    150,
    // GT911 I2C: SDA7, SCL6; address 0x5D or 0x14, both probed.
    7,
    6,
    // No microSD slot is documented for this board.
    false,
    St7701S3Board::kNoSdCard,
};


}  // namespace DeviceGoldenSecurityGST3EV24
