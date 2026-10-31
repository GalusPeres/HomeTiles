/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for HomeTiles: the esp_cam_sensor SC2336 register access is
 * re-implemented on a board transport. esp_cam_sensor/esp_sccb_intf are not
 * part of the Arduino core.
 */

#include "src/video/local_camera/sensors/sc2336/sc2336_sensor.h"

#if defined(HOMETILES_CAMERA_SENSOR_SC2336)

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "src/video/local_camera/sensors/sc2336/sc2336_settings.h"

namespace sc2336 {
namespace {

constexpr uint16_t kRegSoftReset = 0x0103;
// The table opens with the software reset (0x0103 = 1); 5 ms as for the
// SC2336 of the same family.
constexpr uint32_t kSoftResetDelayMs = 5;

// Orientation (esp_cam_sensor sc2336_set_mirror/vflip): 0x3221 bits [2:1]
// mirror, [6:5] flip.
constexpr uint8_t kMirrorBits = 0x06;
constexpr uint8_t kFlipBits = 0x60;
// Output window start inside the 1312x728 array window of the table
// (0x3200..0x3207: x 308..1619, y 180..907): the table's 16/4. Upstream
// sc2336_set_mirror/vflip only write 0x3221 and report BGGR throughout, so
// the start stays at the table value in every orientation, as on the SC2336.
constexpr uint8_t kStartX = 0x10;
constexpr uint8_t kStartY = 0x04;
static_assert(kStartX + kFrameWidth <= 1312, "x window");
static_assert(kStartY + kFrameHeight <= 728, "y window");
// The turned 544x720 window on the 1280x720 window's centre: start x 384.
constexpr uint16_t kTurnedStartX = kStartX + (kFrameWidth - kTurnedFrameWidth) / 2;  // 384
static_assert(kTurnedStartX % 2 == 0, "the start keeps the table's phase");
static_assert(kTurnedStartX + kTurnedFrameWidth <= 1312, "turned x window");

// HomeTiles overrides, not vendor data (PROVENANCE.md): VTS written
// explicitly (the table sets 1250) because longer exposures stretch it. Default exposure and 1x gain as setExposure() writes them.
constexpr uint16_t kExposureReg = kDefaultExposureLines;
const sc2336_reginfo_t kOverrides[] = {
    {SC2336_REG_TOTAL_HEIGHT_H, static_cast<uint8_t>(kFrameLengthLines >> 8)},
    {SC2336_REG_TOTAL_HEIGHT_L, static_cast<uint8_t>(kFrameLengthLines & 0xff)},
    {SC2336_REG_SHUTTER_TIME_H, static_cast<uint8_t>((kExposureReg >> 12) & 0x0f)},
    {SC2336_REG_SHUTTER_TIME_M, static_cast<uint8_t>((kExposureReg >> 4) & 0xff)},
    {SC2336_REG_SHUTTER_TIME_L, static_cast<uint8_t>((kExposureReg & 0x0f) << 4)},
    {SC2336_REG_DIG_FINE_GAIN, 0x80},
    {SC2336_REG_DIG_COARSE_GAIN, 0x00},
    {SC2336_REG_ANG_GAIN, 0x00},
    {SC2336_REG_END, 0x00},
};

}  // namespace

esp_err_t Sensor::attach(Transport* transport) {
  if (!transport) return ESP_ERR_INVALID_STATE;
  transport_ = transport;
  return ESP_OK;
}

esp_err_t Sensor::detach() {
  transport_ = nullptr;
  return ESP_OK;
}

esp_err_t Sensor::write(uint16_t reg, uint8_t value) {
  return transport_ ? transport_->write(reg, value) : ESP_ERR_INVALID_STATE;
}

esp_err_t Sensor::read(uint16_t reg, uint8_t* value) {
  if (!value) return ESP_ERR_INVALID_ARG;
  return transport_ ? transport_->read(reg, value) : ESP_ERR_INVALID_STATE;
}

esp_err_t Sensor::probe(uint16_t* chip_id) {
  if (chip_id) *chip_id = 0;
  if (!transport_) return ESP_ERR_INVALID_STATE;
  esp_err_t err = transport_->probe();
  if (err != ESP_OK) return err;
  uint8_t high = 0;
  uint8_t low = 0;
  err = read(SC2336_REG_SENSOR_ID_H, &high);
  if (err == ESP_OK) err = read(SC2336_REG_SENSOR_ID_L, &low);
  if (err != ESP_OK) return err;
  const uint16_t id = static_cast<uint16_t>((high << 8) | low);
  if (chip_id) *chip_id = id;
  return id == kChipId ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t write_table(Sensor* sensor, const sc2336_reginfo_t* table,
                             esp_err_t (Sensor::*write_fn)(uint16_t, uint8_t)) {
  esp_err_t err = ESP_OK;
  for (size_t i = 0; err == ESP_OK && table[i].reg != SC2336_REG_END; ++i) {
    if (table[i].reg == SC2336_REG_DELAY) {
      vTaskDelay(pdMS_TO_TICKS(table[i].val));
      continue;
    }
    err = (sensor->*write_fn)(table[i].reg, table[i].val);
    // The table opens with the software reset; give the sensor its 5 ms.
    if (err == ESP_OK && table[i].reg == kRegSoftReset) {
      vTaskDelay(pdMS_TO_TICKS(kSoftResetDelayMs));
    }
    // Yield now and then so other tasks run during the long table.
    if ((i & 0x1f) == 0x1f) vTaskDelay(1);
  }
  return err;
}

esp_err_t Sensor::loadDefaultMode(bool mirror, bool turned) {
  esp_err_t err = write_table(this, sc2336_mipi_2lane_24Minput_1280x720_raw8_30fps, &Sensor::write);
  if (err != ESP_OK) return err;
  err = write_table(this, kOverrides, &Sensor::write);
  if (err != ESP_OK) return err;
  turned_ = turned;
  if (turned) {
    // The output width and the start's high byte; setOrientation() writes
    // the start's low byte.
    err = write(SC2336_REG_OUT_WIDTH_H, static_cast<uint8_t>(kTurnedFrameWidth >> 8));
    if (err == ESP_OK) err = write(SC2336_REG_OUT_WIDTH_L, static_cast<uint8_t>(kTurnedFrameWidth & 0xff));
    if (err == ESP_OK) err = write(SC2336_REG_OUT_START_PIXEL_H, static_cast<uint8_t>(kTurnedStartX >> 8));
    if (err != ESP_OK) return err;
  }
  err = setStream(false);
  if (err != ESP_OK) return err;
  // Orientation state 0 is the board mirror; the table reads out unmirrored.
  default_mirror_ = mirror;
  return setOrientation(false, false);
}

esp_err_t Sensor::setStream(bool enable) {
  return write(SC2336_REG_SLEEP_MODE, enable ? 0x01 : 0x00);
}

esp_err_t Sensor::setOrientation(bool mirror, bool flip) {
  const bool mirrored = default_mirror_ != mirror;
  uint8_t value = 0;
  esp_err_t err = read(SC2336_REG_FLIP_MIRROR, &value);
  if (err != ESP_OK) return err;
  const uint8_t wanted = static_cast<uint8_t>((value & ~(kMirrorBits | kFlipBits)) |
                                              (mirrored ? kMirrorBits : 0) |
                                              (flip ? kFlipBits : 0));
  const uint8_t x = turned_ ? static_cast<uint8_t>(kTurnedStartX & 0xff) : kStartX;
  const uint8_t y = kStartY;
  err = write(SC2336_REG_OUT_START_PIXEL_L, x);
  if (err == ESP_OK) err = write(SC2336_REG_OUT_START_LINE_L, y);
  if (err == ESP_OK) err = write(SC2336_REG_FLIP_MIRROR, wanted);
  if (err != ESP_OK) return err;
  uint8_t read_x = 0;
  uint8_t read_y = 0;
  uint8_t read_value = 0;
  err = read(SC2336_REG_OUT_START_PIXEL_L, &read_x);
  if (err == ESP_OK) err = read(SC2336_REG_OUT_START_LINE_L, &read_y);
  if (err == ESP_OK) err = read(SC2336_REG_FLIP_MIRROR, &read_value);
  if (err != ESP_OK) return err;
  if (read_x != x || read_y != y || read_value != wanted) return ESP_ERR_INVALID_RESPONSE;
  return ESP_OK;
}

esp_err_t Sensor::setExposure(uint16_t lines, uint16_t gain_x16) {
  if (lines < kMinExposureLines) lines = kMinExposureLines;
  if (lines > kMaxExposureLines) lines = kMaxExposureLines;
  if (gain_x16 < kMinGainX16) gain_x16 = kMinGainX16;
  if (gain_x16 > kMaxTotalGainX16) gain_x16 = kMaxTotalGainX16;
  // Frame length first, so the exposure never exceeds the current frame.
  const uint16_t frame_length = frameLengthFor(lines);
  const GainRegisters gain = gainRegistersFor(gain_x16);
  esp_err_t err = write(SC2336_REG_TOTAL_HEIGHT_H, static_cast<uint8_t>(frame_length >> 8));
  if (err == ESP_OK) err = write(SC2336_REG_TOTAL_HEIGHT_L, static_cast<uint8_t>(frame_length & 0xff));
  if (err == ESP_OK) err = write(SC2336_REG_SHUTTER_TIME_H, static_cast<uint8_t>((lines >> 12) & 0x0f));
  if (err == ESP_OK) err = write(SC2336_REG_SHUTTER_TIME_M, static_cast<uint8_t>((lines >> 4) & 0xff));
  if (err == ESP_OK) err = write(SC2336_REG_SHUTTER_TIME_L, static_cast<uint8_t>((lines & 0x0f) << 4));
  if (err == ESP_OK) err = write(SC2336_REG_ANG_GAIN, gain.analog);
  if (err == ESP_OK) err = write(SC2336_REG_DIG_COARSE_GAIN, 0x00);
  if (err == ESP_OK) err = write(SC2336_REG_DIG_FINE_GAIN, gain.digital_fine);
  return err;
}

}  // namespace sc2336

#endif  // defined(HOMETILES_CAMERA_SENSOR_SC2336)
