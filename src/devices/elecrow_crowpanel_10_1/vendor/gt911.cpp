/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "Arduino.h"

#include "gt911.h"

static const char *TAG = "GT911";

/* GT911 registers */
#define ESP_LCD_TOUCH_GT911_READ_KEY_REG    (0x8093)
#define ESP_LCD_TOUCH_GT911_READ_XY_REG     (0x814E)
#define ESP_LCD_TOUCH_GT911_CONFIG_REG      (0x8047)
#define ESP_LCD_TOUCH_GT911_PRODUCT_ID_REG  (0x8140)
#define ESP_LCD_TOUCH_GT911_ENTER_SLEEP     (0x8040)

/* GT911 support key num */
#define ESP_GT911_TOUCH_MAX_BUTTONS         (4)

esp_lcd_touch_handle_t tp_handle = NULL; // Declare a handle for the touch panel
/*******************************************************************************
* Function definitions
*******************************************************************************/
static esp_err_t esp_lcd_touch_gt911_read_data(esp_lcd_touch_handle_t tp);
static bool esp_lcd_touch_gt911_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
static esp_err_t esp_lcd_touch_gt911_get_track_id(esp_lcd_touch_handle_t tp, uint8_t *track_id, uint8_t point_num);
#if (ESP_LCD_TOUCH_MAX_BUTTONS > 0)
static esp_err_t esp_lcd_touch_gt911_get_button_state(esp_lcd_touch_handle_t tp, uint8_t n, uint8_t *state);
#endif
static esp_err_t esp_lcd_touch_gt911_del(esp_lcd_touch_handle_t tp);

/* I2C read/write */
static esp_err_t touch_gt911_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len);
static esp_err_t touch_gt911_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t data);

/* GT911 reset */
static esp_err_t touch_gt911_reset(esp_lcd_touch_handle_t tp);
/* Read status and config register */
static esp_err_t touch_gt911_read_cfg(esp_lcd_touch_handle_t tp);

/* GT911 enter/exit sleep mode */
static esp_err_t esp_lcd_touch_gt911_enter_sleep(esp_lcd_touch_handle_t tp);
static esp_err_t esp_lcd_touch_gt911_exit_sleep(esp_lcd_touch_handle_t tp);

/*******************************************************************************
* Public API functions
*******************************************************************************/

esp_err_t esp_lcd_touch_new_i2c_gt911(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch) {
    esp_err_t ret = ESP_OK;

    // Check for invalid arguments
    if (!io || !config || !out_touch) {
        ESP_LOGE(TAG, "Invalid arguments!");
        return ESP_ERR_INVALID_ARG;
    }

    // Dynamically allocate memory for the touch controller handle
    esp_lcd_touch_handle_t esp_lcd_touch_gt911 = 
        (esp_lcd_touch_handle_t)heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);
    if (!esp_lcd_touch_gt911) {
        ESP_LOGE(TAG, "Memory allocation failed for GT911 controller");
        return ESP_ERR_NO_MEM;
    }

    // Initialize the touch controller structure
    esp_lcd_touch_gt911->io = io;
    esp_lcd_touch_gt911->read_data = esp_lcd_touch_gt911_read_data;
    esp_lcd_touch_gt911->get_xy = esp_lcd_touch_gt911_get_xy;
    esp_lcd_touch_gt911->get_track_id = esp_lcd_touch_gt911_get_track_id;
#if (ESP_LCD_TOUCH_MAX_BUTTONS > 0)
    esp_lcd_touch_gt911->get_button_state = esp_lcd_touch_gt911_get_button_state;
#endif
    esp_lcd_touch_gt911->del = esp_lcd_touch_gt911_del;
    esp_lcd_touch_gt911->enter_sleep = esp_lcd_touch_gt911_enter_sleep;
    esp_lcd_touch_gt911->exit_sleep = esp_lcd_touch_gt911_exit_sleep;
    esp_lcd_touch_gt911->data.lock.owner = portMUX_FREE_VAL;
    memcpy(&esp_lcd_touch_gt911->config, config, sizeof(esp_lcd_touch_config_t));

    // Configure the reset GPIO pin if specified
    if (config->rst_gpio_num != GPIO_NUM_NC) {
        gpio_config_t rst_gpio_config = {
            .pin_bit_mask = BIT64(config->rst_gpio_num),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ret = gpio_config(&rst_gpio_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure reset GPIO");
            heap_caps_free(esp_lcd_touch_gt911);  // Free allocated memory
            return ret;
        }
    }

    // Configure the interrupt GPIO pin if specified
    if (config->int_gpio_num != GPIO_NUM_NC) {
        gpio_config_t int_gpio_config = {
            .pin_bit_mask = BIT64(config->int_gpio_num),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = config->levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE,
        };
        ret = gpio_config(&int_gpio_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure interrupt GPIO");
            heap_caps_free(esp_lcd_touch_gt911);  // Free allocated memory
            return ret;
        }
    }

    // Reset the touch controller
    ret = touch_gt911_reset(esp_lcd_touch_gt911);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GT911 reset failed");
        heap_caps_free(esp_lcd_touch_gt911);  // Free allocated memory
        return ret;
    }

    // Read the touch controller configuration
    ret = touch_gt911_read_cfg(esp_lcd_touch_gt911);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GT911 configuration read failed");
        heap_caps_free(esp_lcd_touch_gt911);  // Free allocated memory
        return ret;
    }

    // On success, return the touch controller handle to the caller
    *out_touch = esp_lcd_touch_gt911;
    return ESP_OK;
}




static esp_err_t esp_lcd_touch_gt911_enter_sleep(esp_lcd_touch_handle_t tp)
{
    esp_err_t err = touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_ENTER_SLEEP, 0x05);
    ESP_RETURN_ON_ERROR(err, TAG, "Enter Sleep failed!");

    return ESP_OK;
}

static esp_err_t esp_lcd_touch_gt911_exit_sleep(esp_lcd_touch_handle_t tp)
{
    esp_err_t ret;
    esp_lcd_touch_handle_t esp_lcd_touch_gt911 = tp;

    if (esp_lcd_touch_gt911->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_gpio_config_high = {
            .pin_bit_mask = BIT64(esp_lcd_touch_gt911->config.int_gpio_num),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ret = gpio_config(&int_gpio_config_high);
        ESP_RETURN_ON_ERROR(ret, TAG, "High GPIO config failed");
        gpio_set_level(esp_lcd_touch_gt911->config.int_gpio_num, 1);

        vTaskDelay(pdMS_TO_TICKS(5));

        const gpio_config_t int_gpio_config_float = {
            .pin_bit_mask = BIT64(esp_lcd_touch_gt911->config.int_gpio_num),
            .mode = GPIO_MODE_OUTPUT_OD,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ret = gpio_config(&int_gpio_config_float);
        ESP_RETURN_ON_ERROR(ret, TAG, "Float GPIO config failed");
    }

    return ESP_OK;
}

static esp_err_t esp_lcd_touch_gt911_read_data(esp_lcd_touch_handle_t tp)
{
    esp_err_t err;
    uint8_t buf[1 + (ESP_LCD_TOUCH_MAX_POINTS * 8)];
    uint8_t touch_cnt = 0;
    uint8_t clear = 0;
    size_t i = 0;

    assert(tp != NULL);

    err = touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_READ_XY_REG, buf, 1);
    ESP_RETURN_ON_ERROR(err, TAG, "I2C read error!");

    /* Any touch data? */
    if ((buf[0] & 0x80) == 0x00) {
        touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_READ_XY_REG, clear);
#if (ESP_LCD_TOUCH_MAX_BUTTONS > 0)
    } else if ((buf[0] & 0x10) == 0x10) {
        /* Read all keys */
        uint8_t key_max = ((ESP_GT911_TOUCH_MAX_BUTTONS < ESP_LCD_TOUCH_MAX_BUTTONS) ? \
                           (ESP_GT911_TOUCH_MAX_BUTTONS) : (ESP_LCD_TOUCH_MAX_BUTTONS));
        err = touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_READ_KEY_REG, &buf[0], key_max);
        ESP_RETURN_ON_ERROR(err, TAG, "I2C read error!");

        /* Clear all */
        touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_READ_XY_REG, clear);
        ESP_RETURN_ON_ERROR(err, TAG, "I2C write error!");

        portENTER_CRITICAL(&tp->data.lock);

        /* Buttons count */
        tp->data.buttons = key_max;
        for (i = 0; i < key_max; i++) {
            tp->data.button[i].status = buf[0] ? 1 : 0;
        }

        portEXIT_CRITICAL(&tp->data.lock);
#endif
    } else if ((buf[0] & 0x80) == 0x80) {
        portENTER_CRITICAL(&tp->data.lock);
        tp->data.points = 0;
#if (ESP_LCD_TOUCH_MAX_BUTTONS > 0)
        for (i = 0; i < ESP_LCD_TOUCH_MAX_BUTTONS; i++) {
            tp->data.button[i].status = 0;
        }
#endif
        portEXIT_CRITICAL(&tp->data.lock);
        /* Count of touched points */
        touch_cnt = buf[0] & 0x0f;
        if (touch_cnt > ESP_LCD_TOUCH_MAX_POINTS || touch_cnt == 0) {
            touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_READ_XY_REG, clear);
            return ESP_OK;
        }

        /* Read all points */
        err = touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_READ_XY_REG + 1, &buf[1], touch_cnt * 8);
        ESP_RETURN_ON_ERROR(err, TAG, "I2C read error!");

        /* Clear all */
        err = touch_gt911_i2c_write(tp, ESP_LCD_TOUCH_GT911_READ_XY_REG, clear);
        ESP_RETURN_ON_ERROR(err, TAG, "I2C read error!");

        portENTER_CRITICAL(&tp->data.lock);

        /* Number of touched points */
        touch_cnt = (touch_cnt > ESP_LCD_TOUCH_MAX_POINTS ? ESP_LCD_TOUCH_MAX_POINTS : touch_cnt);
        tp->data.points = touch_cnt;

        /* Fill all coordinates */
        for (i = 0; i < touch_cnt; i++) {
            tp->data.coords[i].track_id = buf[(i * 8) + 1];
            tp->data.coords[i].x = ((uint16_t)buf[(i * 8) + 3] << 8) + buf[(i * 8) + 2];
            tp->data.coords[i].y = (((uint16_t)buf[(i * 8) + 5] << 8) + buf[(i * 8) + 4]);
            tp->data.coords[i].strength = (((uint16_t)buf[(i * 8) + 7] << 8) + buf[(i * 8) + 6]);
        }

        portEXIT_CRITICAL(&tp->data.lock);
    }

    return ESP_OK;
}

static bool esp_lcd_touch_gt911_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    assert(tp != NULL);
    assert(x != NULL);
    assert(y != NULL);
    assert(point_num != NULL);
    assert(max_point_num > 0);

    portENTER_CRITICAL(&tp->data.lock);

    /* Count of points */
    *point_num = (tp->data.points > max_point_num ? max_point_num : tp->data.points);

    for (size_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;

        if (strength) {
            strength[i] = tp->data.coords[i].strength;
        }
    }

    portEXIT_CRITICAL(&tp->data.lock);

    return (*point_num > 0);
}

static esp_err_t esp_lcd_touch_gt911_get_track_id(esp_lcd_touch_handle_t tp, uint8_t *track_id, uint8_t point_num)
{
    assert(tp != NULL);
    assert(track_id != NULL);

    portENTER_CRITICAL(&tp->data.lock);

    const uint8_t count = (tp->data.points > point_num ? point_num : tp->data.points);
    for (uint8_t i = 0; i < count; i++) {
        track_id[i] = tp->data.coords[i].track_id;
    }

    portEXIT_CRITICAL(&tp->data.lock);

    return ESP_OK;
}

#if (ESP_LCD_TOUCH_MAX_BUTTONS > 0)
static esp_err_t esp_lcd_touch_gt911_get_button_state(esp_lcd_touch_handle_t tp, uint8_t n, uint8_t *state)
{
    esp_err_t err = ESP_OK;
    assert(tp != NULL);
    assert(state != NULL);

    *state = 0;

    portENTER_CRITICAL(&tp->data.lock);

    if (n > tp->data.buttons) {
        err = ESP_ERR_INVALID_ARG;
    } else {
        *state = tp->data.button[n].status;
    }

    portEXIT_CRITICAL(&tp->data.lock);

    return err;
}
#endif

static esp_err_t esp_lcd_touch_gt911_del(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    /* Reset GPIO pin settings */
    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.int_gpio_num);
        if (tp->config.interrupt_callback) {
            gpio_isr_handler_remove(tp->config.int_gpio_num);
        }
    }

    /* Reset GPIO pin settings */
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.rst_gpio_num);
    }

    free(tp);

    return ESP_OK;
}

// Function to initialize the GT911 touch controller
esp_lcd_touch_handle_t touch_gt911_init(DEV_I2C_Port port)
{
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;  // Declare a handle for touch panel I/O
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    uint8_t touch_addr = 0;

    delay(120);

    if (i2c_master_probe(port.bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 100) == ESP_OK) {
        touch_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    } else if (i2c_master_probe(port.bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, 100) == ESP_OK) {
        touch_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    } else {
        ESP_LOGE(TAG, "GT911 probe failed on 0x%02X and 0x%02X",
                 ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
                 ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);
        return NULL;
    }

    tp_io_config.dev_addr = touch_addr;
    ESP_LOGI(TAG, "Using touch address 0x%02X", touch_addr);

    ESP_LOGI(TAG, "Initialize I2C panel IO");  // Log I2C panel I/O initialization
    // Create a new I2C panel I/O handle for the touch controller
    esp_err_t err = esp_lcd_new_panel_io_i2c(port.bus, &tp_io_config, &tp_io_handle);
    if (err != ESP_OK || !tp_io_handle) {
        ESP_LOGE(TAG, "GT911 panel IO init failed: %d", err);
        return NULL;
    }

    ESP_LOGI(TAG, "Initialize touch controller GT911");  // Log touch controller initialization
    // Configure the touch controller with necessary settings (coordinates, GPIO pins, etc.)
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = display_cfg.width,  // Set the maximum X coordinate based on screen resolution
        .y_max = display_cfg.height,  // Set the maximum Y coordinate based on screen resolution
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST,  // GPIO number for reset
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,  // GPIO number for interrupt
        .levels = {
            .reset = 0,  // Low level for reset
            .interrupt = 0,  // Low level for interrupt
        },
        .flags = {
            .swap_xy = 0,  // No swap of X and Y coordinates
            .mirror_x = 0,  // No mirroring of X axis
            .mirror_y = 0,  // No mirroring of Y axis
        },
    };

    // Create a new touch controller instance using the configured I2C and settings
    tp_handle = NULL;
    err = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle);
    if (err != ESP_OK || !tp_handle) {
        ESP_LOGE(TAG, "GT911 touch init failed: %d", err);
        esp_lcd_panel_io_del(tp_io_handle);
        return NULL;
    }

    return tp_handle;  // Return the touch controller handle
}

// Function to read touch points from the GT911 touch controller
touch_gt911_point_t touch_gt911_read_point(uint8_t max_touch_cnt)
{
    touch_gt911_point_t data;  // Declare a structure to hold touch point data

    /* Read touch data from the touch controller */
    esp_lcd_touch_read_data(tp_handle);  // Read raw data from the touch controller

    /* Get the touch coordinates and count of touch points */
    esp_lcd_touch_get_coordinates(tp_handle, data.x, data.y, NULL, &data.cnt, max_touch_cnt);

    return data;  // Return the touch point data
}


/*******************************************************************************
* Private API function
*******************************************************************************/

/* Reset controller */
static esp_err_t touch_gt911_reset(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

static esp_err_t touch_gt911_read_cfg(esp_lcd_touch_handle_t tp)
{
    uint8_t buf[4];

    assert(tp != NULL);

    ESP_RETURN_ON_ERROR(touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_PRODUCT_ID_REG, (uint8_t *)&buf[0], 3), TAG, "GT911 read error!");
    ESP_RETURN_ON_ERROR(touch_gt911_i2c_read(tp, ESP_LCD_TOUCH_GT911_CONFIG_REG, (uint8_t *)&buf[3], 1), TAG, "GT911 read error!");

    ESP_LOGI(TAG, "TouchPad_ID:0x%02x,0x%02x,0x%02x", buf[0], buf[1], buf[2]);
    ESP_LOGI(TAG, "TouchPad_Config_Version:%d", buf[3]);
    Serial.printf("[Device/WaveshareTouchLCD10] TouchPad_ID:%02X%02X%02X cfg=%u\n",
                  static_cast<unsigned>(buf[0]),
                  static_cast<unsigned>(buf[1]),
                  static_cast<unsigned>(buf[2]),
                  static_cast<unsigned>(buf[3]));

    return ESP_OK;
}

static esp_err_t touch_gt911_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len)
{
    assert(tp != NULL);
    assert(data != NULL);

    /* Read data */
    return esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
}

static esp_err_t touch_gt911_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t data)
{
    assert(tp != NULL);

    // *INDENT-OFF*
    /* Write data */
    // return esp_lcd_panel_io_tx_param(tp->io, reg, (uint8_t[]){data}, 1);
    uint8_t data_array[1] = {data};
    return esp_lcd_panel_io_tx_param(tp->io, reg, data_array, 1);

    // *INDENT-ON*
}

#endif  // defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
