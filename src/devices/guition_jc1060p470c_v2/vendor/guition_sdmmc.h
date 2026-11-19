/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "src/devices/device_select.h"

#if defined(DEVICE_GUITION_JC1060P470C_V2)

#include <FS.h>
#include <driver/sdmmc_host.h>
#include <driver/sdmmc_types.h>
#include <esp_err.h>
#include <vfs_api.h>

enum JC1060SdCardType : uint8_t {
  JC1060_CARD_NONE,
  JC1060_CARD_SD,
  JC1060_CARD_SDHC,
};

namespace fs {

class JC1060SDMMCFS : public FS {
 public:
  explicit JC1060SDMMCFS(FSImplPtr impl);

  bool begin(const char* mountpoint = "/sdcard",
             int sdmmc_frequency = SDMMC_FREQ_HIGHSPEED,
             uint8_t max_open_files = 5);
  void end();

  // FATFS can mount and enumerate a card even when data/metadata writes fail
  // at the negotiated bus speed. Verify both operations before exposing the
  // card as writable to HomeTiles.
  bool testWritable();
  bool writable() const;
  int frequencyKHz() const;
  const char* lastErrorPhase() const;
  esp_err_t lastError() const;

  JC1060SdCardType cardType() const;
  uint64_t cardSize() const;

 private:
  sdmmc_card_t* card_;
  bool writable_;
  int frequency_khz_;
  const char* last_error_phase_;
  esp_err_t last_error_;
};

}  // namespace fs

extern fs::JC1060SDMMCFS JC1060SDMMC;

#endif  // defined(DEVICE_GUITION_JC1060P470C_V2)
