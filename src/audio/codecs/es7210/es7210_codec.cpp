/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted for HomeTiles from esp-bsp components/es7210/es7210.c: the capture
 * configuration for 16-bit standard I2S without TDM, ported to the i2c_master
 * driver, with the coefficient row for 16 kHz at MCLK = 256 x fs. See
 * PROVENANCE.md.
 */

#include "src/audio/speaker_select.h"

#if defined(HOMETILES_SPEAKER)

#include "src/audio/codecs/es7210/es7210_codec.h"

namespace es7210 {
namespace {

// Register names from esp-bsp es7210_reg.h.
constexpr uint8_t kRegReset00 = 0x00;
constexpr uint8_t kRegMainClk02 = 0x02;
constexpr uint8_t kRegLrckDivH04 = 0x04;
constexpr uint8_t kRegLrckDivL05 = 0x05;
constexpr uint8_t kRegPowerDown06 = 0x06;
constexpr uint8_t kRegOsr07 = 0x07;
constexpr uint8_t kRegTimeControl0_09 = 0x09;
constexpr uint8_t kRegTimeControl1_0A = 0x0A;
constexpr uint8_t kRegSdpInterface1_11 = 0x11;
constexpr uint8_t kRegSdpInterface2_12 = 0x12;
constexpr uint8_t kRegAdc34Hpf2_20 = 0x20;
constexpr uint8_t kRegAdc34Hpf1_21 = 0x21;
constexpr uint8_t kRegAdc12Hpf2_22 = 0x22;
constexpr uint8_t kRegAdc12Hpf1_23 = 0x23;
constexpr uint8_t kRegChipId1_3D = 0x3D;
constexpr uint8_t kRegChipId0_3E = 0x3E;
constexpr uint8_t kRegAnalog40 = 0x40;
constexpr uint8_t kRegMic12Bias41 = 0x41;
constexpr uint8_t kRegMic34Bias42 = 0x42;
constexpr uint8_t kRegMic1Gain43 = 0x43;
constexpr uint8_t kRegMic2Gain44 = 0x44;
constexpr uint8_t kRegMic3Gain45 = 0x45;
constexpr uint8_t kRegMic4Gain46 = 0x46;
constexpr uint8_t kRegMic1Power47 = 0x47;
constexpr uint8_t kRegMic2Power48 = 0x48;
constexpr uint8_t kRegMic3Power49 = 0x49;
constexpr uint8_t kRegMic4Power4A = 0x4A;
constexpr uint8_t kRegMic12Power4B = 0x4B;
constexpr uint8_t kRegMic34Power4C = 0x4C;

// es7210_set_i2s_format(): ES7210_I2S_FMT_I2S (0x00) | 16-bit (0x60); TDM off.
constexpr uint8_t kSdpStandardI2s16Bit = 0x00 | 0x60;
constexpr uint8_t kSdpTdmOff = 0x00;
// ES7210_MIC_BIAS_2V87.
constexpr uint8_t kMicBias2V87 = 0x70;
// ES7210_MIC_GAIN_30DB, written as (gain | 0x10) like es7210_set_mic_gain().
constexpr uint8_t kMicGain30Db = 10;
constexpr uint8_t kMicGainRegister = kMicGain30Db | 0x10;

constexpr int kI2cTimeoutMs = 100;
constexpr uint32_t kI2cSpeedHz = 100000;

struct CoeffDiv {
  uint32_t mclk;
  uint32_t lrck;
  uint8_t ss_ds;
  uint8_t adc_div;
  uint8_t dll;
  uint8_t doubler;
  uint8_t osr;
  uint8_t mclk_src;
  uint8_t lrck_h;
  uint8_t lrck_l;
};

// The esp-bsp es7210_coeff_div[] row for 16 kHz with MCLK = 256 x rate.
constexpr CoeffDiv kCoeffs[] = {
    {4096000, 16000, 0x00, 0x01, 0x01, 0x01, 0x20, 0x00, 0x01, 0x00},
};

const CoeffDiv* findCoeff(uint32_t rate) {
  for (const CoeffDiv& coeff : kCoeffs) {
    if (coeff.lrck == rate && coeff.mclk == rate * kMclkMultiple) return &coeff;
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

#define ES7210_TRY(expr)              \
  do {                                \
    const esp_err_t err_ = (expr);    \
    if (err_ != ESP_OK) return err_;  \
  } while (0)

// es7210_set_i2s_sample_rate().
esp_err_t setSampleRate(Codec* codec, const CoeffDiv* coeff) {
  ES7210_TRY(writeReg(codec, kRegOsr07, coeff->osr));
  ES7210_TRY(writeReg(codec, kRegMainClk02,
                      static_cast<uint8_t>(coeff->adc_div | (coeff->doubler << 6) |
                                           (coeff->dll << 7))));
  ES7210_TRY(writeReg(codec, kRegLrckDivH04, coeff->lrck_h));
  return writeReg(codec, kRegLrckDivL05, coeff->lrck_l);
}

}  // namespace

bool supportsSampleRate(uint32_t sample_rate) { return findCoeff(sample_rate) != nullptr; }

esp_err_t probe(i2c_master_bus_handle_t bus, uint8_t address, Codec* codec,
                uint16_t* id_out) {
  if (!bus || !codec) return ESP_ERR_INVALID_ARG;
  codec->dev = nullptr;
  if (id_out) *id_out = 0;
  i2c_device_config_t config = {};
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = address;
  config.scl_speed_hz = kI2cSpeedHz;
  i2c_master_dev_handle_t dev = nullptr;
  ES7210_TRY(i2c_master_bus_add_device(bus, &config, &dev));
  codec->dev = dev;
  uint8_t id1 = 0;
  uint8_t id0 = 0;
  esp_err_t err = readReg(codec, kRegChipId1_3D, &id1);
  if (err == ESP_OK) err = readReg(codec, kRegChipId0_3E, &id0);
  if (err != ESP_OK) {
    release(codec);
    return err;
  }
  if (id_out) *id_out = static_cast<uint16_t>((id1 << 8) | id0);
  return ESP_OK;
}

// es7210_config_codec() with the HomeTiles settings.
esp_err_t init(Codec* codec, uint32_t sample_rate) {
  if (!codec || !codec->dev) return ESP_ERR_INVALID_STATE;
  const CoeffDiv* coeff = findCoeff(sample_rate);
  if (!coeff) return ESP_ERR_NOT_SUPPORTED;

  // Software reset.
  ES7210_TRY(writeReg(codec, kRegReset00, 0xFF));
  ES7210_TRY(writeReg(codec, kRegReset00, 0x32));
  // Initialization time when the device powers up.
  ES7210_TRY(writeReg(codec, kRegTimeControl0_09, 0x30));
  ES7210_TRY(writeReg(codec, kRegTimeControl1_0A, 0x30));
  // High-pass filters of ADC1-4.
  ES7210_TRY(writeReg(codec, kRegAdc12Hpf1_23, 0x2A));
  ES7210_TRY(writeReg(codec, kRegAdc12Hpf2_22, 0x0A));
  ES7210_TRY(writeReg(codec, kRegAdc34Hpf1_21, 0x2A));
  ES7210_TRY(writeReg(codec, kRegAdc34Hpf2_20, 0x0A));
  // 16-bit standard I2S, TDM off: ADC1/ADC2 (MIC1/MIC2) on SDOUT1.
  ES7210_TRY(writeReg(codec, kRegSdpInterface1_11, kSdpStandardI2s16Bit));
  ES7210_TRY(writeReg(codec, kRegSdpInterface2_12, kSdpTdmOff));
  // Analog power and VMID voltage.
  ES7210_TRY(writeReg(codec, kRegAnalog40, 0xC3));
  ES7210_TRY(writeReg(codec, kRegMic12Bias41, kMicBias2V87));
  ES7210_TRY(writeReg(codec, kRegMic34Bias42, kMicBias2V87));
  ES7210_TRY(writeReg(codec, kRegMic1Gain43, kMicGainRegister));
  ES7210_TRY(writeReg(codec, kRegMic2Gain44, kMicGainRegister));
  ES7210_TRY(writeReg(codec, kRegMic3Gain45, kMicGainRegister));
  ES7210_TRY(writeReg(codec, kRegMic4Gain46, kMicGainRegister));
  // Power on MIC1-4.
  ES7210_TRY(writeReg(codec, kRegMic1Power47, 0x08));
  ES7210_TRY(writeReg(codec, kRegMic2Power48, 0x08));
  ES7210_TRY(writeReg(codec, kRegMic3Power49, 0x08));
  ES7210_TRY(writeReg(codec, kRegMic4Power4A, 0x08));
  ES7210_TRY(setSampleRate(codec, coeff));
  // Power down the DLL.
  ES7210_TRY(writeReg(codec, kRegPowerDown06, 0x04));
  // Power on MIC1-4 bias, ADC1-4 and PGA1-4.
  ES7210_TRY(writeReg(codec, kRegMic12Power4B, 0x0F));
  ES7210_TRY(writeReg(codec, kRegMic34Power4C, 0x0F));
  // Enable the device.
  ES7210_TRY(writeReg(codec, kRegReset00, 0x71));
  return writeReg(codec, kRegReset00, 0x41);
}

void release(Codec* codec) {
  if (!codec || !codec->dev) return;
  i2c_master_bus_rm_device(codec->dev);
  codec->dev = nullptr;
}

#undef ES7210_TRY

}  // namespace es7210

#endif  // defined(HOMETILES_SPEAKER)
