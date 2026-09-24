/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal OV02C10 SCCB driver, shared by every board that selects
 * HOMETILES_CAMERA_SENSOR_OV02C10 (see camera_driver.h).
 * Register addresses and sequences follow Espressif's esp_cam_sensor OV02C10
 * driver as shipped in Guition's demo; see PROVENANCE.md.
 */

#pragma once

#include "src/video/local_camera/camera_select.h"

#if defined(HOMETILES_CAMERA_SENSOR_OV02C10)

#include <stdint.h>

#include <driver/i2c_master.h>
#include <esp_err.h>

namespace ov02c10 {

constexpr uint16_t kSccbAddress = 0x36;
constexpr uint32_t kSccbFrequencyHz = 100000;
constexpr uint16_t kChipId = 0x5602;

// 1-lane 24 MHz RAW10 mode: the vendored 1288x728 table plus the HomeTiles
// window override (see PROVENANCE.md). The sensor outputs exactly the JPEG
// size, so no crop pass runs; the array window stays 1296x732.
constexpr uint32_t kFrameWidth = 1280;
constexpr uint32_t kFrameHeight = 720;
constexpr uint32_t kArrayWindowWidth = 1296;
constexpr uint32_t kArrayWindowHeight = 732;
constexpr uint32_t kDataLanes = 1;
constexpr uint32_t kLaneBitRateMbps = 400;
constexpr uint16_t kFrameLengthLines = 1164;       // VTS of the table.
constexpr uint16_t kTableExposureLines = 0x046c;   // 0x3501/0x3502 of the table.
constexpr uint16_t kTableAnalogGainReg = 0x0800;   // 0x3508/0x3509 of the table.

constexpr const char* kName = "ov02c10";
// Exposure range and analog gain range, 16 == 1.0x. Up to the table value
// (1132 lines, 30 fps) the frame length stays at the table VTS; longer
// exposures stretch the frame (VTS = lines + kExposureMarginLines), which
// lowers the frame rate down to 7.5 fps at the maximum.
constexpr uint16_t kExposureMarginLines = kFrameLengthLines - kTableExposureLines;
constexpr uint16_t kMinExposureLines = 4;
constexpr uint16_t kMaxExposureLines = 4 * kFrameLengthLines - kExposureMarginLines;
constexpr uint16_t kMinGainX16 = 16;
constexpr uint16_t kMaxGainX16 = 248;
// Sensor digital gain on top of the analog maximum (Linux ov02c10.c:
// 0x350a-0x350c = gain << 6, 0x400 = 1x, up to 0x3fff). It works on the
// 10-bit data before the ISP, so it bands less than a gain after it.
constexpr uint16_t kMaxDigitalGainX1024 = 4 * 1024;
constexpr uint16_t kMaxTotalGainX16 =
    static_cast<uint16_t>(kMaxGainX16 * kMaxDigitalGainX1024 / 1024);

// Frame length in lines for an exposure: the table VTS or longer.
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
  // Software reset, 1288x728 table, 1280x720 window, stream off,
  // demo-default mirror. The readout is then orientation state 0.
  esp_err_t loadDefaultMode(bool mirror);
  // Sensor readout orientation relative to the table readout: mirror turns it
  // horizontally, flip vertically, both = 180 degrees. The ISP window offsets
  // move with the flips so the Bayer order stays the same in every state.
  // Works in standby and while streaming (the next frames change); the
  // written registers are read back.
  esp_err_t setOrientation(bool mirror, bool flip);
  esp_err_t setStream(bool enable);
  // lines: exposure in lines; gain_x16: total gain, 16 == 1.0x. Up to the
  // analog maximum it is analog gain, above it sensor digital gain.
  esp_err_t setExposure(uint16_t lines, uint16_t gain_x16);

 private:
  esp_err_t write(uint16_t reg, uint8_t value);
  esp_err_t read(uint16_t reg, uint8_t* value);
  esp_err_t setMirror(bool enable);

  i2c_master_bus_handle_t bus_ = nullptr;
  i2c_master_dev_handle_t device_ = nullptr;
  // Last digital gain register value (24 bit); the mode table's value after
  // loadDefaultMode(), so an unchanged gain costs no SCCB write.
  uint32_t digital_gain_reg_ = 0;
};

}  // namespace ov02c10

#endif  // defined(HOMETILES_CAMERA_SENSOR_OV02C10)
