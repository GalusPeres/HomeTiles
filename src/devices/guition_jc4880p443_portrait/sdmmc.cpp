#include "src/devices/guition_jc4880p443_portrait/sdmmc.h"

#if defined(DEVICE_GUITION_JC4880P443_PORTRAIT)

#include <Arduino.h>
#include <esp_err.h>
#include <esp_ldo_regulator.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <sd_protocol_defs.h>

namespace {

static constexpr int kSdLdoChannel = 4;
static constexpr int kSdLdoVoltageMv = 3300;
static esp_ldo_channel_handle_t g_sd_ldo_handle = nullptr;

static void ensure_sd_ldo_power() {
  if (g_sd_ldo_handle != nullptr) {
    return;
  }

  esp_ldo_channel_config_t ldo_cfg = {};
  ldo_cfg.chan_id = kSdLdoChannel;
  ldo_cfg.voltage_mv = kSdLdoVoltageMv;

  esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &g_sd_ldo_handle);
  if (err == ESP_OK) {
    Serial.printf("[SD] LDO VO%d set to %d mV\n", kSdLdoChannel, kSdLdoVoltageMv);
  } else {
    Serial.printf("[SD] WARN: LDO VO%d acquire failed: %s (0x%x)\n",
                  kSdLdoChannel,
                  esp_err_to_name(err),
                  static_cast<unsigned>(err));
  }
}

}  // namespace

namespace fs {

GuitionPortraitSDMMCFS::GuitionPortraitSDMMCFS(FSImplPtr impl)
    : FS(impl), _card(nullptr) {
}

bool GuitionPortraitSDMMCFS::begin(const char* mountpoint,
                             bool format_if_mount_failed,
                             int sdmmc_frequency,
                             uint8_t maxOpenFiles) {
  if (_card != nullptr) {
    return true;
  }

  ensure_sd_ldo_power();

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = format_if_mount_failed;
  mount_config.max_files = maxOpenFiles;
  mount_config.allocation_unit_size = 64 * 1024;
  mount_config.disk_status_check_enable = false;
  mount_config.use_one_fat = false;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = sdmmc_frequency;
  host.flags = SDMMC_HOST_FLAG_4BIT;

  sdmmc_slot_config_t slot_config = {};
  slot_config.cd = SDMMC_SLOT_NO_CD;
  slot_config.wp = SDMMC_SLOT_NO_WP;
  slot_config.width = 4;
  // The Guition JC4880P443 uses the P4 slot-0 IO-MUX pins (43/44/39/40/41/42).
  // TF_VCC is powered from on-chip LDO channel 4 (see ensure_sd_ldo_power);
  // the GPIO45 card-power enable is unpopulated on this board.
  // For slot 0 we must not force GPIO_NUM_NC here, otherwise the driver
  // rejects the configuration with "Invalid GPIO number -1".
  slot_config.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_err_t err = esp_vfs_fat_sdmmc_mount(mountpoint, &host, &slot_config, &mount_config, &_card);
  if (err != ESP_OK) {
    Serial.printf("[SD] Mount failed: %s (0x%x), freq=%d kHz\n",
                  esp_err_to_name(err),
                  static_cast<unsigned>(err),
                  sdmmc_frequency);
    _card = nullptr;
    _impl->mountpoint(nullptr);
    return false;
  }

  _impl->mountpoint(mountpoint);
  return true;
}

void GuitionPortraitSDMMCFS::end() {
  if (_card == nullptr) {
    return;
  }

  esp_vfs_fat_sdcard_unmount(_impl->mountpoint(), _card);
  _card = nullptr;
  _impl->mountpoint(nullptr);
}

sdcard_type_t GuitionPortraitSDMMCFS::cardType() const {
  if (_card == nullptr) {
    return CARD_NONE;
  }
  return (_card->ocr & SD_OCR_SDHC_CAP) ? CARD_SDHC : CARD_SD;
}

uint64_t GuitionPortraitSDMMCFS::cardSize() const {
  if (_card == nullptr) {
    return 0;
  }
  return static_cast<uint64_t>(_card->csd.capacity) * _card->csd.sector_size;
}

}  // namespace fs

fs::GuitionPortraitSDMMCFS GuitionPortraitSDMMC(FSImplPtr(new VFSImpl()));

#endif  // defined(DEVICE_GUITION_JC4880P443_PORTRAIT)
