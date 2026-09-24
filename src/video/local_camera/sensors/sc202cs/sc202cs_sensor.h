/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal SC202CS (sold as SC2356) SCCB driver, shared by every board that
 * selects HOMETILES_CAMERA_SENSOR_SC202CS (see camera_driver.h). Register
 * addresses, the mode table and the gain/exposure encoding follow Espressif's
 * esp_cam_sensor SC202CS driver. See PROVENANCE.md.
 */

#pragma once

#include "src/video/local_camera/camera_select.h"

#if defined(HOMETILES_CAMERA_SENSOR_SC202CS)

#include <stdint.h>

#include <esp_err.h>

namespace sc202cs {

constexpr uint16_t kSccbAddress = 0x36;
constexpr uint32_t kSccbFrequencyHz = 400000;
constexpr uint16_t kChipId = 0xeb52;

// 1-lane 24 MHz RAW8 1280x720 mode: the vendored table (the M5Stack Tab5
// demo's default), 72 MHz pixel clock, HTS 1920, VTS 1250 = 30 fps. The
// sensor outputs exactly the JPEG size, so no crop pass runs.
constexpr uint32_t kFrameWidth = 1280;
constexpr uint32_t kFrameHeight = 720;
constexpr uint32_t kDataLanes = 1;
constexpr uint8_t kRawBits = 8;
// esp_cam_sensor: mipi_clk 576000000 for the RAW8 modes (lane rate in Mbps).
constexpr uint32_t kLaneBitRateMbps = 576;
constexpr uint16_t kLineLengthPclk = 1920;          // HTS of the mode.
constexpr uint16_t kFrameLengthLines = 1250;        // VTS for 30 fps.
constexpr uint16_t kFrameMs = 34;                   // 1250 * 1920 / 72 MHz = 33.3 ms.

constexpr const char* kName = "sc202cs";
// Exposure in lines (the register holds 1/16 lines); up to VTS - 6 the frame
// rate stays at 30 fps, longer exposures stretch the frame down to 7.5 fps.
constexpr uint16_t kExposureMarginLines = 6;
constexpr uint16_t kDefaultExposureLines = kFrameLengthLines - kExposureMarginLines;
constexpr uint16_t kMinExposureLines = 8;
constexpr uint16_t kMaxExposureLines = 4 * kFrameLengthLines - kExposureMarginLines;
// Gain, 16 == 1.0x: analog coarse steps 1/2/4/8/16x (0x3e09), the rest in
// digital fine steps of 1/128 up to 0xfc/0x80 (0x3e07).
constexpr uint16_t kMinGainX16 = 16;
constexpr uint16_t kMaxGainX16 = 256;
constexpr uint16_t kMaxTotalGainX16 = kMaxGainX16 * 0xfc / 0x80;

// Frame length in lines for an exposure: the 30 fps VTS or longer.
constexpr uint16_t frameLengthFor(uint16_t lines) {
  return lines + kExposureMarginLines > kFrameLengthLines
             ? static_cast<uint16_t>(lines + kExposureMarginLines)
             : kFrameLengthLines;
}

struct GainRegisters {
  uint8_t analog;         // 0x3e09: 0x00, 0x01, 0x03, 0x07, 0x0f = 1, 2, 4, 8, 16x.
  uint8_t digital_fine;   // 0x3e07: 0x80..0xfc = 1.0..1.97x.
};

// Analog coarse first (largest step not above the gain), the remainder as
// digital fine gain.
constexpr GainRegisters gainRegistersFor(uint16_t gain_x16) {
  uint16_t analog_x16 = 16;
  uint8_t analog = 0x00;
  while (analog_x16 < kMaxGainX16 && analog_x16 * 2 <= gain_x16) {
    analog_x16 = static_cast<uint16_t>(analog_x16 * 2);
    analog = static_cast<uint8_t>((analog << 1) | 1);
  }
  uint32_t fine = (static_cast<uint32_t>(gain_x16) * 0x80 + analog_x16 / 2) / analog_x16;
  if (fine < 0x80) fine = 0x80;
  if (fine > 0xfc) fine = 0xfc;
  return GainRegisters{analog, static_cast<uint8_t>(fine)};
}

// Register access provided by the board: the SCCB bus may be owned by
// another driver (M5Unified on the Tab5), so the sensor never talks to an
// I2C driver itself. 16-bit register addresses, 8-bit values.
class Transport {
 public:
  virtual esp_err_t probe() = 0;
  virtual esp_err_t write(uint16_t reg, uint8_t value) = 0;
  virtual esp_err_t read(uint16_t reg, uint8_t* value) = 0;

 protected:
  ~Transport() = default;
};

class Sensor {
 public:
  // Uses the board transport. Never creates or releases a bus.
  esp_err_t attach(Transport* transport);
  esp_err_t detach();
  bool attached() const { return transport_ != nullptr; }

  // ACK probe followed by the chip-ID read (0x3107/0x3108). chip_id receives
  // the raw value even when it does not match kChipId.
  esp_err_t probe(uint16_t* chip_id);
  // Mode table (it starts with the software reset), 30 fps VTS, default
  // exposure at 1x gain, stream off, board mirror. The readout is then
  // orientation state 0.
  esp_err_t loadDefaultMode(bool mirror);
  // Readout orientation relative to state 0: mirror horizontally, flip
  // vertically, both = 180 degrees (0x3221). The sensor keeps the Bayer
  // order itself; the output window stays at the table start. Read back.
  esp_err_t setOrientation(bool mirror, bool flip);
  esp_err_t setStream(bool enable);
  // lines: exposure in lines; gain_x16: total gain, 16 == 1.0x.
  esp_err_t setExposure(uint16_t lines, uint16_t gain_x16);

 private:
  esp_err_t write(uint16_t reg, uint8_t value);
  esp_err_t read(uint16_t reg, uint8_t* value);

  Transport* transport_ = nullptr;
  bool default_mirror_ = false;
};

}  // namespace sc202cs

#endif  // defined(HOMETILES_CAMERA_SENSOR_SC202CS)
