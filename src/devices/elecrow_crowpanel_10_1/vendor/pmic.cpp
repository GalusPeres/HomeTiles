#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)

#include "src/devices/elecrow_crowpanel_10_1/vendor/pmic.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr uint8_t kPmicI2cAddress = 0x45;

}  // namespace

esp_err_t elecrow_crowpanel_10_1_init_pmic(i2c_master_bus_handle_t bus) {
  if (!bus) {
    return ESP_ERR_INVALID_ARG;
  }

  // The PMIC sits on the same physical SDA/SCL lines as the touch controller.
  // Use the already-initialized driver_ng master bus instead of mixing in the
  // legacy I2C driver, which crashes on ESP32-P4.
  esp_err_t err = i2c_master_probe(bus, kPmicI2cAddress, 100);
  if (err != ESP_OK) {
    return err;
  }

  i2c_master_dev_handle_t pmic = nullptr;
  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = kPmicI2cAddress;
  dev_cfg.scl_speed_hz = 100000;
  dev_cfg.scl_wait_us = 20000;

  err = i2c_master_bus_add_device(bus, &dev_cfg, &pmic);
  if (err != ESP_OK) {
    return err;
  }

  auto write_pmic_reg = [&](uint8_t reg, uint8_t value) {
    const uint8_t data[2] = {reg, value};
    return i2c_master_transmit(pmic, data, sizeof(data), 100);
  };

  err = write_pmic_reg(0x95, 0x11);
  if (err == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(10));
    err = write_pmic_reg(0x95, 0x17);
  }
  if (err == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(10));
    err = write_pmic_reg(0x96, 0x00);
  }
  if (err == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(100));
    err = write_pmic_reg(0x96, 0xFF);
  }

  i2c_master_bus_rm_device(pmic);

  if (err == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  return err;
}

#endif  // defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
