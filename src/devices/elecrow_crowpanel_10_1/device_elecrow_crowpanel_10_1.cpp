#include "src/devices/elecrow_crowpanel_10_1/device_elecrow_crowpanel_10_1.h"

#include "src/core/display/esp_idf_dsi/display_driver_dsi.h"
#include "src/core/display/display_flush.h"
#include "src/core/display/ppa/ppa_client.h"
#include "src/devices/device_select.h"
#include "src/io/storage/sd_card.h"

#if defined(DEVICE_ELECROW_CROWPANEL_10_1)

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace DeviceElecrowCrowPanel10 {

static const char* TAG = "Elecrow10";
static i2c_master_bus_handle_t s_i2c_bus = nullptr;
static esp_lcd_panel_handle_t s_panel_handle = nullptr;
static bool s_initialized = false;
static uint8_t s_brightness = 255;

static constexpr int TOUCH_I2C_NUM = 0;
static constexpr int TOUCH_SDA_PIN = 7;
static constexpr int TOUCH_SCL_PIN = 8;
static constexpr int TOUCH_INT_PIN = 9;
static constexpr int BACKLIGHT_PIN = 26;

static constexpr int LCD_H_RES = 1024;
static constexpr int LCD_V_RES = 600;

static esp_lcd_panel_handle_t init_dsi_display() {
    ESP_LOGI(TAG, "Initializing MIPI-DSI display 1024x600");

    esp_lcd_panel_handle_t panel_handle = nullptr;

    esp_lcd_dsi_bus_handle_t dsi_bus = ESP_LCD_DSI_BUS_DEFAULT();
    dsi_bus.num_data_lanes = 2;
    dsi_bus.phy_clk_src = ESP_LCD_DSI_PHY_CLK_SRC_PLL;
    dsi_bus.lp_clk_freq_hz = 20000000;

    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&dsi_bus, &dsi_bus));

    esp_lcd_dsi_panel_handle_t dsi_panel = ESP_LCD_DSI_PANEL_DEFAULT();
    dsi_panel.width = LCD_H_RES;
    dsi_panel.height = LCD_V_RES;

    ESP_ERROR_CHECK(esp_lcd_new_panel_dsi(dsi_bus, &dsi_panel, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));

    return panel_handle;
}

static esp_err_t init_i2c_bus() {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = (i2c_port_t)TOUCH_I2C_NUM,
        .sda_io_num = TOUCH_SDA_PIN,
        .scl_io_num = TOUCH_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    return i2c_new_master_bus(&bus_config, &s_i2c_bus);
}

bool init() {
    if (s_initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing Elecrow CrowPanel Advanced 10.1");

    esp_err_t ret = init_i2c_bus();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        return false;
    }

    s_panel_handle = init_dsi_display();
    if (!s_panel_handle) {
        ESP_LOGE(TAG, "Display init failed");
        return false;
    }

    gpio_config_t bl_conf = {
        .pin_bit_mask = (1ULL << BACKLIGHT_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_conf);
    gpio_set_level(BACKLIGHT_PIN, 0);

    s_initialized = true;
    ESP_LOGI(TAG, "Initialization complete");
    return true;
}

void update() {
    if (s_initialized) {
        DisplayDriverDSI::update();
    }
}

void displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                       const uint16_t* data) {
    if (!s_initialized || !s_panel_handle) return;
    DisplayDriverDSI::push_pixels(x, y, w, h, data);
}

void displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                          const uint16_t* data) {
    displayPushPixels(x, y, w, h, data);
}

bool displayTryFullFramePreview(int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t source_stride,
                                const uint16_t* data, size_t data_size,
                                bool byte_swap) {
    return false;
}

void displayEndFullFramePreview() {}
bool displayFullFrameInfo(uint16_t& width, uint16_t& height, uint16_t& turn_cw) {
    return false;
}
bool displayBeginFullFrames() { return false; }
uint16_t* displayAcquireFullFrame(size_t& bytes) { return nullptr; }
bool displaySubmitFullFrame() { return false; }
void displayEndFullFrames() {}

void displayWaitDMA() {}
void displayWaitDisplayStart() {}
void displayCommit() {}

void displayFillScreen(uint16_t color) {
    if (!s_initialized || !s_panel_handle) return;
    esp_lcd_panel_fill_window(s_panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, color);
}

void displaySetRotation(uint8_t rotation) {
    if (!s_initialized || !s_panel_handle) return;
    bool mirror_x = false;
    bool mirror_y = false;
    bool swap_xy = false;

    switch (rotation) {
        case 1: mirror_x = true; mirror_y = true; break;
        case 2: swap_xy = true; mirror_x = true; break;
        case 3: swap_xy = true; mirror_y = true; break;
    }

    esp_lcd_panel_swap_xy(s_panel_handle, swap_xy);
    esp_lcd_panel_mirror(s_panel_handle, mirror_x, mirror_y);
}

void displaySetUpright(bool upright) {
    (void)upright;
}

void pausePpaFor(uint32_t duration_ms) {
    (void)duration_ms;
}

bool ppaCooldownActive() {
    return false;
}

void setBrightness(uint8_t value) {
    s_brightness = value;
    if (!s_initialized) return;
    uint32_t duty = (value * 1023) / 255;
    gpio_set_level(BACKLIGHT_PIN, duty > 511 ? 1 : 0);
}

uint8_t getBrightness() {
    return s_brightness;
}

bool getTouch(int16_t& x, int16_t& y) {
    (void)x;
    (void)y;
    return false;
}

void displaySleep() {
    if (!s_initialized || !s_panel_handle) return;
    esp_lcd_panel_disp_off(s_panel_handle, true);
}

void displayWake() {
    if (!s_initialized || !s_panel_handle) return;
    esp_lcd_panel_disp_off(s_panel_handle, false);
}

void displayWakeDark() {}
void displayPowerSaveOn() {}
void displayPowerSaveOff() {}
void displayWaitDisplay() {}

void prepareForRestart() {}

i2c_master_bus_handle_t sharedI2cBus() {
    return s_i2c_bus;
}

bool initSDCard() {
    return Storage::initSDCard();
}

bool storageReady() {
    return Storage::storageReady();
}

fs::FS& storageFS() {
    return Storage::storageFS();
}

bool sdReady() {
    return Storage::sdReady();
}

fs::FS& sdFS() {
    return Storage::sdFS();
}

bool suspendSDCardForNetworkTransition() {
    return Storage::suspendSDCardForNetworkTransition();
}

bool resumeSDCardAfterNetworkTransition() {
    return Storage::resumeSDCardAfterNetworkTransition();
}

bool initLittleFS() {
    return Storage::initLittleFS();
}

void migrateStorageFromSD() {
    Storage::migrateStorageFromSD();
}

}  // namespace DeviceElecrowCrowPanel10

#endif
