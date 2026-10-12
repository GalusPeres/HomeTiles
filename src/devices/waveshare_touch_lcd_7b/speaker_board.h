#pragma once

// Speaker wiring of the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B, from the
// board's wiki (https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B):
// ES8311 codec at I2C 0x18 on the shared touch bus (I2C0, SDA 7, SCL 8),
// NS4150B amplifier enabled by PA_Ctrl (GPIO53, active high), PH 2.0 jack.
// The ES7210 microphone ADC (two MEMS mics on MIC1/MIC2) sits at I2C 0x40 on
// the same bus and shares MCLK, BCLK and LRCK; its SDOUT reaches the P4 on
// GPIO11 (Waveshare BSP for this board).

#include <driver/gpio.h>
#include <driver/i2c_master.h>

namespace speaker_board {

constexpr gpio_num_t kI2sMclk = GPIO_NUM_13;
constexpr gpio_num_t kI2sBclk = GPIO_NUM_12;
constexpr gpio_num_t kI2sLrck = GPIO_NUM_10;
// Codec DSDIN: data from the P4 to the codec.
constexpr gpio_num_t kI2sDout = GPIO_NUM_9;
// ES7210 SDOUT: data from the microphone ADC to the P4.
constexpr gpio_num_t kI2sDin = GPIO_NUM_11;
constexpr gpio_num_t kAmpEnable = GPIO_NUM_53;
constexpr bool kAmpActiveHigh = true;
constexpr uint8_t kCodecAddress = 0x18;
// ES7210 7-bit address; 0 on a speaker board without a microphone ADC.
constexpr uint8_t kMicAddress = 0x40;

// The touch-owned I2C bus; nullptr before Device init opened it.
i2c_master_bus_handle_t bus();

}  // namespace speaker_board
