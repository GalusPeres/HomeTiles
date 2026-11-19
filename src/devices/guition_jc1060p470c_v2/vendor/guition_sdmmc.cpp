/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for HomeTiles from Espressif's ESP32-P4 Function EV Board SDMMC
 * setup, also distributed in Guition's JC1060P470C examples. The
 * board uses SDMMC slot 0 IO-MUX pins: CLK 43, CMD 44, D0-D3 39-42.
 */

#include "guition_sdmmc.h"

#if defined(DEVICE_GUITION_JC1060P470C_V2)

#include <Arduino.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <esp_err.h>
#include <esp_random.h>
#include <esp_vfs_fat.h>
#include <esp32-hal-periman.h>
#include <sd_protocol_defs.h>
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#include <sdmmc_cmd.h>

namespace {

constexpr int kSdLdoChannel = 4;
constexpr uint32_t kSdPowerOffMs = 200;
sd_pwr_ctrl_handle_t g_sd_power_handle = nullptr;

static_assert(BOARD_SDMMC_POWER_CHANNEL == kSdLdoChannel,
              "JC1060 SD power must use ESP32-P4 LDO VO4");
static_assert(BOARD_SDMMC_POWER_PIN == 45,
              "JC1060 SD card power switch must use GPIO45");
static_assert(BOARD_SDMMC_POWER_ON_LEVEL == LOW,
              "JC1060 SD card power switch must be active-low");

esp_err_t ensure_sd_power() {
  if (g_sd_power_handle) {
    return ESP_OK;
  }

  sd_pwr_ctrl_ldo_config_t config = {};
  config.ldo_chan_id = kSdLdoChannel;
  const esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&config, &g_sd_power_handle);
  if (err != ESP_OK) {
    Serial.printf("[Device/Guition JC1060P470C V2] SD power control on LDO VO%d failed: %s (0x%x)\n",
                  kSdLdoChannel,
                  esp_err_to_name(err),
                  static_cast<unsigned>(err));
    return err;
  }

  Serial.printf("[Device/Guition JC1060P470C V2] SD power control uses LDO VO%d\n",
                kSdLdoChannel);
  return ESP_OK;
}

void reset_sd_card_power() {
  // Match Arduino-ESP32's official ESP32-P4 SD_MMC.begin() sequence. The
  // JC1060 routes LDO VO4 through an active-low load switch controlled by
  // GPIO45, so an SDMMC host reset alone cannot recover a card left in a bad
  // electrical state.
  pinMode(BOARD_SDMMC_POWER_PIN, OUTPUT);
  digitalWrite(BOARD_SDMMC_POWER_PIN, !BOARD_SDMMC_POWER_ON_LEVEL);
  delay(kSdPowerOffMs);
  digitalWrite(BOARD_SDMMC_POWER_PIN, BOARD_SDMMC_POWER_ON_LEVEL);
  perimanSetPinBusExtraType(BOARD_SDMMC_POWER_PIN, "SDMMC_POWER");
  Serial.printf("[Device/Guition JC1060P470C V2] SD power reset: gpio=%d off_ms=%lu on_level=%s\n",
                BOARD_SDMMC_POWER_PIN,
                static_cast<unsigned long>(kSdPowerOffMs),
                BOARD_SDMMC_POWER_ON_LEVEL == LOW ? "LOW" : "HIGH");
}

}  // namespace

namespace fs {

JC1060SDMMCFS::JC1060SDMMCFS(FSImplPtr impl)
    : FS(impl),
      card_(nullptr),
      writable_(false),
      frequency_khz_(0),
      last_error_phase_("none"),
      last_error_(ESP_OK) {
}

bool JC1060SDMMCFS::begin(const char* mountpoint,
                          int sdmmc_frequency,
                          uint8_t max_open_files) {
  if (card_) {
    return true;
  }
  const esp_err_t power_err = ensure_sd_power();
  if (power_err != ESP_OK) {
    last_error_phase_ = "power-control";
    last_error_ = power_err;
    return false;
  }
  reset_sd_card_power();

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = max_open_files;
  mount_config.allocation_unit_size = 64 * 1024;
  mount_config.disk_status_check_enable = false;
  mount_config.use_one_fat = false;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = sdmmc_frequency;
  // Keep SDMMC_HOST_DEFAULT() capabilities intact. In particular,
  // SDMMC_HOST_FLAG_DEINIT_ARG tells FATFS to call deinit_p(slot). Dropping
  // that flag makes an unmount call the slot deinitializer with the wrong
  // function signature and can leave slot 0 in an undefined state before the
  // network-transition remount.
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
    last_error_phase_ = "mount";
    last_error_ = err;
    Serial.printf("[Device/Guition JC1060P470C V2] SD mount failed: %s (0x%x), freq=%d kHz\n",
                  esp_err_to_name(err),
                  static_cast<unsigned>(err),
                  sdmmc_frequency);
    card_ = nullptr;
    _impl->mountpoint(nullptr);
    return false;
  }

  _impl->mountpoint(mountpoint);
  writable_ = false;
  frequency_khz_ = card_ ? card_->real_freq_khz : sdmmc_frequency;
  last_error_phase_ = "none";
  last_error_ = ESP_OK;
  Serial.printf("[Device/Guition JC1060P470C V2] SD mounted, requested=%d kHz, real=%d kHz, host_flags=0x%lx\n",
                sdmmc_frequency,
                frequency_khz_,
                static_cast<unsigned long>(host.flags));
  return true;
}

void JC1060SDMMCFS::end() {
  if (!card_) {
    writable_ = false;
    frequency_khz_ = 0;
    return;
  }

  const esp_err_t err = esp_vfs_fat_sdcard_unmount(_impl->mountpoint(), card_);
  if (err != ESP_OK) {
    Serial.printf("[Device/Guition JC1060P470C V2] SD unmount failed: %s (0x%x)\n",
                  esp_err_to_name(err),
                  static_cast<unsigned>(err));
  }
  card_ = nullptr;
  writable_ = false;
  frequency_khz_ = 0;
  _impl->mountpoint(nullptr);
}

bool JC1060SDMMCFS::testWritable() {
  writable_ = false;
  if (!card_ || !_impl->mountpoint()) {
    Serial.println("[Device/Guition JC1060P470C V2] SD write test skipped: card is not mounted");
    return false;
  }

  char directory[48] = {};
  char file_path[72] = {};
  const unsigned long nonce = static_cast<unsigned long>(esp_random());
  snprintf(directory, sizeof(directory), "/.hometiles-rw-%08lx", nonce);
  snprintf(file_path, sizeof(file_path), "%s/probe.bin", directory);

  static constexpr uint8_t kProbeData[] = {
      0x48, 0x6f, 0x6d, 0x65, 0x54, 0x69, 0x6c, 0x65,
      0x73, 0x2d, 0x53, 0x44, 0x2d, 0x52, 0x57, 0x0a,
  };

  auto log_failure = [&](const char* phase, int error_number) {
    const esp_err_t status = sdmmc_get_status(card_);
    Serial.printf("[Device/Guition JC1060P470C V2] SD write test failed: phase=%s freq=%d kHz errno=%d (%s) card_status=%s (0x%x)\n",
                  phase,
                  frequency_khz_,
                  error_number,
                  error_number ? strerror(error_number) : "none",
                  esp_err_to_name(status),
                  static_cast<unsigned>(status));
  };

  errno = 0;
  if (!mkdir(directory)) {
    const int saved_errno = errno;
    log_failure("mkdir", saved_errno);
    return false;
  }

  errno = 0;
  File output = open(file_path, FILE_WRITE);
  if (!output) {
    const int saved_errno = errno;
    rmdir(directory);
    log_failure("open-write", saved_errno);
    return false;
  }

  errno = 0;
  const size_t written = output.write(kProbeData, sizeof(kProbeData));
  output.flush();
  output.close();
  if (written != sizeof(kProbeData)) {
    const int saved_errno = errno;
    remove(file_path);
    rmdir(directory);
    log_failure("write", saved_errno);
    return false;
  }

  errno = 0;
  File input = open(file_path, FILE_READ);
  if (!input) {
    const int saved_errno = errno;
    remove(file_path);
    rmdir(directory);
    log_failure("open-readback", saved_errno);
    return false;
  }

  uint8_t readback[sizeof(kProbeData)] = {};
  const size_t read_count = input.read(readback, sizeof(readback));
  input.close();
  if (read_count != sizeof(readback) || memcmp(readback, kProbeData, sizeof(readback)) != 0) {
    const int saved_errno = errno;
    remove(file_path);
    rmdir(directory);
    log_failure("verify", saved_errno);
    return false;
  }

  errno = 0;
  const bool file_removed = remove(file_path);
  const int remove_errno = errno;
  errno = 0;
  const bool directory_removed = rmdir(directory);
  const int rmdir_errno = errno;
  if (!file_removed || !directory_removed) {
    log_failure(!file_removed ? "remove" : "rmdir",
                !file_removed ? remove_errno : rmdir_errno);
    return false;
  }

  writable_ = true;
  Serial.printf("[Device/Guition JC1060P470C V2] SD write test OK, freq=%d kHz, bytes=%u\n",
                frequency_khz_,
                static_cast<unsigned>(sizeof(kProbeData)));
  return true;
}

bool JC1060SDMMCFS::writable() const {
  return card_ && writable_;
}

int JC1060SDMMCFS::frequencyKHz() const {
  return frequency_khz_;
}

const char* JC1060SDMMCFS::lastErrorPhase() const {
  return last_error_phase_;
}

esp_err_t JC1060SDMMCFS::lastError() const {
  return last_error_;
}

JC1060SdCardType JC1060SDMMCFS::cardType() const {
  if (!card_) {
    return JC1060_CARD_NONE;
  }
  return (card_->ocr & SD_OCR_SDHC_CAP) ? JC1060_CARD_SDHC : JC1060_CARD_SD;
}

uint64_t JC1060SDMMCFS::cardSize() const {
  if (!card_) {
    return 0;
  }
  return static_cast<uint64_t>(card_->csd.capacity) * card_->csd.sector_size;
}

}  // namespace fs

fs::JC1060SDMMCFS JC1060SDMMC(FSImplPtr(new VFSImpl()));

#endif  // defined(DEVICE_GUITION_JC1060P470C_V2)
