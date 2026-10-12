#pragma once

// Minimal ES7210 microphone ADC driver for capture (MIC1/MIC2 on SDOUT1 in
// standard I2S, no TDM), on the ESP-IDF i2c_master bus driver that the
// display, touch and the ES8311 already share. Register sequence and clock
// coefficients come from Espressif's esp-bsp es7210 component (see
// PROVENANCE.md); that component uses the legacy I2C driver, which cannot
// share a bus with i2c_master devices.
//
// The ADC runs as I2S slave with MCLK from the MCLK pin at 256 x fs.
// Not thread-safe: one task (the speaker worker) owns the handle.

#include <stddef.h>
#include <stdint.h>

#include <driver/i2c_master.h>
#include <esp_err.h>

namespace es7210 {

// 7-bit address with AD0 and AD1 low (Waveshare 7B: 0x40).
constexpr uint8_t kDefaultAddress = 0x40;
// MCLK the I2S peripheral must provide: kMclkMultiple x sample rate.
constexpr uint32_t kMclkMultiple = 256;

struct Codec {
  i2c_master_dev_handle_t dev = nullptr;
};

// True for the sample rates the clock table covers at 256 x fs (16 kHz).
bool supportsSampleRate(uint32_t sample_rate);

// Adds the device to the bus and reads the chip ID registers 0x3D/0x3E; an
// answer on I2C counts as present. On failure the device is removed again
// and codec->dev stays nullptr. id_out (optional) receives 0x3D << 8 | 0x3E.
esp_err_t probe(i2c_master_bus_handle_t bus, uint8_t address, Codec* codec,
                uint16_t* id_out);
// Resets the ADC and configures 16-bit standard I2S slave capture at
// sample_rate (MCLK = 256 x rate), mic bias 2.87 V and 30 dB mic gain.
esp_err_t init(Codec* codec, uint32_t sample_rate);
// Removes the device from the bus.
void release(Codec* codec);

}  // namespace es7210
