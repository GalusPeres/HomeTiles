/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for HomeTiles: the esp_cam_sensor OV02C10 register access is
 * re-implemented on top of an i2c_master device that shares the touch bus.
 * esp_cam_sensor/esp_sccb_intf are not part of the Arduino core.
 */

#include "src/video/local_camera/sensors/ov02c10/ov02c10_sensor.h"

#if defined(HOMETILES_CAMERA_SENSOR_OV02C10)

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "src/video/local_camera/sensors/ov02c10/ov02c10_settings.h"

namespace ov02c10 {
namespace {

constexpr int kTransferTimeoutMs = 50;
constexpr uint8_t kRemoveAttempts = 5;
constexpr uint32_t kRemoveRetryDelayMs = 2;
constexpr uint16_t kRegSensorIdHigh = 0x300a;
constexpr uint16_t kRegSensorIdLow = 0x300b;
constexpr uint16_t kRegStreamMode = 0x0100;
constexpr uint16_t kRegMipiCtrl00 = 0x4800;
constexpr uint16_t kRegMirror = 0x3821;
constexpr uint16_t kRegExposureHigh = 0x3501;
constexpr uint16_t kRegExposureLow = 0x3502;
constexpr uint16_t kRegAnalogGainHigh = 0x3508;
constexpr uint16_t kRegAnalogGainLow = 0x3509;
// Digital gain, 24 bit (Linux ov02c10.c OV02C10_REG_DIGITAL_GAIN): gain in
// 1/1024 steps shifted left by 6. The mode table writes 0x010041 (1x); its low
// six bits are kept.
constexpr uint16_t kRegDigitalGainHigh = 0x350a;
constexpr uint16_t kRegDigitalGainMid = 0x350b;
constexpr uint16_t kRegDigitalGainLow = 0x350c;
constexpr uint32_t kTableDigitalGainReg = 0x010041;
// Frame length (VTS) as written by the vendored mode table (0x380e/0x380f).
constexpr uint16_t kRegFrameLengthHigh = 0x380e;
constexpr uint16_t kRegFrameLengthLow = 0x380f;
// Line-sync packets on, clock-lane gating and bus idle as written by the demo
// driver with CONFIG_CAMERA_OV02C10_CSI_LINESYNC_ENABLE.
constexpr uint8_t kMipiCtrl00LineSync = 0x64;

// HomeTiles overrides, not vendor data (PROVENANCE.md). Datasheet v2.01
// table 4-1/4-2: 0x3820 FORMAT1 bit4 = vflip, bit3 = hmirror (0 = mirrored,
// the table readout has it clear); 0x3811/0x3813 = ISP x/y window offset.
// Linux ov02c10.c and Intel ipu6-drivers move the window by one pixel per
// flip to keep the Bayer phase; the vendored table reads out GBRG.
constexpr uint16_t kRegFormat1 = 0x3820;
constexpr uint8_t kFormat1Flip = 0x10;
constexpr uint8_t kFormat1Mirror = 0x08;
constexpr uint16_t kRegIspXWinLow = 0x3811;
constexpr uint16_t kRegIspYWinLow = 0x3813;
// Output 1280x720 inside the table's 1296x732 array window: offsets 11/8
// select the pixels of the former centred crop (table offsets 7/4 + 4/4).
constexpr uint8_t kWindowX = 0x0b;
constexpr uint8_t kWindowXMirrored = 0x08;
constexpr uint8_t kWindowY = 0x08;
constexpr uint8_t kWindowYFlipped = 0x07;
static_assert(kWindowX + kFrameWidth <= kArrayWindowWidth, "x window");
static_assert(kWindowXMirrored + kFrameWidth <= kArrayWindowWidth, "x window");
static_assert(kWindowY + kFrameHeight <= kArrayWindowHeight, "y window");
static_assert(kWindowYFlipped + kFrameHeight <= kArrayWindowHeight, "y window");
static_assert((kWindowX & 1) != (kWindowXMirrored & 1), "a mirror moves the x phase");
static_assert((kWindowY & 1) != (kWindowYFlipped & 1), "a flip moves the y phase");

const ov02c10_reginfo_t kWindow1280x720[] = {
    {0x3808, 0x05}, {0x3809, 0x00},  // x_output_size 1280
    {0x380a, 0x02}, {0x380b, 0xd0},  // y_output_size 720
    {kRegIspXWinLow, kWindowX},
    {kRegIspYWinLow, kWindowY},
    {OV02C10_REG_END, 0x00},
};

}  // namespace

esp_err_t Sensor::attach(i2c_master_bus_handle_t bus) {
  if (device_) return ESP_OK;
  if (!bus) return ESP_ERR_INVALID_STATE;
  i2c_device_config_t config = {};
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = kSccbAddress;
  config.scl_speed_hz = kSccbFrequencyHz;
  const esp_err_t err = i2c_master_bus_add_device(bus, &config, &device_);
  if (err != ESP_OK) {
    device_ = nullptr;
    return err;
  }
  bus_ = bus;
  return ESP_OK;
}

esp_err_t Sensor::detach() {
  if (!device_) {
    bus_ = nullptr;
    return ESP_OK;
  }
  // i2c_master_bus_rm_device() refuses with INVALID_STATE while a touch
  // transaction is being set up on the shared bus; retry briefly.
  esp_err_t err = ESP_ERR_INVALID_STATE;
  for (uint8_t attempt = 0; attempt < kRemoveAttempts; ++attempt) {
    if (attempt != 0) vTaskDelay(pdMS_TO_TICKS(kRemoveRetryDelayMs));
    err = i2c_master_bus_rm_device(device_);
    if (err != ESP_ERR_INVALID_STATE) break;
  }
  if (err != ESP_OK) return err;  // Keep the handle; the next detach retries.
  device_ = nullptr;
  bus_ = nullptr;
  return ESP_OK;
}

esp_err_t Sensor::write(uint16_t reg, uint8_t value) {
  if (!device_) return ESP_ERR_INVALID_STATE;
  const uint8_t buffer[3] = {static_cast<uint8_t>(reg >> 8),
                             static_cast<uint8_t>(reg & 0xff), value};
  return i2c_master_transmit(device_, buffer, sizeof(buffer), kTransferTimeoutMs);
}

esp_err_t Sensor::read(uint16_t reg, uint8_t* value) {
  if (!device_ || !value) return ESP_ERR_INVALID_STATE;
  const uint8_t address[2] = {static_cast<uint8_t>(reg >> 8),
                              static_cast<uint8_t>(reg & 0xff)};
  return i2c_master_transmit_receive(device_, address, sizeof(address), value, 1,
                                     kTransferTimeoutMs);
}

esp_err_t Sensor::probe(uint16_t* chip_id) {
  if (chip_id) *chip_id = 0;
  if (!bus_ || !device_) return ESP_ERR_INVALID_STATE;
  esp_err_t err = i2c_master_probe(bus_, kSccbAddress, kTransferTimeoutMs);
  if (err != ESP_OK) return err;
  uint8_t high = 0;
  uint8_t low = 0;
  err = read(kRegSensorIdHigh, &high);
  if (err == ESP_OK) err = read(kRegSensorIdLow, &low);
  if (err != ESP_OK) return err;
  const uint16_t id = static_cast<uint16_t>((high << 8) | low);
  if (chip_id) *chip_id = id;
  return id == kChipId ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t write_table(Sensor* sensor, const ov02c10_reginfo_t* table,
                             esp_err_t (Sensor::*write_fn)(uint16_t, uint8_t)) {
  esp_err_t err = ESP_OK;
  for (size_t i = 0; err == ESP_OK && table[i].reg != OV02C10_REG_END; ++i) {
    if (table[i].reg == OV02C10_REG_DELAY) {
      vTaskDelay(pdMS_TO_TICKS(table[i].val));
      continue;
    }
    err = (sensor->*write_fn)(table[i].reg, table[i].val);
    // The bus is shared with touch; let its reads interleave with the table.
    if ((i & 0x1f) == 0x1f) vTaskDelay(1);
  }
  return err;
}

esp_err_t Sensor::loadDefaultMode(bool mirror) {
  esp_err_t err = write_table(this, ov02c10_mipi_reset_regs, &Sensor::write);
  if (err != ESP_OK) return err;
  err = write_table(this, ov02c10_input_24M_MIPI_1lane_raw10_1288x728_30fps,
                    &Sensor::write);
  if (err != ESP_OK) return err;
  err = write_table(this, kWindow1280x720, &Sensor::write);
  if (err != ESP_OK) return err;
  digital_gain_reg_ = kTableDigitalGainReg;
  err = setStream(false);
  if (err != ESP_OK) return err;
  return setMirror(mirror);
}

esp_err_t Sensor::setStream(bool enable) {
  if (enable) {
    // Same order as the demo driver with its AF pad option enabled.
    esp_err_t err = write(kRegMipiCtrl00, kMipiCtrl00LineSync);
    if (err == ESP_OK) err = write(0x3002, 0x01);
    if (err == ESP_OK) err = write(0x3010, 0x01);
    if (err == ESP_OK) err = write(0x300d, 0x01);
    if (err == ESP_OK) err = write(kRegStreamMode, 0x01);
    return err;
  }
  // Stop: standby first so that a failed pad write can never leave the
  // sensor streaming. Every write is attempted; the first error is returned.
  esp_err_t first = write(kRegStreamMode, 0x00);
  const esp_err_t pad_writes[] = {
      write(kRegMipiCtrl00, kMipiCtrl00LineSync),
      write(0x3002, 0x00),
      write(0x3010, 0x00),
      write(0x300d, 0x00),
  };
  for (const esp_err_t err : pad_writes) {
    if (first == ESP_OK) first = err;
  }
  return first;
}

esp_err_t Sensor::setMirror(bool enable) {
  uint8_t value = 0;
  esp_err_t err = read(kRegMirror, &value);
  if (err != ESP_OK) return err;
  value = enable ? static_cast<uint8_t>(value | 0x02)
                 : static_cast<uint8_t>(value & ~0x02);
  return write(kRegMirror, value);
}

esp_err_t Sensor::setOrientation(bool mirror, bool flip) {
  const uint8_t x = mirror ? kWindowXMirrored : kWindowX;
  const uint8_t y = flip ? kWindowYFlipped : kWindowY;
  uint8_t format1 = 0;
  esp_err_t err = read(kRegFormat1, &format1);
  if (err != ESP_OK) return err;
  // Only the two flip bits change; the other table bits stay.
  const uint8_t wanted = static_cast<uint8_t>(
      (format1 & ~(kFormat1Flip | kFormat1Mirror)) | (flip ? kFormat1Flip : 0) |
      (mirror ? kFormat1Mirror : 0));
  err = write(kRegIspXWinLow, x);
  if (err == ESP_OK) err = write(kRegIspYWinLow, y);
  if (err == ESP_OK) err = write(kRegFormat1, wanted);
  if (err != ESP_OK) return err;
  uint8_t read_x = 0;
  uint8_t read_y = 0;
  uint8_t read_format1 = 0;
  err = read(kRegIspXWinLow, &read_x);
  if (err == ESP_OK) err = read(kRegIspYWinLow, &read_y);
  if (err == ESP_OK) err = read(kRegFormat1, &read_format1);
  if (err != ESP_OK) return err;
  if (read_x != x || read_y != y || read_format1 != wanted) return ESP_ERR_INVALID_RESPONSE;
  return ESP_OK;
}

esp_err_t Sensor::setExposure(uint16_t lines, uint16_t gain_x16) {
  if (lines < kMinExposureLines) lines = kMinExposureLines;
  if (lines > kMaxExposureLines) lines = kMaxExposureLines;
  if (gain_x16 < kMinGainX16) gain_x16 = kMinGainX16;
  if (gain_x16 > kMaxTotalGainX16) gain_x16 = kMaxTotalGainX16;
  const uint16_t analog_x16 = gain_x16 > kMaxGainX16 ? kMaxGainX16 : gain_x16;
  const uint16_t gain_reg = static_cast<uint16_t>(analog_x16 << 4);
  uint32_t digital_reg = kTableDigitalGainReg;
  if (gain_x16 > kMaxGainX16) {
    const uint32_t digital_x1024 =
        (static_cast<uint32_t>(gain_x16) * 1024u + kMaxGainX16 / 2) / kMaxGainX16;
    digital_reg = (digital_x1024 << 6) | (kTableDigitalGainReg & 0x3f);
  }
  // Frame length first, so the exposure never exceeds the current frame.
  const uint16_t frame_length = frameLengthFor(lines);
  esp_err_t err = write(kRegFrameLengthHigh, static_cast<uint8_t>(frame_length >> 8));
  if (err == ESP_OK) err = write(kRegFrameLengthLow, static_cast<uint8_t>(frame_length & 0xff));
  if (err == ESP_OK) err = write(kRegExposureHigh, static_cast<uint8_t>(lines >> 8));
  if (err == ESP_OK) err = write(kRegExposureLow, static_cast<uint8_t>(lines & 0xff));
  if (err == ESP_OK) err = write(kRegAnalogGainHigh, static_cast<uint8_t>(gain_reg >> 8));
  if (err == ESP_OK) err = write(kRegAnalogGainLow, static_cast<uint8_t>(gain_reg & 0xff));
  if (err == ESP_OK && digital_reg != digital_gain_reg_) {
    err = write(kRegDigitalGainHigh, static_cast<uint8_t>(digital_reg >> 16));
    if (err == ESP_OK) err = write(kRegDigitalGainMid, static_cast<uint8_t>(digital_reg >> 8));
    if (err == ESP_OK) err = write(kRegDigitalGainLow, static_cast<uint8_t>(digital_reg & 0xff));
    // Unknown after a failed write: the next call writes it again.
    digital_gain_reg_ = err == ESP_OK ? digital_reg : 0;
  }
  return err;
}

}  // namespace ov02c10

#endif  // defined(HOMETILES_CAMERA_SENSOR_OV02C10)
