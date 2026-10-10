# ES8311 codec driver for the speaker

Compiled only for profiles that define `HOMETILES_SPEAKER` in
`src/audio/speaker_select.h` (first: Waveshare ESP32-P4-WIFI6-Touch-LCD-7B).

## Source

- Espressif `esp-bsp`, component `es8311` 1.0.0~1, commit
  `f259a6abd5808019b1903a422092a4234ca95a51`, files
  `components/es8311/es8311.c` and `components/es8311/priv_include/es8311_reg.h`.
- License: Apache-2.0, see `LICENSE-APACHE-2.0.txt`. The original SPDX notice
  is retained in `es8311_codec.cpp`.

## What was taken

- The nine `coeff_div[]` rows whose MCLK is 256 x the sample rate (8 kHz to
  48 kHz), verbatim.
- The register sequences of `es8311_init()` (reset, clock source, slave I2S
  format, 16-bit serial ports, DAC power-up), `es8311_sample_frequency_config()`,
  the DAC volume register `0x32` and the DAC mute bits of `0x31`.

## What changed

- Register access uses the ESP-IDF `i2c_master` device API: the legacy
  `i2c_master_write_to_device()` calls of the original cannot share the bus
  that the display and touch drivers already opened with `i2c_master`.
- Microphone, ADC gain, fade and register-dump functions were left out.
- The chip ID check (`0xFD`/`0xFE` = `0x83`/`0x11`) is new.
