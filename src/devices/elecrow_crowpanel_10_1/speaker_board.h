#pragma once

// Speaker and microphone wiring of the Waveshare
// ESP32-P4-WIFI6-Touch-LCD-10.1, from Waveshare's BSP for the LCD-X main
// board of the 7, 8 and 10.1 inch panels
// (waveshareteam/Waveshare-ESP32-components,
// bsp/esp32_p4_wifi6_touch_lcd_x): ES8311 codec at I2C 0x18
// (ES8311_CODEC_DEFAULT_ADDR) on the shared touch bus (I2C0, SDA 7, SCL 8),
// power amplifier enabled by BSP_POWER_AMP_IO (GPIO53, pa_reverted = false:
// active high). The ES7210 microphone ADC sits at I2C 0x40
// (ES7210_CODEC_DEFAULT_ADDR) on the same bus, shares MCLK, BCLK and LRCK,
// and sends its SDOUT on BSP_I2S_DSIN (GPIO11). The same pins as the 8 inch.

#include <driver/gpio.h>
#include <driver/i2c_master.h>

#include "src/audio/codecs/audio_chips.h"

namespace speaker_board {

constexpr gpio_num_t kI2sMclk = GPIO_NUM_13;
constexpr gpio_num_t kI2sBclk = GPIO_NUM_12;
constexpr gpio_num_t kI2sLrck = GPIO_NUM_10;
// Codec DSDIN: data from the P4 to the codec.
constexpr gpio_num_t kI2sDout = GPIO_NUM_9;
// ES7210 SDOUT: data from the microphone ADC to the P4.
constexpr gpio_num_t kI2sDin = GPIO_NUM_11;
constexpr audio_chips::Output kOutput = audio_chips::Output::Es8311;
constexpr uint8_t kCodecAddress = 0x18;
constexpr audio_chips::Input kInput = audio_chips::Input::Es7210;
// ES7210 7-bit address; 0 without an ES7210.
constexpr uint8_t kMicAddress = 0x40;
constexpr gpio_num_t kAmpEnable = GPIO_NUM_53;
constexpr bool kAmpActiveHigh = true;
// Loudest clean level as a fraction of the shared volume scale (1 = the
// codec's full scale, fine on this board's amplifier).
constexpr float kVolumeLimit = 1.0f;

// The touch-owned I2C bus; nullptr before Device init opened it.
i2c_master_bus_handle_t bus();

}  // namespace speaker_board
