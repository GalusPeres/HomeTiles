/* Appended to each variant's qio_qspi/include/sdkconfig.h when the objects in
 * this folder are rebuilt: the P4 display refresh and camera DMA interrupts
 * keep running while the cache is disabled (flash writes). */
#define CONFIG_LCD_DSI_ISR_CACHE_SAFE 1
#define CONFIG_DW_GDMA_ISR_IRAM_SAFE 1
#define CONFIG_DW_GDMA_OBJ_DRAM_SAFE 1
#define CONFIG_CAM_CTLR_MIPI_CSI_ISR_CACHE_SAFE 1
