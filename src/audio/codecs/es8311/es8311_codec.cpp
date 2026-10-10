/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted for HomeTiles from esp-bsp components/es8311/es8311.c: the playback
 * part only, ported to the i2c_master driver, with the coefficient rows for
 * MCLK = 256 x fs. See PROVENANCE.md.
 */

#include "src/audio/speaker_select.h"

#if defined(HOMETILES_SPEAKER)

#include "src/audio/codecs/es8311/es8311_codec.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace es8311 {
namespace {

constexpr uint8_t kRegReset = 0x00;
constexpr uint8_t kRegClk01 = 0x01;
constexpr uint8_t kRegClk02 = 0x02;
constexpr uint8_t kRegClk03 = 0x03;
constexpr uint8_t kRegClk04 = 0x04;
constexpr uint8_t kRegClk05 = 0x05;
constexpr uint8_t kRegClk06 = 0x06;
constexpr uint8_t kRegClk07 = 0x07;
constexpr uint8_t kRegClk08 = 0x08;
constexpr uint8_t kRegSdpIn = 0x09;
constexpr uint8_t kRegSdpOut = 0x0A;
constexpr uint8_t kRegSystem0D = 0x0D;
constexpr uint8_t kRegSystem0E = 0x0E;
constexpr uint8_t kRegSystem12 = 0x12;
constexpr uint8_t kRegSystem13 = 0x13;
constexpr uint8_t kRegAdc1C = 0x1C;
constexpr uint8_t kRegDac31 = 0x31;
constexpr uint8_t kRegDac32 = 0x32;
constexpr uint8_t kRegDac37 = 0x37;
constexpr uint8_t kRegChipId1 = 0xFD;
constexpr uint8_t kRegChipId2 = 0xFE;

constexpr int kI2cTimeoutMs = 100;
constexpr uint32_t kI2cSpeedHz = 100000;

struct CoeffDiv {
  uint32_t mclk;
  uint32_t rate;
  uint8_t pre_div;
  uint8_t pre_multi;
  uint8_t adc_div;
  uint8_t dac_div;
  uint8_t fs_mode;
  uint8_t lrck_h;
  uint8_t lrck_l;
  uint8_t bclk_div;
  uint8_t adc_osr;
  uint8_t dac_osr;
};

// Rows of the esp-bsp coeff_div[] table whose MCLK is 256 x rate.
constexpr CoeffDiv kCoeffs[] = {
    {2048000, 8000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 11025, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 12000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000, 16000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 22050, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 24000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 32000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {11289600, 44100, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 48000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
};

const CoeffDiv* findCoeff(uint32_t rate) {
  for (const CoeffDiv& coeff : kCoeffs) {
    if (coeff.rate == rate && coeff.mclk == rate * kMclkMultiple) return &coeff;
  }
  return nullptr;
}

esp_err_t writeReg(Codec* codec, uint8_t reg, uint8_t value) {
  const uint8_t buffer[2] = {reg, value};
  return i2c_master_transmit(codec->dev, buffer, sizeof(buffer), kI2cTimeoutMs);
}

esp_err_t readReg(Codec* codec, uint8_t reg, uint8_t* value) {
  return i2c_master_transmit_receive(codec->dev, &reg, 1, value, 1, kI2cTimeoutMs);
}

#define ES8311_TRY(expr)              \
  do {                                \
    const esp_err_t err_ = (expr);    \
    if (err_ != ESP_OK) return err_;  \
  } while (0)

}  // namespace

bool supportsSampleRate(uint32_t sample_rate) { return findCoeff(sample_rate) != nullptr; }

esp_err_t probe(i2c_master_bus_handle_t bus, uint8_t address, Codec* codec) {
  if (!bus || !codec) return ESP_ERR_INVALID_ARG;
  codec->dev = nullptr;
  i2c_device_config_t config = {};
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = address;
  config.scl_speed_hz = kI2cSpeedHz;
  i2c_master_dev_handle_t dev = nullptr;
  ES8311_TRY(i2c_master_bus_add_device(bus, &config, &dev));
  codec->dev = dev;
  uint8_t id1 = 0;
  uint8_t id2 = 0;
  esp_err_t err = readReg(codec, kRegChipId1, &id1);
  if (err == ESP_OK) err = readReg(codec, kRegChipId2, &id2);
  if (err == ESP_OK && (id1 != 0x83 || id2 != 0x11)) err = ESP_ERR_NOT_FOUND;
  if (err != ESP_OK) release(codec);
  return err;
}

esp_err_t setSampleRate(Codec* codec, uint32_t sample_rate) {
  if (!codec || !codec->dev) return ESP_ERR_INVALID_STATE;
  const CoeffDiv* coeff = findCoeff(sample_rate);
  if (!coeff) return ESP_ERR_NOT_SUPPORTED;
  uint8_t value = 0;

  ES8311_TRY(readReg(codec, kRegClk02, &value));
  value &= 0x07;
  value |= (coeff->pre_div - 1) << 5;
  value |= coeff->pre_multi << 3;
  ES8311_TRY(writeReg(codec, kRegClk02, value));

  ES8311_TRY(writeReg(codec, kRegClk03, (coeff->fs_mode << 6) | coeff->adc_osr));
  ES8311_TRY(writeReg(codec, kRegClk04, coeff->dac_osr));
  ES8311_TRY(writeReg(codec, kRegClk05, ((coeff->adc_div - 1) << 4) | (coeff->dac_div - 1)));

  ES8311_TRY(readReg(codec, kRegClk06, &value));
  value &= 0xE0;
  value |= coeff->bclk_div < 19 ? (coeff->bclk_div - 1) : coeff->bclk_div;
  ES8311_TRY(writeReg(codec, kRegClk06, value));

  ES8311_TRY(readReg(codec, kRegClk07, &value));
  value &= 0xC0;
  value |= coeff->lrck_h;
  ES8311_TRY(writeReg(codec, kRegClk07, value));
  return writeReg(codec, kRegClk08, coeff->lrck_l);
}

esp_err_t init(Codec* codec, uint32_t sample_rate) {
  if (!codec || !codec->dev) return ESP_ERR_INVALID_STATE;
  if (!findCoeff(sample_rate)) return ESP_ERR_NOT_SUPPORTED;

  // Reset to defaults, then the power-on command.
  ES8311_TRY(writeReg(codec, kRegReset, 0x1F));
  vTaskDelay(pdMS_TO_TICKS(20));
  ES8311_TRY(writeReg(codec, kRegReset, 0x00));
  ES8311_TRY(writeReg(codec, kRegReset, 0x80));

  // MCLK from the MCLK pin, not inverted; every clock enabled.
  ES8311_TRY(writeReg(codec, kRegClk01, 0x3F));
  uint8_t value = 0;
  ES8311_TRY(readReg(codec, kRegClk06, &value));
  value &= ~(1U << 5);  // SCLK not inverted.
  ES8311_TRY(writeReg(codec, kRegClk06, value));
  ES8311_TRY(setSampleRate(codec, sample_rate));

  // Slave serial port, standard I2S, 16-bit in and out.
  ES8311_TRY(readReg(codec, kRegReset, &value));
  ES8311_TRY(writeReg(codec, kRegReset, value & 0xBF));
  ES8311_TRY(writeReg(codec, kRegSdpIn, 3 << 2));
  ES8311_TRY(writeReg(codec, kRegSdpOut, 3 << 2));

  ES8311_TRY(writeReg(codec, kRegSystem0D, 0x01));  // Power up analog circuitry.
  ES8311_TRY(writeReg(codec, kRegSystem0E, 0x02));  // Analog PGA, ADC modulator.
  ES8311_TRY(writeReg(codec, kRegSystem12, 0x00));  // Power up the DAC.
  ES8311_TRY(writeReg(codec, kRegSystem13, 0x10));  // Output to the HP driver.
  ES8311_TRY(writeReg(codec, kRegAdc1C, 0x6A));     // ADC EQ bypass, DC offset cancel.
  return writeReg(codec, kRegDac37, 0x08);          // Bypass the DAC equalizer.
}

esp_err_t setVolumeRegister(Codec* codec, uint8_t value) {
  if (!codec || !codec->dev) return ESP_ERR_INVALID_STATE;
  return writeReg(codec, kRegDac32, value);
}

esp_err_t setMute(Codec* codec, bool mute) {
  if (!codec || !codec->dev) return ESP_ERR_INVALID_STATE;
  uint8_t value = 0;
  ES8311_TRY(readReg(codec, kRegDac31, &value));
  if (mute) {
    value |= (1U << 6) | (1U << 5);
  } else {
    value &= ~((1U << 6) | (1U << 5));
  }
  return writeReg(codec, kRegDac31, value);
}

void release(Codec* codec) {
  if (!codec || !codec->dev) return;
  i2c_master_bus_rm_device(codec->dev);
  codec->dev = nullptr;
}

#undef ES8311_TRY

}  // namespace es8311

#endif  // defined(HOMETILES_SPEAKER)
