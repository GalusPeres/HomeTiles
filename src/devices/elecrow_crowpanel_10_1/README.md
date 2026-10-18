# Elecrow CrowPanel Advanced 10.1" Support

HomeTiles firmware support for the **Elecrow CrowPanel Advanced 10.1"** ESP32-P4 display.

## Hardware Specifications

- **Main Processor:** ESP32-P4 dual-core RISC-V (400 MHz)
- **Flash:** 16 MB
- **PSRAM:** 32 MB
- **Display:** 10.1" IPS, 1024×600 resolution
- **Display Interface:** MIPI-DSI (2-lane)
- **Touch Controller:** GT911 / FT5x06 via I2C
- **Network Co-processor:** ESP32-C6 (Wi-Fi 6, Bluetooth 5.3)

## Pin Configuration

| Function | GPIO Pin |
|----------|----------|
| I2C SDA  | 7        |
| I2C SCL  | 8        |
| Touch INT| 9        |
| Backlight| 26       |

## Building

To build for Elecrow CrowPanel Advanced 10.1":

1. Select `DEVICE_ELECROW_CROWPANEL_10_1` in `src/devices/device_select.h`
2. Use the `elecrow_crowpanel_10_1` build profile in PlatformIO or Arduino CLI
3. Flash using the standard ESP32-P4 flashing procedure

## Known Limitations

- Audio codec support: Not yet implemented (board has no built-in speaker/mic)
- Camera support: Not available (no CSI connector on this model)
- SD card: Supported via standard HomeTiles SD card path

## References

- [Eleccrow Product Page](https://www.elecrow.com/)
- [ESP32-P4 Documentation](https://www.espressif.com/en/products/socs/esp32p4)
