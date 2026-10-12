# ES7210 microphone ADC driver for Assist capture

Compiled only for profiles that define `HOMETILES_SPEAKER` in
`src/audio/speaker_select.h` (first: Waveshare ESP32-P4-WIFI6-Touch-LCD-7B);
the speaker worker owns I2S and both codecs.

## Source

- Espressif `esp-bsp`, component `es7210` 1.0.1~1, commit
  `f259a6abd5808019b1903a422092a4234ca95a51`, files
  `components/es7210/es7210.c`, `components/es7210/include/es7210.h` and
  `components/es7210/es7210_reg.h`.
- License: Apache-2.0, see `LICENSE-APACHE-2.0.txt`. The original SPDX notice
  is retained in `es7210_codec.cpp`.

## What was taken

- The `es7210_coeff_div[]` row for 16 kHz with MCLK 4.096 MHz (256 x fs),
  verbatim.
- The register sequence of `es7210_config_codec()` (software reset, power-up
  timing, ADC high-pass filters, analog power, MIC1-4 power, DLL power-down,
  enable), with the values that `es7210_set_i2s_format()` (standard I2S,
  16-bit, TDM off), `es7210_set_mic_bias()` (2.87 V),
  `es7210_set_mic_gain()` (30 dB) and `es7210_set_i2s_sample_rate()` write.

## What changed

- Register access uses the ESP-IDF `i2c_master` device API: the legacy
  `i2c_cmd_link` calls of the original cannot share the bus that the display,
  touch and ES8311 drivers already opened with `i2c_master`.
- Format, bias, gain and sample rate are fixed constants instead of a
  configuration struct; `es7210_config_volume()` and the other table rows
  were left out.
- The probe (reading chip ID registers `0x3D`/`0x3E`) is new. Only the I2C
  acknowledge is required; the ID values are logged, not compared.
