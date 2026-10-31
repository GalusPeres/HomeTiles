#ifndef ELECROW_CROWPANEL_10_1_SDMMC_H
#define ELECROW_CROWPANEL_10_1_SDMMC_H

#include "src/devices/device_select.h"

#if defined(DEVICE_ELECROW_CROWPANEL_10_1)

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

class ElecrowSDMMCFS : public FS {
public:
  ElecrowSDMMCFS(FSImplPtr impl);

  bool begin(
      const char* mountpoint = "/sdcard",
      bool format_if_mount_failed = false,
      int sdmmc_frequency = SDMMC_FREQ_HIGHSPEED,
      uint8_t maxOpenFiles = 5);
  void end();

  sdcard_type_t cardType() const;
  uint64_t cardSize() const;

private:
  sdmmc_card_t* _card;
};

}  // namespace fs

extern fs::ElecrowSDMMCFS ElecrowSDMMC;

#endif  // defined(DEVICE_ELECROW_CROWPANEL_10_1)

#endif
