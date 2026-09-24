/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for HomeTiles: the esp_cam_sensor OV5647 register access is
 * re-implemented on top of an i2c_master device that shares the touch bus.
 * esp_cam_sensor/esp_sccb_intf are not part of the Arduino core.
 */

#include "src/video/local_camera/sensors/ov5647/ov5647_sensor.h"

#if defined(HOMETILES_CAMERA_SENSOR_OV5647)

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "src/video/local_camera/sensors/ov5647/ov5647_settings.h"

namespace ov5647 {
namespace {

constexpr int kTransferTimeoutMs = 50;
constexpr uint8_t kRemoveAttempts = 5;
constexpr uint32_t kRemoveRetryDelayMs = 2;
constexpr uint16_t kRegSensorIdHigh = 0x300a;
constexpr uint16_t kRegSensorIdLow = 0x300b;
constexpr uint16_t kRegStreamMode = 0x0100;
constexpr uint16_t kRegMipiCtrl00 = 0x4800;
// Line-sync packets on and bus idle, as written by the esp_cam_sensor driver
// with CONFIG_CAMERA_OV5647_CSI_LINESYNC_ENABLE (its default).
constexpr uint8_t kMipiCtrl00LineSync = 0x14;

// Manual exposure, gain and frame length (Linux ov5647.c): 0x3503 bit0 = AEC
// manual, bit1 = AGC manual; exposure 0x3500[3:0]/0x3501/0x3502 in 1/16
// lines; analog gain 0x350a[1:0]/0x350b in 1/16 steps; VTS 0x380e/0x380f.
constexpr uint16_t kRegAecAgc = 0x3503;
constexpr uint8_t kAecAgcManual = 0x03;
constexpr uint16_t kRegExposureHigh = 0x3500;
constexpr uint16_t kRegExposureMid = 0x3501;
constexpr uint16_t kRegExposureLow = 0x3502;
constexpr uint16_t kRegGainHigh = 0x350a;
constexpr uint16_t kRegGainLow = 0x350b;
constexpr uint16_t kRegFrameLengthHigh = 0x380e;
constexpr uint16_t kRegFrameLengthLow = 0x380f;

// Orientation (esp_cam_sensor ov5647_set_mirror/ov5647_set_vflip, Linux/RPi
// ov5647 TIMING_TC_H/V): 0x3821 bit1 = horizontal mirror, 0x3820 bit1 =
// vertical flip; bit0 of both is the table's binning and stays. The table
// reads out mirrored (0x3821 = 0x03) and not flipped (0x3820 = 0x01).
constexpr uint16_t kRegTimingV = 0x3820;
constexpr uint16_t kRegTimingH = 0x3821;
constexpr uint8_t kTimingFlipBit = 0x02;
// ISP window offsets inside the binned array window (table: x 4, y 2). The x
// offset centres the 544 columns (0x3810 holds its high bits). A flip moves
// the window by one pixel so the Bayer phase (GBRG) stays the same, as on the
// OV02C10.
constexpr uint16_t kRegIspXOffsetHigh = 0x3810;
constexpr uint16_t kRegIspXOffset = 0x3811;
constexpr uint16_t kRegIspYOffset = 0x3813;
// Binned input of the table: x 24..2599 -> 1288 columns, y 12..1943 -> 966 rows.
constexpr uint32_t kBinnedWidth = 1288;
constexpr uint32_t kBinnedHeight = 966;
constexpr uint16_t kOffsetXCentred = (kBinnedWidth - kFrameWidth) / 2;  // 372, even like 4
constexpr uint8_t kOffsetXHigh = static_cast<uint8_t>(kOffsetXCentred >> 8);
constexpr uint8_t kOffsetX = static_cast<uint8_t>(kOffsetXCentred & 0xff);
constexpr uint8_t kOffsetXMirrored = static_cast<uint8_t>((kOffsetXCentred + 1) & 0xff);
constexpr uint8_t kOffsetY = 0x02;
constexpr uint8_t kOffsetYFlipped = 0x03;
static_assert(kOffsetXCentred % 2 == 0 && (kOffsetXCentred + 1) >> 8 == kOffsetXHigh,
              "only the low offset byte changes with the mirror");
static_assert(kOffsetXCentred + 1 + kFrameWidth <= kBinnedWidth, "x window");
static_assert(kOffsetYFlipped + kFrameHeight <= kBinnedHeight, "y window");
static_assert((kOffsetX & 1) != (kOffsetXMirrored & 1), "a mirror moves the x phase");
static_assert((kOffsetY & 1) != (kOffsetYFlipped & 1), "a flip moves the y phase");

// Manual white balance: sensor AWB off (0x3406 bit0), R/G/B gains 1x
// (0x0400); the ISP pipeline balances the colours like on the OV02C10.
constexpr uint16_t kRegAwbManual = 0x3406;

// HomeTiles overrides, not vendor data (PROVENANCE.md): 544x960 portrait
// output inside the table's array window (all binned rows, centred columns
// through the ISP x offset), VTS 1640 = 30 fps, manual AEC/AGC/AWB with a
// full-frame default exposure at 1x gain.
constexpr uint16_t kExposureReg = static_cast<uint16_t>(kDefaultExposureLines);
const ov5647_reginfo_t kWindow544x960[] = {
    {0x3808, 0x02}, {0x3809, 0x20},  // x output size 544
    {0x380a, 0x03}, {0x380b, 0xc0},  // y output size 960
    {kRegFrameLengthHigh, static_cast<uint8_t>(kFrameLengthLines >> 8)},
    {kRegFrameLengthLow, static_cast<uint8_t>(kFrameLengthLines & 0xff)},
    {kRegIspXOffsetHigh, kOffsetXHigh},
    {kRegIspXOffset, kOffsetX},
    {kRegIspYOffset, kOffsetY},
    {kRegAecAgc, kAecAgcManual},
    {kRegExposureHigh, static_cast<uint8_t>((kExposureReg >> 12) & 0x0f)},
    {kRegExposureMid, static_cast<uint8_t>((kExposureReg >> 4) & 0xff)},
    {kRegExposureLow, static_cast<uint8_t>((kExposureReg << 4) & 0xf0)},
    {kRegGainHigh, 0x00},
    {kRegGainLow, static_cast<uint8_t>(kMinGainX16)},
    {kRegAwbManual, 0x01},
    {0x3400, 0x04}, {0x3401, 0x00},  // R gain 1x
    {0x3402, 0x04}, {0x3403, 0x00},  // G gain 1x
    {0x3404, 0x04}, {0x3405, 0x00},  // B gain 1x
    {OV5647_REG_END, 0x00},
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

static esp_err_t write_table(Sensor* sensor, const ov5647_reginfo_t* table,
                             esp_err_t (Sensor::*write_fn)(uint16_t, uint8_t)) {
  esp_err_t err = ESP_OK;
  for (size_t i = 0; err == ESP_OK && table[i].reg != OV5647_REG_END; ++i) {
    if (table[i].reg == OV5647_REG_DELAY) {
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
  esp_err_t err = write_table(this, ov5647_mipi_reset_regs, &Sensor::write);
  if (err != ESP_OK) return err;
  err = write_table(this, ov5647_mipi_2lane_24Minput_1280x960_raw10_45fps, &Sensor::write);
  if (err != ESP_OK) return err;
  err = write_table(this, kWindow544x960, &Sensor::write);
  if (err != ESP_OK) return err;
  err = setStream(false);
  if (err != ESP_OK) return err;
  // Orientation state 0 is the board mirror; the table itself reads out
  // mirrored, a board without mirror clears it.
  default_mirror_ = mirror;
  return setOrientation(false, false);
}

esp_err_t Sensor::setStream(bool enable) {
  if (enable) {
    // Same order as the esp_cam_sensor driver without the AF pad option.
    esp_err_t err = write(kRegMipiCtrl00, kMipiCtrl00LineSync);
    if (err == ESP_OK) err = write(kRegStreamMode, 0x01);
    return err;
  }
  // Stop: standby first so that a failed MIPI write can never leave the
  // sensor streaming. Both writes are attempted; the first error is returned.
  esp_err_t first = write(kRegStreamMode, 0x00);
  const esp_err_t mipi = write(kRegMipiCtrl00, kMipiCtrl00LineSync);
  if (first == ESP_OK) first = mipi;
  return first;
}

esp_err_t Sensor::setOrientation(bool mirror, bool flip) {
  // mirror is relative to orientation state 0 (the board mirror). The x
  // offset follows the hardware mirror bit relative to the table readout,
  // which is mirrored with an even offset; the table is not flipped (offset 2).
  const bool mirrored = default_mirror_ != mirror;
  const uint8_t x = mirrored ? kOffsetX : kOffsetXMirrored;
  const uint8_t y = flip ? kOffsetYFlipped : kOffsetY;
  uint8_t timing_h = 0;
  uint8_t timing_v = 0;
  esp_err_t err = read(kRegTimingH, &timing_h);
  if (err == ESP_OK) err = read(kRegTimingV, &timing_v);
  if (err != ESP_OK) return err;
  // Only the flip bits change; the binning bits stay.
  const uint8_t wanted_h = static_cast<uint8_t>(
      (timing_h & ~kTimingFlipBit) | (mirrored ? kTimingFlipBit : 0));
  const uint8_t wanted_v = static_cast<uint8_t>(
      (timing_v & ~kTimingFlipBit) | (flip ? kTimingFlipBit : 0));
  err = write(kRegIspXOffset, x);
  if (err == ESP_OK) err = write(kRegIspYOffset, y);
  if (err == ESP_OK) err = write(kRegTimingH, wanted_h);
  if (err == ESP_OK) err = write(kRegTimingV, wanted_v);
  if (err != ESP_OK) return err;
  uint8_t read_x = 0;
  uint8_t read_y = 0;
  uint8_t read_h = 0;
  uint8_t read_v = 0;
  err = read(kRegIspXOffset, &read_x);
  if (err == ESP_OK) err = read(kRegIspYOffset, &read_y);
  if (err == ESP_OK) err = read(kRegTimingH, &read_h);
  if (err == ESP_OK) err = read(kRegTimingV, &read_v);
  if (err != ESP_OK) return err;
  if (read_x != x || read_y != y || read_h != wanted_h || read_v != wanted_v) {
    return ESP_ERR_INVALID_RESPONSE;
  }
  return ESP_OK;
}

esp_err_t Sensor::setExposure(uint16_t lines, uint16_t gain_x16) {
  if (lines < kMinExposureLines) lines = kMinExposureLines;
  if (lines > kMaxExposureLines) lines = kMaxExposureLines;
  if (gain_x16 < kMinGainX16) gain_x16 = kMinGainX16;
  if (gain_x16 > kMaxGainX16) gain_x16 = kMaxGainX16;
  // Frame length first, so the exposure never exceeds the current frame.
  const uint16_t frame_length = frameLengthFor(lines);
  const uint32_t exposure = static_cast<uint32_t>(lines) << 4;
  esp_err_t err = write(kRegFrameLengthHigh, static_cast<uint8_t>(frame_length >> 8));
  if (err == ESP_OK) err = write(kRegFrameLengthLow, static_cast<uint8_t>(frame_length & 0xff));
  if (err == ESP_OK) err = write(kRegExposureHigh, static_cast<uint8_t>((exposure >> 16) & 0x0f));
  if (err == ESP_OK) err = write(kRegExposureMid, static_cast<uint8_t>((exposure >> 8) & 0xff));
  if (err == ESP_OK) err = write(kRegExposureLow, static_cast<uint8_t>(exposure & 0xf0));
  if (err == ESP_OK) err = write(kRegGainHigh, static_cast<uint8_t>((gain_x16 >> 8) & 0x03));
  if (err == ESP_OK) err = write(kRegGainLow, static_cast<uint8_t>(gain_x16 & 0xff));
  return err;
}

}  // namespace ov5647

#endif  // defined(HOMETILES_CAMERA_SENSOR_OV5647)
