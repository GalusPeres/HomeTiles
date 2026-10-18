#pragma once

#include <driver/i2c_master.h>

namespace Elecrow10 {

esp_err_t initI2CBus(i2c_master_bus_handle_t& bus_handle, int sda_pin, int scl_pin);
esp_err_t deinitI2CBus(i2c_master_bus_handle_t bus_handle);

}  // namespace Elecrow10
