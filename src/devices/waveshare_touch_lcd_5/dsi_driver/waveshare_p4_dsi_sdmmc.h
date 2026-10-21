#ifndef WAVESHARE_P4_DSI_SDMMC_H
#define WAVESHARE_P4_DSI_SDMMC_H

#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_P4_DSI_BOARD)

#include <FS.h>
#include <vfs_api.h>
#include <driver/sdmmc_host.h>
#include <driver/sdmmc_types.h>

typedef enum {
  CARD_NONE,
  CARD_MMC,
  CARD_SD,
  CARD_SDHC,
  CARD_UNKNOWN
} sdcard_type_t;

namespace fs {

// microSD of the shared Waveshare ESP32-P4 MIPI-DSI boards: 4-bit SDMMC on
// slot 0 (the ESP32-P4 IO-MUX pins CLK 43, CMD 44, D0-D3 39-42; slot 1
// carries the ESP-Hosted link to the ESP32-C6), the card powered from an
// on-chip LDO channel at 3.3 V.
class WaveshareP4DsiSDMMCFS : public FS {
public:
  WaveshareP4DsiSDMMCFS(FSImplPtr impl);

  bool begin(int ldo_channel,
             const char* mountpoint = "/sdcard",
             bool format_if_mount_failed = false,
             int sdmmc_frequency = SDMMC_FREQ_DEFAULT,
             uint8_t maxOpenFiles = 5);
  void end();

  sdcard_type_t cardType() const;
  uint64_t cardSize() const;

private:
  sdmmc_card_t* _card;
};

}  // namespace fs

extern fs::WaveshareP4DsiSDMMCFS WaveshareP4DsiSDMMC;

#endif  // defined(DEVICE_WAVESHARE_P4_DSI_BOARD)

#endif
