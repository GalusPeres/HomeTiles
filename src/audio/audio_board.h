#pragma once

// The board's audio chips behind one small interface for the speaker worker
// (src/audio/speaker.cpp). Each board only describes itself in its
// speaker_board.h (selected in src/audio/speaker_select.h):
//
//   namespace speaker_board {
//   // I2S lines: the P4 is master and drives MCLK (256 x fs), BCLK, LRCK.
//   constexpr gpio_num_t kI2sMclk, kI2sBclk, kI2sLrck;
//   constexpr gpio_num_t kI2sDout;  // to the output codec's DSDIN
//   constexpr gpio_num_t kI2sDin;   // from the microphone ADC's SDOUT
//   // Chips (src/audio/codecs/audio_chips.h) and their 7-bit addresses.
//   constexpr audio_chips::Output kOutput;
//   constexpr uint8_t kCodecAddress;
//   constexpr audio_chips::Input kInput;
//   constexpr uint8_t kMicAddress;  // ES7210 only, else 0
//   // Amplifier enable pin, or GPIO_NUM_NC with HOMETILES_SPEAKER_AMP_BY_BOARD
//   // and the board's own setAmp() below.
//   constexpr gpio_num_t kAmpEnable;
//   constexpr bool kAmpActiveHigh;
//   // Loudest clean level as a fraction of the shared volume scale (1 =
//   // the codec's full scale); 100 % in Home Assistant plays at it.
//   constexpr float kVolumeLimit;
//   // i2c_master boards: the bus the codecs share (nullptr before Device
//   // init opened it). Boards with HOMETILES_CODEC_BUS_BY_BOARD implement
//   // codec_bus themselves instead.
//   i2c_master_bus_handle_t bus();
//   // Only with HOMETILES_SPEAKER_AMP_BY_BOARD.
//   void setAmp(bool on);
//   }
//
// Worker task only.

#include <stdint.h>

#include <esp_err.h>

namespace audio_board {

// Amplifier off and its pin set up; called once before any probe.
void initAmp();
void setAmp(bool on);

// Output codec: found and configured for playback at initial_rate. Logs.
bool probeOutput(uint32_t initial_rate);
bool outputSupportsRate(uint32_t rate);
esp_err_t setOutputRate(uint32_t rate);
// The I2S output opened and MCLK runs: codecs that start only on a running
// clock do it now (ES8388); a no-op for the ES8311.
esp_err_t startOutput();
// ES8311 volume scale: 0 = -95.5 dB, 0xBF = 0 dB, 0.5 dB steps.
esp_err_t setOutputVolumeRegister(uint8_t value);
esp_err_t setOutputMute(bool mute);
const char* outputName();
// Codec-less outputs (Output::I2sAmp): the volume and mute the samples get,
// in Q15 (32768 = full scale, 0 = muted). Codec boards: always 32768.
int32_t outputGainQ15();

// Microphone: true when the board has one and it answered. Logs.
bool probeInput(uint32_t rate, uint8_t gain_step);
// Configures the microphone ADC for capture at rate with the stored gain
// step (speaker::micGainTenthsDb()); call once the I2S clocks run.
esp_err_t startInput(uint32_t rate, uint8_t gain_step);
const char* inputName();

}  // namespace audio_board
