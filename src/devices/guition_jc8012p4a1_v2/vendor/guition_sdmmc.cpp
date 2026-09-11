/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for HomeTiles from Espressif's ESP32-P4 Function EV Board SDMMC
 * setup, also distributed in Guition's official JC8012P4A1 examples. The
 * board uses SDMMC slot 0 IO-MUX pins: CLK 43, CMD 44, D0-D3 39-42.
 */

#include "guition_sdmmc.h"

#if defined(DEVICE_GUITION_JC8012P4A1_V2)

#include <Arduino.h>
#include <esp_err.h>
#include <esp_vfs_fat.h>
#include <sd_protocol_defs.h>
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#include <sdmmc_cmd.h>

namespace {

constexpr int kSdLdoChannel = 4;
sd_pwr_ctrl_handle_t g_sd_power_handle = nullptr;

bool ensure_sd_power() {
  if (g_sd_power_handle) {
    return true;
  }

  sd_pwr_ctrl_ldo_config_t config = {};
  config.ldo_chan_id = kSdLdoChannel;
  const esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&config, &g_sd_power_handle);
  if (err != ESP_OK) {
    Serial.printf("[Device/Guition JC8012P4A1 V2] SD power control on LDO VO%d failed: %s (0x%x)\n",
                  kSdLdoChannel,
                  esp_err_to_name(err),
                  static_cast<unsigned>(err));
    return false;
  }

  Serial.printf("[Device/Guition JC8012P4A1 V2] SD power control uses LDO VO%d\n",
                kSdLdoChannel);
  return true;
}

}  // namespace

namespace fs {

GuitionSDMMCFS::GuitionSDMMCFS(FSImplPtr impl)
    : FS(impl), card_(nullptr) {
}

bool GuitionSDMMCFS::begin(const char* mountpoint,
                          int sdmmc_frequency,
                          uint8_t max_open_files) {
  if (card_) {
    return true;
  }
  if (!ensure_sd_power()) {
    return false;
  }

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = max_open_files;
  mount_config.allocation_unit_size = 64 * 1024;
  mount_config.disk_status_check_enable = false;
  mount_config.use_one_fat = false;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = sdmmc_frequency;
  // Preserve DEINIT_ARG so failed mounts and unmounts pass the slot to cleanup.
  host.flags |= SDMMC_HOST_FLAG_4BIT;
  host.pwr_ctrl_handle = g_sd_power_handle;

  sdmmc_slot_config_t slot_config = {};
  slot_config.cd = SDMMC_SLOT_NO_CD;
  slot_config.wp = SDMMC_SLOT_NO_WP;
  slot_config.width = 4;
  // Slot 0 uses the fixed ESP32-P4 IO-MUX pins; explicit GPIO assignments
  // would make the driver reject the configuration.
  slot_config.flags = 0;

  const esp_err_t err = esp_vfs_fat_sdmmc_mount(
      mountpoint, &host, &slot_config, &mount_config, &card_);
  if (err != ESP_OK) {
    Serial.printf("[Device/Guition JC8012P4A1 V2] SD mount failed: %s (0x%x), freq=%d kHz\n",
                  esp_err_to_name(err),
                  static_cast<unsigned>(err),
                  sdmmc_frequency);
    card_ = nullptr;
    _impl->mountpoint(nullptr);
    return false;
  }

  _impl->mountpoint(mountpoint);
  return true;
}

void GuitionSDMMCFS::end() {
  if (!card_) {
    return;
  }

  esp_vfs_fat_sdcard_unmount(_impl->mountpoint(), card_);
  card_ = nullptr;
  _impl->mountpoint(nullptr);
}

GuitionSdCardType GuitionSDMMCFS::cardType() const {
  if (!card_) {
    return GUITION_CARD_NONE;
  }
  return (card_->ocr & SD_OCR_SDHC_CAP) ? GUITION_CARD_SDHC : GUITION_CARD_SD;
}

uint64_t GuitionSDMMCFS::cardSize() const {
  if (!card_) {
    return 0;
  }
  return static_cast<uint64_t>(card_->csd.capacity) * card_->csd.sector_size;
}

}  // namespace fs

fs::GuitionSDMMCFS GuitionSDMMC(FSImplPtr(new VFSImpl()));

#endif  // defined(DEVICE_GUITION_JC8012P4A1_V2)
