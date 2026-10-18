#include "src/devices/elecrow_crowpanel_10_1/vendor/i2c.h"

#include <driver/i2c_master.h>
#include <esp_log.h>

namespace Elecrow10 {

static const char* TAG = "I2C";

esp_err_t initI2CBus(i2c_master_bus_handle_t& bus_handle, int sda_pin, int scl_pin) {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d)", sda_pin, scl_pin);
    return ESP_OK;
}

esp_err_t deinitI2CBus(i2c_master_bus_handle_t bus_handle) {
    if (bus_handle) {
        return i2c_del_master_bus(bus_handle);
    }
    return ESP_ERR_INVALID_ARG;
}

}  // namespace Elecrow10
