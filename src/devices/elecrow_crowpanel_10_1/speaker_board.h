#pragma once

// Speaker wiring of the Elecrow CrowPanel Advanced 10.1 (Elecrow wiki "ESP32-P4
// and Audio out", Elecrow BSP of IDF lesson 11, bsp_audio/bsp_mic): a codec-less I2S power
// amplifier on LRCLK 21, BCLK 22, SDATA 23 with its enable on GPIO30, active
// low (set_Audio_ctrl(true) drives it 0), PH2.0 speaker jack (4 ohm 3 W). The
// PDM microphone runs on the P4's PDM receiver: CLK 24, DATA 26. No MCLK and
// no I2C: volume and mute are applied to the samples.

#include <driver/gpio.h>
#include <driver/i2c_master.h>

#include "src/audio/codecs/audio_chips.h"

namespace speaker_board {

constexpr gpio_num_t kI2sMclk = GPIO_NUM_NC;
constexpr gpio_num_t kI2sBclk = GPIO_NUM_22;
constexpr gpio_num_t kI2sLrck = GPIO_NUM_21;
constexpr gpio_num_t kI2sDout = GPIO_NUM_23;
// The microphone is not on these I2S lines (kPdmClk / kPdmDin below).
constexpr gpio_num_t kI2sDin = GPIO_NUM_NC;
constexpr gpio_num_t kPdmClk = GPIO_NUM_24;
constexpr gpio_num_t kPdmDin = GPIO_NUM_26;
constexpr gpio_num_t kAmpEnable = GPIO_NUM_30;
constexpr bool kAmpActiveHigh = false;
// Loudest clean level; hardware validation pending.
constexpr float kVolumeLimit = 1.0f;
constexpr audio_chips::Output kOutput = audio_chips::Output::I2sAmp;
constexpr uint8_t kCodecAddress = 0;
constexpr audio_chips::Input kInput = audio_chips::Input::Pdm;
constexpr uint8_t kMicAddress = 0;

// No audio chip on I2C.
i2c_master_bus_handle_t bus();

}  // namespace speaker_board
