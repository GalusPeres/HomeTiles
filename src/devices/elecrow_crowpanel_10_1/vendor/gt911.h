/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP LCD touch: GT911
 *
 * This file contains the necessary functions and configurations to interact with
 * the GT911 touch controller via I2C. It provides initialization and touch data 
 * reading functions.
 */
#include "touch.h"
#include "i2c.h"
#include "displays_config.h"

/**
 * @brief I2C address of the GT911 controller
 *
 * @note When the interrupt GPIO pin is detected as low level on power-up,
 *       the I2C address used is 0x5D.
 * @note If the interrupt GPIO pin is high level, the I2C address used is 0x14.
 */
#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS          (0x5D)   // Default I2C address
#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP   (0x14)   // Backup I2C address when interrupt GPIO is high

#define EXAMPLE_PIN_NUM_TOUCH_RST       (GPIO_NUM_NC)
#define EXAMPLE_PIN_NUM_TOUCH_INT       (GPIO_NUM_NC)


/**
 * @brief GT911 Configuration Structure
 *
 * This structure contains the configuration for the GT911 touch controller,
 * such as the I2C device address.
 */
typedef struct {
    uint8_t dev_addr;  /*!< I2C device address */
} esp_lcd_touch_io_gt911_config_t;

/**
 * @brief Touch data structure for GT911
 *
 * This structure holds the touch point data, including the coordinates and
 * the number of touches detected by the controller.
 */
typedef struct {
    uint16_t x[ESP_LCD_TOUCH_MAX_POINTS];  /*!< X coordinates of touch points */
    uint16_t y[ESP_LCD_TOUCH_MAX_POINTS];  /*!< Y coordinates of touch points */
    uint8_t cnt;                          /*!< Number of detected touch points */
} touch_gt911_point_t;

/**
 * @brief Create a new GT911 touch driver instance
 *
 * This function initializes a new instance of the GT911 touch driver by configuring
 * the touch screen using the I2C communication protocol.
 *
 * @note The I2C communication should be initialized before calling this function.
 *
 * @param io LCD/Touch panel I/O handle to interface with the I2C bus.
 * @param config Configuration structure with touch screen settings (resolution, GPIO, etc.).
 * @param out_touch Output handle for the touch driver instance.
 * @return
 *      - ESP_OK: Successfully created a new touch driver instance
 *      - ESP_ERR_NO_MEM: Insufficient memory to allocate the touch instance
 */
esp_err_t esp_lcd_touch_new_i2c_gt911(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch);

/**
 * @brief Initialize the GT911 touch controller
 *
 * This function initializes the GT911 touch controller by configuring the I2C 
 * interface and touch settings.
 *
 * @return Touch handle for the initialized controller
 */
esp_lcd_touch_handle_t touch_gt911_init(DEV_I2C_Port port);

/**
 * @brief Read touch points from the GT911 touch controller
 *
 * This function reads the touch points detected by the GT911 controller and returns
 * the coordinates of the touch points.
 *
 * @param max_touch_cnt Maximum number of touch points to read.
 * @return A structure containing the coordinates and the number of detected touch points.
 */
touch_gt911_point_t touch_gt911_read_point(uint8_t max_touch_cnt);

/**
 * @brief Touch IO configuration structure for GT911
 *
 * This macro initializes the configuration structure for the GT911 touch controller's 
 * I2C interface with specific settings like the device address, SCL frequency, etc.
 */
#define ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG()           \
    {                                                 \
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,   /*!< Set the I2C address for the GT911 */  \
        .control_phase_bytes = 1,                         /*!< Set the number of bytes for the control phase */  \
        .dc_bit_offset = 0,                               /*!< Set the DC bit offset for I2C communication */  \
        .lcd_cmd_bits = 16,                               /*!< Set the LCD command bits */  \
        .flags =                                          \
        {                                                 \
            .disable_control_phase = 1,                   /*!< Disable control phase for I2C communication */  \
        },                                                \
        .scl_speed_hz = 400 * 1000,     /*!< Set the I2C master clock frequency */  \
    }

