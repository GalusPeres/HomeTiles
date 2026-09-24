/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal OV5647 SCCB driver, shared by every board that selects
 * HOMETILES_CAMERA_SENSOR_OV5647 (see camera_driver.h). Register addresses and
 * sequences follow Espressif's esp_cam_sensor OV5647 driver; manual exposure
 * and gain follow the Linux ov5647 driver. See PROVENANCE.md.
 */

#pragma once

#include "src/video/local_camera/camera_select.h"

#if defined(HOMETILES_CAMERA_SENSOR_OV5647)

#include <stdint.h>

#include <driver/i2c_master.h>
#include <esp_err.h>

namespace ov5647 {

constexpr uint16_t kSccbAddress = 0x36;
constexpr uint32_t kSccbFrequencyHz = 100000;
constexpr uint16_t kChipId = 0x5647;

// 2-lane 24 MHz RAW10 mode: the vendored 1280x960 2x2-binning table plus the
// HomeTiles window override (PROVENANCE.md). The Waveshare LCD-X front camera
// sits a quarter turn from the landscape UI, so the sensor outputs a portrait
// window with all 960 binned rows and 544 centred columns; the Bridge turns
// the JPEG into 960x544. 30 fps instead of the table's 45
// (less CSI and PSRAM traffic).
constexpr uint32_t kFrameWidth = 544;
constexpr uint32_t kFrameHeight = 960;
constexpr uint32_t kDataLanes = 2;
// Table pixel clock 88333333 Hz, RAW10 over two lanes (esp_cam_sensor:
// OV5647_MIPI_CSI_LINE_RATE_1280x960_45FPS = 441.7 Mbit/s per lane).
constexpr uint32_t kLaneBitRateMbps = 441;
constexpr uint16_t kLineLengthPclk = 1796;          // HTS of the table.
constexpr uint16_t kFrameLengthLines = 1640;        // VTS for 30 fps (table: 1093).
constexpr uint16_t kFrameMs = 34;                   // 1640 * 1796 / 88.33 MHz = 33.3 ms.

constexpr const char* kName = "ov5647";
// Exposure in lines and analog gain, 16 == 1.0x (Linux ov5647.c: exposure
// 0x3500-0x3502 in 1/16 lines, gain 0x350a/0x350b). Up to the frame length
// minus the margin the frame rate stays at 30 fps; longer exposures stretch
// the frame (VTS = lines + margin) down to 7.5 fps at the maximum.
constexpr uint16_t kExposureMarginLines = 4;
constexpr uint16_t kDefaultExposureLines = kFrameLengthLines - kExposureMarginLines;
constexpr uint16_t kMinExposureLines = 4;
constexpr uint16_t kMaxExposureLines = 4 * kFrameLengthLines - kExposureMarginLines;
constexpr uint16_t kMinGainX16 = 16;
constexpr uint16_t kMaxGainX16 = 248;
constexpr uint16_t kMaxTotalGainX16 = kMaxGainX16;

// Frame length in lines for an exposure: the 30 fps VTS or longer.
constexpr uint16_t frameLengthFor(uint16_t lines) {
  return lines + kExposureMarginLines > kFrameLengthLines
             ? static_cast<uint16_t>(lines + kExposureMarginLines)
             : kFrameLengthLines;
}

class Sensor {
 public:
  // Adds the SCCB device to an existing bus. Never creates a bus.
  esp_err_t attach(i2c_master_bus_handle_t bus);
  // Removes the device from the bus; the bus itself stays untouched. On an
  // error the handle is kept so that a later detach() can retry.
  esp_err_t detach();
  bool attached() const { return device_ != nullptr; }

  // ACK probe followed by the chip-ID read. chip_id receives the raw value
  // even when it does not match kChipId.
  esp_err_t probe(uint16_t* chip_id);
  // Software reset, 1280x960 table, 544x960 window at 30 fps, manual
  // exposure, gain and white balance (the ISP pipeline owns them), stream off,
  // table-default mirror when mirror is set. The readout is then orientation
  // state 0.
  esp_err_t loadDefaultMode(bool mirror);
  // Sensor readout orientation relative to the table readout: mirror turns it
  // horizontally, flip vertically, both = 180 degrees. The ISP window offsets
  // move with the flips so the Bayer order stays the same in every state.
  // Works in standby and while streaming; the written registers are read back.
  esp_err_t setOrientation(bool mirror, bool flip);
  esp_err_t setStream(bool enable);
  // lines: exposure in lines; gain_x16: analog gain, 16 == 1.0x.
  esp_err_t setExposure(uint16_t lines, uint16_t gain_x16);

 private:
  esp_err_t write(uint16_t reg, uint8_t value);
  esp_err_t read(uint16_t reg, uint8_t* value);

  i2c_master_bus_handle_t bus_ = nullptr;
  i2c_master_dev_handle_t device_ = nullptr;
  // Board mirror from loadDefaultMode() (orientation state 0): setOrientation()
  // mirrors relative to it.
  bool default_mirror_ = true;
};

}  // namespace ov5647

#endif  // defined(HOMETILES_CAMERA_SENSOR_OV5647)
