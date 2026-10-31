/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal SC2336 SCCB driver, shared by every board that selects
 * HOMETILES_CAMERA_SENSOR_SC2336 (see camera_driver.h). Register addresses,
 * the mode table and the gain/exposure encoding follow Espressif's
 * esp_cam_sensor SC2336 driver. Built like the SC202CS driver of the same
 * SmartSens family. See PROVENANCE.md.
 */

#pragma once

#include "src/video/local_camera/camera_select.h"

#if defined(HOMETILES_CAMERA_SENSOR_SC2336)

#include <stdint.h>

#include <esp_err.h>

namespace sc2336 {

constexpr uint16_t kSccbAddress = 0x30;
constexpr uint32_t kSccbFrequencyHz = 400000;
constexpr uint16_t kChipId = 0xcb3a;

// 2-lane 24 MHz RAW8 1280x720 mode (esp_cam_sensor
// MIPI_2lane_24Minput_RAW8_1280x720_30fps): 84 MHz pixel clock, HTS 2240,
// VTS 1250 = 30 fps, BGGR. The sensor outputs exactly the JPEG size.
constexpr uint32_t kFrameWidth = 1280;
constexpr uint32_t kFrameHeight = 720;
// The turned window: a centred 544x720 strip, as the SC202CS sends it.
constexpr uint32_t kTurnedFrameWidth = 544;
constexpr uint32_t kTurnedFrameHeight = 720;
constexpr uint32_t kDataLanes = 2;
constexpr uint8_t kRawBits = 8;
// esp_cam_sensor: mipi_clk 336000000 for this mode (lane rate in Mbps).
constexpr uint32_t kLaneBitRateMbps = 336;
constexpr uint16_t kLineLengthPclk = 2240;          // HTS of the mode.
constexpr uint16_t kFrameLengthLines = 1250;        // VTS for 30 fps.
constexpr uint16_t kFrameMs = 34;                   // 1250 * 2240 / 84 MHz = 33.3 ms.

constexpr const char* kName = "sc2336";
// Exposure in lines (the register holds 1/16 lines); upstream: the maximum
// exposure is VTS - 6. Longer exposures stretch the frame down to 7.5 fps.
constexpr uint16_t kExposureMarginLines = 6;
constexpr uint16_t kDefaultExposureLines = kFrameLengthLines - kExposureMarginLines;
constexpr uint16_t kMinExposureLines = 8;
constexpr uint16_t kMaxExposureLines = 4 * kFrameLengthLines - kExposureMarginLines;
// Gain, 16 == 1.0x: analog coarse steps 1/2/4/8/16x (0x3e09 codes from the
// upstream gain map), the rest in digital fine steps of 1/128 up to
// 0xfc/0x80 (0x3e07).
constexpr uint16_t kMinGainX16 = 16;
constexpr uint16_t kMaxGainX16 = 256;
constexpr uint16_t kMaxTotalGainX16 = kMaxGainX16 * 0xfc / 0x80;

constexpr uint16_t frameLengthFor(uint16_t lines) {
  return lines + kExposureMarginLines > kFrameLengthLines
             ? static_cast<uint16_t>(lines + kExposureMarginLines)
             : kFrameLengthLines;
}

struct GainRegisters {
  uint8_t analog;         // 0x3e09: 0x00, 0x08, 0x09, 0x0b, 0x0f = 1, 2, 4, 8, 16x.
  uint8_t digital_fine;   // 0x3e07: 0x80..0xfc = 1.0..1.97x.
};

// Analog coarse first (largest step not above the gain), the remainder as
// digital fine gain.
constexpr GainRegisters gainRegistersFor(uint16_t gain_x16) {
  constexpr uint8_t kAnalogCodes[] = {0x00, 0x08, 0x09, 0x0b, 0x0f};
  uint16_t analog_x16 = 16;
  uint8_t step = 0;
  while (analog_x16 < kMaxGainX16 && analog_x16 * 2 <= gain_x16) {
    analog_x16 = static_cast<uint16_t>(analog_x16 * 2);
    ++step;
  }
  uint32_t fine = (static_cast<uint32_t>(gain_x16) * 0x80 + analog_x16 / 2) / analog_x16;
  if (fine < 0x80) fine = 0x80;
  if (fine > 0xfc) fine = 0xfc;
  return GainRegisters{kAnalogCodes[step], static_cast<uint8_t>(fine)};
}

// Register access provided by the board: 16-bit register addresses, 8-bit
// values.
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
  // The turned window for the core; the picture turns back like the SC202CS
  // (hardware check pending).
  static constexpr bool kUprightTurnCounterClockwise = false;
  static constexpr uint32_t kTurnedWidth = kTurnedFrameWidth;
  static constexpr uint32_t kTurnedHeight = kTurnedFrameHeight;
  esp_err_t attach(Transport* transport);
  esp_err_t detach();
  bool attached() const { return transport_ != nullptr; }

  // ACK probe followed by the chip-ID read (0x3107/0x3108).
  esp_err_t probe(uint16_t* chip_id);
  // Mode table (it starts with the software reset), 30 fps VTS, default
  // exposure at 1x gain, stream off, board mirror; turned: the 544x720 window.
  esp_err_t loadDefaultMode(bool mirror, bool turned = false);
  // Readout orientation relative to state 0 (0x3221): mirror bits [2:1],
  // flip bits [6:5]. The output window stays at the table start.
  esp_err_t setOrientation(bool mirror, bool flip);
  esp_err_t setStream(bool enable);
  esp_err_t setExposure(uint16_t lines, uint16_t gain_x16);

 private:
  esp_err_t write(uint16_t reg, uint8_t value);
  esp_err_t read(uint16_t reg, uint8_t* value);

  Transport* transport_ = nullptr;
  bool default_mirror_ = false;
  bool turned_ = false;
};

}  // namespace sc2336

#endif  // defined(HOMETILES_CAMERA_SENSOR_SC2336)
