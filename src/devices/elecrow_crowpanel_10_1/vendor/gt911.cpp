#include "src/devices/elecrow_crowpanel_10_1/vendor/gt911.h"

#include <driver/i2c_master.h>
#include <esp_log.h>

namespace Elecrow10 {
namespace GT911 {

static const char* TAG = "GT911";
static constexpr uint8_t GT911_ADDR = 0x14;

bool init(i2c_master_bus_handle_t i2c_bus) {
    if (!i2c_bus) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return false;
    }

    i2c_master_dev_handle_t dev;
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, GT911_ADDR, 400000, &dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device at 0x%02X: %s", GT911_ADDR, esp_err_to_name(ret));
        uint8_t addr2 = 0x5D;
        ret = i2c_master_bus_add_device(i2c_bus, addr2, 400000, &dev);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add I2C device at 0x%02X: %s", addr2, esp_err_to_name(ret));
            return false;
        }
    }

    uint8_t product_id[4] = {0};
    ret = i2c_master_bus_read(dev, 0x8140, sizeof(product_id), product_id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read product ID: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "GT911 touch controller found (Product ID: %02X %02X %02X %02X)",
             product_id[0], product_id[1], product_id[2], product_id[3]);
    return true;
}

bool readTouch(int16_t& x, int16_t& y, i2c_master_bus_handle_t i2c_bus) {
    if (!i2c_bus) return false;

    i2c_master_dev_handle_t dev;
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, 0x14, 400000, &dev);
    if (ret != ESP_OK) {
        ret = i2c_master_bus_add_device(i2c_bus, 0x5D, 400000, &dev);
        if (ret != ESP_OK) return false;
    }

    uint8_t point_info[7];
    ret = i2c_master_bus_read(dev, 0x814E, sizeof(point_info), point_info);
    if (ret != ESP_OK) return false;

    if (point_info[0] & 0x80) {
        x = ((point_info[3] & 0x0F) << 8) | point_info[2];
        y = ((point_info[5] & 0x0F) << 8) | point_info[4];
        
        uint8_t clear_reg = 0;
        i2c_master_bus_write(dev, 0x814E, 1, &clear_reg);
        return true;
    }

    return false;
}

}  // namespace GT911
}  // namespace Elecrow10
