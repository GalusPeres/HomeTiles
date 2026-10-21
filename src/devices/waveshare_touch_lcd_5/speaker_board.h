#pragma once

// Speaker and microphone wiring of the Waveshare ESP32-P4-WIFI6-Touch-LCD-5,
// from Waveshare's BSP for this board (waveshareteam/Waveshare-ESP32-components,
// bsp/esp32_p4_wifi6_touch_lcd_5, commit 23a6012d5e87d27399cf90fb851cab3689e29159,
// include/bsp/esp32_p4_wifi6_touch_lcd_5.h and esp32_p4_wifi6_touch_lcd_5.c)
// and the board repository (waveshareteam/ESP32-P4-WIFI6-Touch-LCD-5, commit
// d144b34d8f384e84d2442e8733523560216a2f63: docs/IO.md, checked there against
// the schematic; examples/arduino/examples/09_Audio_Playback,
// 10_Mic_Record and examples/esp-idf/06_I2SCodec/main/example_config.h),
// which all agree: ES8311 codec at I2C 0x18 (ES8311_CODEC_DEFAULT_ADDR,
// 09_Audio_Playback/es8311.h ES8311_ADDRESS_0) on the shared touch bus
// (SDA 7, SCL 8), power amplifier enabled by BSP_POWER_AMP_IO (GPIO53,
// pa_reverted = false; 09_Audio_Playback drives it HIGH = on: active high).
// The ES7210 microphone ADC sits at I2C 0x40 (ES7210_CODEC_DEFAULT_ADDR,
// 10_Mic_Record/es7210.h ES7210_AD1_AD0_00) on the same bus, shares MCLK,
// BCLK and LRCK, and sends its SDOUT on BSP_I2S_DSIN (GPIO11). The same pins
// as the LCD-X boards (7, 8 and 10.1 inch).

#include <driver/gpio.h>
#include <driver/i2c_master.h>

#include "src/audio/codecs/audio_chips.h"

namespace speaker_board {

// BSP_I2S_MCLK; 06_I2SCodec I2S_MCK_IO; 09/10 PIN_I2S_MCLK.
constexpr gpio_num_t kI2sMclk = GPIO_NUM_13;
// BSP_I2S_SCLK; 06_I2SCodec I2S_BCK_IO; 09/10 PIN_I2S_BCLK.
constexpr gpio_num_t kI2sBclk = GPIO_NUM_12;
// BSP_I2S_LCLK; 06_I2SCodec I2S_WS_IO; 09/10 PIN_I2S_LRCK.
constexpr gpio_num_t kI2sLrck = GPIO_NUM_10;
// Codec DSDIN: data from the P4 to the codec. BSP_I2S_DOUT; 06_I2SCodec
// I2S_DO_IO; 09_Audio_Playback PIN_I2S_DOUT.
constexpr gpio_num_t kI2sDout = GPIO_NUM_9;
// ES7210 SDOUT: data from the microphone ADC to the P4. BSP_I2S_DSIN;
// 06_I2SCodec I2S_DI_IO; 10_Mic_Record PIN_I2S_DIN.
constexpr gpio_num_t kI2sDin = GPIO_NUM_11;
constexpr audio_chips::Output kOutput = audio_chips::Output::Es8311;
constexpr uint8_t kCodecAddress = 0x18;
constexpr audio_chips::Input kInput = audio_chips::Input::Es7210;
// ES7210 7-bit address; 0 without an ES7210.
constexpr uint8_t kMicAddress = 0x40;
// BSP_POWER_AMP_IO; 06_I2SCodec GPIO_OUTPUT_PA; 09_Audio_Playback PIN_POWER_AMP.
constexpr gpio_num_t kAmpEnable = GPIO_NUM_53;
constexpr bool kAmpActiveHigh = true;
// Loudest clean level as a fraction of the shared volume scale (1 = the
// codec's full scale). The same ES8311 and amplifier circuit as the other
// Waveshare P4 boards, which all run at the full scale.
constexpr float kVolumeLimit = 1.0f;

// The touch-owned I2C bus; nullptr before Device init opened it.
i2c_master_bus_handle_t bus();

}  // namespace speaker_board
