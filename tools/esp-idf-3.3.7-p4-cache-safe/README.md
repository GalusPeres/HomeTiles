# Cache-safe P4 display and camera interrupts (Arduino ESP32 3.3.7)

## Problem

On every ESP32-P4 DSI panel the screen flashed blue while HomeTiles wrote to
flash (moving tiles, saving settings). The DSI DMA (DW-GDMA) stops after each
frame and its "full transfer done" interrupt re-arms the next one
(`mipi_dsi_dma_trans_done_cb`). Arduino 3.3.7 ships ESP-IDF with
`CONFIG_LCD_DSI_ISR_CACHE_SAFE` off, so that interrupt is allocated without
`ESP_INTR_FLAG_IRAM` and stays masked while the cache is disabled for a flash
write. The DSI bridge FIFO then underflows and sends `dpi_rsv_data`, whose
reset value `0x3FFF` is the blue the panel showed.

## Fix

Three ESP-IDF objects are rebuilt with Espressif's cache-safe options
(`sdkconfig-additions.h`) and injected into each P4 variant's precompiled
archive before every P4 build:

| Object | Archive | Option |
| --- | --- | --- |
| `esp_lcd_panel_dpi.c.obj` | `libesp_lcd.a` | `CONFIG_LCD_DSI_ISR_CACHE_SAFE` |
| `dw_gdma.c.obj` | `libesp_hw_support.a` | `CONFIG_DW_GDMA_ISR_IRAM_SAFE`, `CONFIG_DW_GDMA_OBJ_DRAM_SAFE` |
| `esp_cam_ctlr_csi.c.obj` | `libesp_driver_cam.a` | `CONFIG_CAM_CTLR_MIPI_CSI_ISR_CACHE_SAFE` |

The camera is part of the fix because the CSI receiver uses the same DW-GDMA
interrupt line, and a shared interrupt is either IRAM-safe for every handler
or for none. The CSI DMA callback is already `IRAM_ATTR` in the IDF source,
but it calls `dw_gdma_channel_config_transfer()`, which the prebuilt link
script keeps in flash; `dw_gdma-config-transfer-iram.patch` moves it to IRAM
with ISR-safe checks. HomeTiles' own CSI callbacks
(`src/video/local_camera/local_camera.cpp`) and the DPI callbacks of every P4
board driver are `IRAM_ATTR` and only touch internal RAM. Tab5 (M5GFX) and
Waveshare 4B (Arduino_GFX) register no DPI callbacks.

Everything else the interrupts run is already placed in IRAM by the shipped
`sections.ld` (`CONFIG_LCD_DSI_ISR_HANDLER_IN_IRAM=y`): the DSI bridge ISR and
DMA callback, `dw_gdma_channel_default_isr`, the DW-GDMA link-list helpers and
`esp_cache_msync`.

## Source and build

- ESP-IDF `release/v5.5` commit `87912cd291d68f4319f13695718af6754879a83f`,
  the commit named in both variants' `versions.txt` (lib-builder `8cabf2c`).
- Files: `components/esp_lcd/dsi/esp_lcd_panel_dpi.c`,
  `components/esp_hw_support/dma/dw_gdma.c` (plus the patch),
  `components/esp_driver_cam/csi/src/esp_cam_ctlr_csi.c`.
- Toolchain `esp-rv32` 2511 (`riscv32-esp-elf-gcc` 14.2.0) with the variant's
  `flags/c_flags`, `flags/defines` and `flags/includes` response files,
  `qio_qspi/include/sdkconfig.h` plus `sdkconfig-additions.h`, and `-Os`.
- Each rebuilt object has the same symbol table as the stock object; the only
  differences are the cache-safe interrupt flags, the internal-RAM channel
  objects and the IRAM registration checks.

SHA-256:

| Variant | Object | SHA-256 |
| --- | --- | --- |
| esp32p4-libs | esp_lcd_panel_dpi.c.obj | `37d2df6f2c92d1b2f35371d68008992b388c2919736e10280d96db7cc798abb5` |
| esp32p4-libs | dw_gdma.c.obj | `a08154399e515e0f6ab50a1b54acd79009a254d90bdd0468ffac57b9a2fe1a48` |
| esp32p4-libs | esp_cam_ctlr_csi.c.obj | `f4fe2c1a0b2d72bf0908ef8c6fc01663d7d4cbf6dab0ca1ce46b271b099c0d63` |
| esp32p4_es-libs | esp_lcd_panel_dpi.c.obj | `ad3f9bffdf0051d154bbc6dc7f0f834332c5ddec59a2f1fc3fbc6c8134cdbd1a` |
| esp32p4_es-libs | dw_gdma.c.obj | `9fe4c2120acdf294987511d566719b997c6ebf6e7f77b506730b14557ad476f5` |
| esp32p4_es-libs | esp_cam_ctlr_csi.c.obj | `da2578671f84529a1630de407d5a77c2d8d87c40245aa4f5c113ab4ca56b2b58` |

## Application and verification

`tools/apply-p4-cache-safe-local.ps1` (local builds, called by
`tools/build-firmware-local.ps1`) and the firmware workflow inject the objects
and compare each archive member byte for byte. A P4 firmware must contain the
DW-GDMA registration check `on_full_trans_done not in IRAM`, which only the
rebuilt object has.

Hardware: Guition JC8012P4A1 V2 b137 (2026-10-01) saved the grid five times
without a blue flash; the built-in camera took a snapshot and streamed at
25 FPS. Other P4 boards are pending.
