/*
 * HX8394 MIPI-DSI panel driver from Waveshare's esp_lcd_hx8394 component
 * 2.1.0 (waveshareteam/Waveshare-ESP32-components,
 * display/lcd/esp_lcd_hx8394, commit
 * 23a6012d5e87d27399cf90fb851cab3689e29159), MIT License
 * (../LICENSE-MIT.txt).
 *
 * Modified for HomeTiles: built for the Waveshare ESP32-P4-WIFI6-Touch-LCD-5
 * only; the standalone board-specific I2C sequence (address 0x45) is removed
 * because HomeTiles owns the board I2C bus, as Waveshare's LCD-5 BSP does
 * with ESP_LCD_HX8394_SKIP_I2C_INIT; the component version that CMake
 * generates upstream has a fallback for the Arduino build.
 */

#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_5)

#include "soc/soc_caps.h"

#if SOC_MIPI_DSI_SUPPORTED
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_lcd_hx8394.h"
#include "esp_idf_version.h"
#include "sdkconfig.h"

// The upstream component CMake generates these version definitions. Arduino
// builds do not run that CMake step, so keep the vendored component version.
#ifndef ESP_LCD_HX8394_VER_MAJOR
#define ESP_LCD_HX8394_VER_MAJOR 2
#endif
#ifndef ESP_LCD_HX8394_VER_MINOR
#define ESP_LCD_HX8394_VER_MINOR 1
#endif
#ifndef ESP_LCD_HX8394_VER_PATCH
#define ESP_LCD_HX8394_VER_PATCH 0
#endif

#define HX8394_CMD_DSI_INT0 (0xBA)
#define HX8394_DSI_1_LANE (0x60)
#define HX8394_DSI_2_LANE (0x61)
#define HX8394_DSI_3_LANE (0x62)
#define HX8394_DSI_4_LANE (0x63)

typedef struct
{
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_val; // save surrent value of LCD_CMD_COLMOD register
    const hx8394_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    uint8_t lane_num;
    struct
    {
        unsigned int reset_level : 1;
    } flags;
    // To save the original functions of MIPI DPI panel
    esp_err_t (*del)(esp_lcd_panel_t *panel);
    esp_err_t (*init)(esp_lcd_panel_t *panel);
} hx8394_panel_t;

static const char *TAG = "hx8394";

static esp_err_t panel_hx8394_del(esp_lcd_panel_t *panel);
static esp_err_t panel_hx8394_init(esp_lcd_panel_t *panel);
static esp_err_t panel_hx8394_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_hx8394_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_hx8394_disp_on_off(esp_lcd_panel_t *panel, bool on_off);

esp_err_t esp_lcd_new_panel_hx8394(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel)
{
    ESP_LOGI(TAG, "version: %d.%d.%d", ESP_LCD_HX8394_VER_MAJOR, ESP_LCD_HX8394_VER_MINOR,
             ESP_LCD_HX8394_VER_PATCH);
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid arguments");
    hx8394_vendor_config_t *vendor_config = (hx8394_vendor_config_t *)panel_dev_config->vendor_config;
    ESP_RETURN_ON_FALSE(vendor_config && vendor_config->mipi_config.dpi_config && vendor_config->mipi_config.dsi_bus, ESP_ERR_INVALID_ARG, TAG,
                        "invalid vendor config");

    esp_err_t ret = ESP_OK;
    hx8394_panel_t *hx8394 = (hx8394_panel_t *)calloc(1, sizeof(hx8394_panel_t));
    ESP_RETURN_ON_FALSE(hx8394, ESP_ERR_NO_MEM, TAG, "no mem for hx8394 panel");

    if (panel_dev_config->reset_gpio_num >= 0)
    {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order)
    {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        hx8394->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        hx8394->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color space");
        break;
    }

    switch (panel_dev_config->bits_per_pixel)
    {
    case 16: // RGB565
        hx8394->colmod_val = 0x55;
        break;
    case 18: // RGB666
        hx8394->colmod_val = 0x66;
        break;
    case 24: // RGB888
        hx8394->colmod_val = 0x77;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    hx8394->io = io;
    hx8394->init_cmds = vendor_config->init_cmds;
    hx8394->init_cmds_size = vendor_config->init_cmds_size;
    hx8394->lane_num = vendor_config->mipi_config.lane_num;
    hx8394->reset_gpio_num = panel_dev_config->reset_gpio_num;
    hx8394->flags.reset_level = panel_dev_config->flags.reset_active_high;

    // Create MIPI DPI panel
    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_dpi(vendor_config->mipi_config.dsi_bus, vendor_config->mipi_config.dpi_config, &panel_handle), err, TAG,
                      "create MIPI DPI panel failed");
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    ESP_GOTO_ON_ERROR(esp_lcd_dpi_panel_enable_dma2d(panel_handle), err, TAG, "enable MIPI DPI DMA2D failed");
#endif
    ESP_LOGD(TAG, "new MIPI DPI panel @%p", panel_handle);

    // Save the original functions of MIPI DPI panel
    hx8394->del = panel_handle->del;
    hx8394->init = panel_handle->init;
    // Overwrite the functions of MIPI DPI panel
    panel_handle->del = panel_hx8394_del;
    panel_handle->init = panel_hx8394_init;
    panel_handle->reset = panel_hx8394_reset;
    panel_handle->invert_color = panel_hx8394_invert_color;
    panel_handle->disp_on_off = panel_hx8394_disp_on_off;
    panel_handle->user_data = hx8394;
    *ret_panel = panel_handle;
    ESP_LOGD(TAG, "new hx8394 panel @%p", hx8394);

    return ESP_OK;

err:
    if (hx8394)
    {
        if (panel_dev_config->reset_gpio_num >= 0)
        {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(hx8394);
    }
    return ret;
}

static const hx8394_lcd_init_cmd_t vendor_specific_init_code_default[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xB9, (uint8_t[]){0xFF,0x83,0x94}, 3, 0},
    {0xB1, (uint8_t[]){0x48,0x0A,0x6A,0x09,0x33,0x54,0x71,0x71,0x2E,0x45}, 10, 0},
    {0xBA, (uint8_t[]){0x61,0x03,0x68,0x6B,0xB2,0xC0}, 6, 0},
    {0xB2, (uint8_t[]){0x00,0x80,0x64,0x0C,0x06,0x2F}, 6, 0},
    {0xB4, (uint8_t[]){0x1C,0x78,0x1C,0x78,0x1C,0x78,0x01,0x0C,0x86,0x75,0x00,0x3F,0x1C,0x78,0x1C,0x78,0x1C,0x78,0x01,0x0C,0x86}, 21, 0},
    {0xD3, (uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x08,0x32,0x10,0x05,0x00,0x05,0x32,0x13,0xC1,0x00,0x01,0x32,0x10,0x08,0x00,0x00,0x37,0x03,0x07,0x07,0x37,0x05,0x05,0x37,0x0C,0x40}, 33, 0},
    {0xD5, (uint8_t[]){0x18,0x18,0x18,0x18,0x22,0x23,0x20,0x21,0x04,0x05,0x06,0x07,0x00,0x01,0x02,0x03,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x19,0x19,0x19,0x19}, 44, 0},
    {0xD6, (uint8_t[]){0x18,0x18,0x19,0x19,0x21,0x20,0x23,0x22,0x03,0x02,0x01,0x00,0x07,0x06,0x05,0x04,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x19,0x19,0x18,0x18}, 44, 0},
    {0xE0, (uint8_t[]){0x07,0x08,0x09,0x0D,0x10,0x14,0x16,0x13,0x24,0x36,0x48,0x4A,0x58,0x6F,0x76,0x80,0x97,0xA5,0xA8,0xB5,0xC6,0x62,0x63,0x68,0x6F,0x72,0x78,0x7F,0x7F,0x00,0x02,0x08,0x0D,0x0C,0x0E,0x0F,0x10,0x24,0x36,0x48,0x4A,0x58,0x6F,0x78,0x82,0x99,0xA4,0xA0,0xB1,0xC0,0x5E,0x5E,0x64,0x6B,0x6C,0x73,0x7F,0x7F}, 58, 0},
    {0xCC, (uint8_t[]){0x0B}, 1, 0},
    {0xC0, (uint8_t[]){0x1F,0x73}, 2, 0},
    {0xB6, (uint8_t[]){0x6B,0x6B}, 2, 0},
    {0xD4, (uint8_t[]){0x02}, 1, 0},
    {0xBD, (uint8_t[]){0x01}, 1, 0},
    {0xB1, (uint8_t[]){0x00}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0},
    {0xBF, (uint8_t[]){0x40,0x81,0x50,0x00,0x1A,0xFC,0x01}, 7, 0},
    {0x3A, (uint8_t[]){0x50}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 0, 200},
    {0xB2, (uint8_t[]){0x00,0x80,0x64,0x0C,0x06,0x2F,0x00,0x00,0x00,0x00,0xC0,0x18}, 12, 0},
    {0x29, (uint8_t[]){0x00}, 0, 80},
};

static esp_err_t panel_hx8394_del(esp_lcd_panel_t *panel)
{
    hx8394_panel_t *hx8394 = (hx8394_panel_t *)panel->user_data;

    if (hx8394->reset_gpio_num >= 0)
    {
        gpio_reset_pin(hx8394->reset_gpio_num);
    }
    // Delete MIPI DPI panel
    hx8394->del(panel);
    ESP_LOGD(TAG, "del hx8394 panel @%p", hx8394);
    free(hx8394);

    return ESP_OK;
}

static esp_err_t panel_hx8394_init(esp_lcd_panel_t *panel)
{
    hx8394_panel_t *hx8394 = (hx8394_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = hx8394->io;
    const hx8394_lcd_init_cmd_t *init_cmds = NULL;
    uint16_t init_cmds_size = 0;
    uint8_t lane_command = HX8394_DSI_2_LANE;
    bool is_cmd_overwritten = false;

    switch (hx8394->lane_num)
    {
    case 0:
        lane_command = HX8394_DSI_2_LANE;
        break;
    case 1:
        lane_command = HX8394_DSI_1_LANE;
        break;
    case 2:
        lane_command = HX8394_DSI_2_LANE;
        break;
    case 3:
        lane_command = HX8394_DSI_3_LANE;
        break;
    case 4:
        lane_command = HX8394_DSI_4_LANE;
        break;
    default:
        ESP_LOGE(TAG, "Invalid lane number %d", hx8394->lane_num);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0), TAG,
                        "io tx param failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){
                                                                          hx8394->madctl_val,
                                                                      },
                                                  1),
                        TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, (uint8_t[]){
                                                                          hx8394->colmod_val,
                                                                      },
                                                  1),
                        TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, HX8394_CMD_DSI_INT0, (uint8_t[]){
                                                                               lane_command,
                                                                           },
                                                  1),
                        TAG, "send command failed");

    // vendor specific initialization, it can be different between manufacturers
    // should consult the LCD supplier for initialization sequence code
    if (hx8394->init_cmds)
    {
        init_cmds = hx8394->init_cmds;
        init_cmds_size = hx8394->init_cmds_size;
    }
    else
    {
        init_cmds = vendor_specific_init_code_default;
        init_cmds_size = sizeof(vendor_specific_init_code_default) / sizeof(hx8394_lcd_init_cmd_t);
    }

    for (int i = 0; i < init_cmds_size; i++)
    {
        // Check if the command has been used or conflicts with the internal
        if (init_cmds[i].data_bytes > 0)
        {
            switch (init_cmds[i].cmd)
            {
            case LCD_CMD_MADCTL:
                is_cmd_overwritten = true;
                hx8394->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
                break;
            case LCD_CMD_COLMOD:
                is_cmd_overwritten = true;
                hx8394->colmod_val = ((uint8_t *)init_cmds[i].data)[0];
                break;
            default:
                is_cmd_overwritten = false;
                break;
            }

            if (is_cmd_overwritten)
            {
                is_cmd_overwritten = false;
                ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence",
                         init_cmds[i].cmd);
            }
        }

        // Send command
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }

    ESP_LOGD(TAG, "send init commands success");

    ESP_RETURN_ON_ERROR(hx8394->init(panel), TAG, "init MIPI DPI panel failed");

    return ESP_OK;
}

static esp_err_t panel_hx8394_reset(esp_lcd_panel_t *panel)
{
    hx8394_panel_t *hx8394 = (hx8394_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = hx8394->io;

    // Perform hardware reset
    if (hx8394->reset_gpio_num >= 0)
    {
        gpio_set_level(hx8394->reset_gpio_num, hx8394->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(hx8394->reset_gpio_num, !hx8394->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    else if (io)
    { // Perform software reset
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    return ESP_OK;
}

static esp_err_t panel_hx8394_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    hx8394_panel_t *hx8394 = (hx8394_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = hx8394->io;
    uint8_t command = 0;

    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_STATE, TAG, "invalid panel IO");

    if (invert_color_data)
    {
        command = LCD_CMD_INVON;
    }
    else
    {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");

    return ESP_OK;
}

static esp_err_t panel_hx8394_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    hx8394_panel_t *hx8394 = (hx8394_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = hx8394->io;
    int command = 0;

    if (on_off)
    {
        command = LCD_CMD_DISPON;
    }
    else
    {
        command = LCD_CMD_DISPOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}
#endif
#endif  // defined(DEVICE_WAVESHARE_TOUCH_LCD_5)
