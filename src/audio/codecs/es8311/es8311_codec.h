#pragma once

// Minimal ES8311 mono codec driver for playback (DAC path only), on the
// ESP-IDF i2c_master bus driver that the display and touch already own.
// Register sequence and clock coefficients come from Espressif's esp-bsp
// es8311 component (see PROVENANCE.md); that component uses the legacy I2C
// driver, which cannot share a bus with i2c_master devices.
//
// The codec runs as I2S slave with MCLK from the MCLK pin at 256 x fs.
// Not thread-safe: one task (the speaker worker) owns the handle.

#include <stddef.h>
#include <stdint.h>

#include <driver/i2c_master.h>
#include <esp_err.h>

namespace es8311 {

// 7-bit address with CE low (Waveshare 7B: 0x18).
constexpr uint8_t kDefaultAddress = 0x18;
// MCLK the I2S peripheral must provide: kMclkMultiple x sample rate.
constexpr uint32_t kMclkMultiple = 256;

struct Codec {
  i2c_master_dev_handle_t dev = nullptr;
};

// True for the sample rates the clock table covers at 256 x fs.
bool supportsSampleRate(uint32_t sample_rate);

// Adds the device to the bus and checks the chip ID (0xFD/0xFE = 0x83/0x11).
// On failure the device is removed again and codec->dev stays nullptr.
esp_err_t probe(i2c_master_bus_handle_t bus, uint8_t address, Codec* codec);
// Resets the codec and configures 16-bit I2S slave playback at sample_rate.
esp_err_t init(Codec* codec, uint32_t sample_rate);
// Reprograms the clock dividers for a new sample rate (MCLK = 256 x rate).
esp_err_t setSampleRate(Codec* codec, uint32_t sample_rate);
// DAC digital volume register 0x32: 0 = -95.5 dB, 0xBF = 0 dB, 0.5 dB steps.
esp_err_t setVolumeRegister(Codec* codec, uint8_t value);
esp_err_t setMute(Codec* codec, bool mute);
// Removes the device from the bus.
void release(Codec* codec);

}  // namespace es8311
