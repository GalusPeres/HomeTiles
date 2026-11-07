#include "src/devices/waveshare_s3_touch_lcd_7/device_waveshare_s3_touch_lcd_7.h"

#include <Arduino.h>
#include <LittleFS.h>
#include "waveshare_lcd_port.h"

#include <Wire.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_private/periph_ctrl.h>
#include <hal/lcd_ll.h>

#include <algorithm>
#include <vector>

namespace DeviceWaveshareS3TouchLCD7 {

    static esp_lcd_panel_handle_t g_panel = nullptr;
    static uint8_t g_brightness = 255;
    static bool g_littlefs_ready = false;

    // Touch: GT911 on the CH422G/I2C0 bus (SDA 8, SCL 9). The official
    // Waveshare v9 demo uses the same bus and address 0x5D, and maps the
    // controller coordinates 1:1 onto the native 800x480 panel.
    static constexpr int8_t kTouchSda = 8;
    static constexpr int8_t kTouchScl = 9;
    static constexpr uint32_t kTouchFrequency = 400000;
    static constexpr uint8_t kTouchAddressPrimary = 0x5D;
    static constexpr uint8_t kTouchAddressAlternate = 0x14;
    static constexpr uint16_t kTouchProductIdRegister = 0x8140;
    static constexpr uint16_t kTouchStatusRegister = 0x814E;
    static constexpr uint16_t kTouchPointRegister = 0x814F;
    // The GT911 reset line is not a GPIO on this board. It is EXIO1 of the
    // CH422G IO expander, and the I2C address is latched on the rising edge of
    // that reset from the INT line (low -> 0x5D, high -> 0x14). The vendor
    // board configuration drives INT low during the reset pulse.
    static constexpr int8_t kTouchIntPin = 4;
    // CH422G is register-less and write-only for the outputs: each function is
    // a single data byte written to its own 7-bit address.
    static constexpr uint8_t kCh422gWriteSetAddress = 0x48 >> 1;  // WR_SET
    static constexpr uint8_t kCh422gWriteIoAddress = 0x70 >> 1;   // WR_IO
    static constexpr uint8_t kCh422gOutputsEnabled = 1 << 0;      // IO_OE
    static constexpr uint8_t kCh422gPinTouchReset = 1 << 1;       // EXIO1
    static constexpr uint8_t kCh422gOutputsIdle = 0xFF;
    // Keep a held point across normal GT911 "no new frame" polls, but do not
    // let a broken I2C/data path leave LVGL pressed forever. At the 8 ms input
    // period, 16 consecutive real errors give the bus roughly 128 ms to recover.
    static constexpr uint8_t kTouchErrorReleaseThreshold = 16;

    // The GT911 reset sequence drives the CH422G outputs (TP_RST, LCD_BL,
    // LCD_RST) and retries on every poll while no GT911 answers, which blocks
    // the loop for roughly 300 ms each time; the error-release threshold below
    // bounds a stuck press.
    static constexpr bool kTouchEnabled = true;
    static bool g_touch_ready = false;
    static uint8_t g_touch_address = 0;
    static bool g_touch_active = false;
    static int16_t g_touch_last_x = 0;
    static int16_t g_touch_last_y = 0;
    static uint8_t g_touch_status_error_streak = 0;
    static uint8_t g_touch_point_error_streak = 0;


    // Vendor RGB pin map and timings. This profile drives the RGB peripheral
    // directly instead of through ESP32_Display_Panel: that library depends on
    // ESP32_IO_Expander, whose port sources install the legacy I2C driver and
    // abort at boot because the touch bus uses the new driver. The direct
    // configuration matches what the library built for this panel (one
    // framebuffer in PSRAM, 64-byte DMA bursts) plus the vendor timing flags.
    static bool create_waveshare_panel()
    {
        esp_lcd_rgb_panel_config_t config{};
        config.clk_src = LCD_CLK_SRC_DEFAULT;
        config.timings.pclk_hz = EXAMPLE_LCD_RGB_TIMING_FREQ_HZ;
        config.timings.h_res = EXAMPLE_LCD_WIDTH;
        config.timings.v_res = EXAMPLE_LCD_HEIGHT;
        config.timings.hsync_pulse_width = EXAMPLE_LCD_RGB_TIMING_HPW;
        config.timings.hsync_back_porch = EXAMPLE_LCD_RGB_TIMING_HBP;
        config.timings.hsync_front_porch = EXAMPLE_LCD_RGB_TIMING_HFP;
        config.timings.vsync_pulse_width = EXAMPLE_LCD_RGB_TIMING_VPW;
        config.timings.vsync_back_porch = EXAMPLE_LCD_RGB_TIMING_VBP;
        config.timings.vsync_front_porch = EXAMPLE_LCD_RGB_TIMING_VFP;
        config.timings.flags.hsync_idle_low = 0;
        config.timings.flags.vsync_idle_low = 0;
        config.timings.flags.de_idle_high = 0;
        // The panel latches data on the rising PCLK edge, so the peripheral
        // must launch it on the falling edge.
        config.timings.flags.pclk_active_neg = 1;
        config.timings.flags.pclk_idle_high = 0;
        config.data_width = 16;
        config.bits_per_pixel = 16;
        config.num_fbs = 1;
        config.bounce_buffer_size_px = EXAMPLE_LCD_RGB_BOUNCE_BUFFER_SIZE;
        config.dma_burst_size = 64;
        config.hsync_gpio_num = EXAMPLE_LCD_RGB_IO_HSYNC;
        config.vsync_gpio_num = EXAMPLE_LCD_RGB_IO_VSYNC;
        config.de_gpio_num = EXAMPLE_LCD_RGB_IO_DE;
        config.pclk_gpio_num = EXAMPLE_LCD_RGB_IO_PCLK;
        config.disp_gpio_num = EXAMPLE_LCD_RGB_IO_DISP;
        const int data_pins[16] = {
            EXAMPLE_LCD_RGB_IO_DATA0,
            EXAMPLE_LCD_RGB_IO_DATA1,
            EXAMPLE_LCD_RGB_IO_DATA2,
            EXAMPLE_LCD_RGB_IO_DATA3,
            EXAMPLE_LCD_RGB_IO_DATA4,
            EXAMPLE_LCD_RGB_IO_DATA5,
            EXAMPLE_LCD_RGB_IO_DATA6,
            EXAMPLE_LCD_RGB_IO_DATA7,
            EXAMPLE_LCD_RGB_IO_DATA8,
            EXAMPLE_LCD_RGB_IO_DATA9,
            EXAMPLE_LCD_RGB_IO_DATA10,
            EXAMPLE_LCD_RGB_IO_DATA11,
            EXAMPLE_LCD_RGB_IO_DATA12,
            EXAMPLE_LCD_RGB_IO_DATA13,
            EXAMPLE_LCD_RGB_IO_DATA14,
            EXAMPLE_LCD_RGB_IO_DATA15,
        };
        std::copy(std::begin(data_pins), std::end(data_pins),
                  config.data_gpio_nums);
        config.flags.disp_active_low = false;
        config.flags.refresh_on_demand = false;
        config.flags.fb_in_psram = true;
        config.flags.double_fb = false;
        config.flags.no_fb = false;
        config.flags.bb_invalidate_cache = false;

        Serial.printf(
            "[Device/Waveshare ESP32-S3-Touch-LCD-7] LCD config: "
            "PCLK=%u MHz, bounce=%u px (%u lines), pclk_active_neg=1\n",
            static_cast<unsigned>(EXAMPLE_LCD_RGB_TIMING_FREQ_HZ / 1000000),
            static_cast<unsigned>(EXAMPLE_LCD_RGB_BOUNCE_BUFFER_SIZE),
            static_cast<unsigned>(
                EXAMPLE_LCD_RGB_BOUNCE_BUFFER_SIZE / EXAMPLE_LCD_WIDTH));

        esp_err_t err = esp_lcd_new_rgb_panel(&config, &g_panel);
        if (err != ESP_OK) {
            Serial.printf(
                "[Device/Waveshare ESP32-S3-Touch-LCD-7] "
                "esp_lcd_new_rgb_panel failed: %s\n",
                esp_err_to_name(err));
            g_panel = nullptr;
            return false;
        }

        err = esp_lcd_panel_reset(g_panel);
        if (err != ESP_OK) {
            Serial.printf(
                "[Device/Waveshare ESP32-S3-Touch-LCD-7] "
                "esp_lcd_panel_reset failed: %s\n",
                esp_err_to_name(err));
            esp_lcd_panel_del(g_panel);
            g_panel = nullptr;
            return false;
        }

        err = esp_lcd_panel_init(g_panel);
        if (err != ESP_OK) {
            Serial.printf(
                "[Device/Waveshare ESP32-S3-Touch-LCD-7] "
                "esp_lcd_panel_init failed: %s\n",
                esp_err_to_name(err));
            esp_lcd_panel_del(g_panel);
            g_panel = nullptr;
            return false;
        }

        return true;
    }


    // Arduino-ESP32 3.3.7 ships ESP-IDF 5.5.2 with
    // CONFIG_LCD_RGB_RESTART_IN_VSYNC=y. In bounce-buffer mode that path
    // restarts GDMA from bounce buffer 0 on every VSYNC without resetting the
    // bounce-buffer index, so a single late EOF leaves the refills permanently
    // one buffer out of phase: the picture flickers and horizontal bands of
    // stale pixels appear (ESP-IDF issue IDFGH-18263, fixed after 5.5.2).
    // The vendor BSP runs with that restart disabled. Masking the VSYNC
    // interrupt, as the Waveshare S3 4/4B profiles do, removes the restart
    // while the GDMA EOF interrupt keeps refilling the bounce buffers.
    static void disableVsyncRestart()
    {
        PERIPH_RCC_ATOMIC() {
            lcd_ll_enable_interrupt(&LCD_CAM, LCD_LL_EVENT_RGB, false);
            lcd_ll_clear_interrupt_status(&LCD_CAM, LCD_LL_EVENT_RGB);
        }
    }


    bool init()
    {
        if (g_panel) {
            return true;
        }

        Serial.println(
            "[Device/Waveshare ESP32-S3-Touch-LCD-7] Initialising board...");

        if (!create_waveshare_panel()) {
            return false;
        }

        disableVsyncRestart();

        // The ST7262 reports display on/off support; without a DISP GPIO this
        // is a no-op on the RGB peripheral.
        esp_lcd_panel_disp_on_off(g_panel, true);

        Serial.println(
            "[Device/Waveshare ESP32-S3-Touch-LCD-7] LCD ready; "
            "VSYNC restart disabled");

        if (!initLittleFS()) {
            Serial.println(
                "[Device/Waveshare ESP32-S3-Touch-LCD-7] LittleFS init failed");
        }

        return true;
    }


    void update()
    {
    }


    void displayPushPixels(
        int32_t x,
        int32_t y,
        int32_t w,
        int32_t h,
        const uint16_t* data)
    {
        if (!g_panel || !data || w <= 0 || h <= 0 ||
            x < 0 || y < 0 ||
            x + w > EXAMPLE_LCD_WIDTH ||
            y + h > EXAMPLE_LCD_HEIGHT) {
            return;
        }

        esp_lcd_panel_draw_bitmap(g_panel, x, y, x + w, y + h, data);
    }


    void displayPushPixelsDMA(
        int32_t x,
        int32_t y,
        int32_t w,
        int32_t h,
        const uint16_t* data)
    {
        displayPushPixels(x, y, w, h, data);
    }


    bool displayTryFullFramePreview(
        int32_t,
        int32_t,
        int32_t,
        int32_t,
        int32_t,
        const uint16_t*,
        size_t,
        bool)
    {
        return false;
    }


    bool displayBeginAtomicFrame(const char*)
    {
        return false;
    }


    void displayWaitDMA()
    {
    }


    void displayFillScreen(uint16_t color)
    {
        if (!g_panel) {
            return;
        }

        std::vector<uint16_t> line(EXAMPLE_LCD_WIDTH, color);

        for (int y = 0; y < EXAMPLE_LCD_HEIGHT; ++y) {
            esp_lcd_panel_draw_bitmap(
                g_panel,
                0,
                y,
                EXAMPLE_LCD_WIDTH,
                y + 1,
                line.data()
            );
        }
    }


    void displaySetRotation(uint8_t)
    {
        // Rotation not implemented yet.
    }


    void setBrightness(uint8_t value)
    {
        g_brightness = value;
    }


    uint8_t getBrightness()
    {
        return g_brightness;
    }


    // The CH422G is register-less: one data byte per function, written to a
    // distinct 7-bit address. Unlike the library helper, always send the full
    // byte, including zero bits, because a skipped write would leave the
    // outputs unchanged.
    static bool writeCh422gByte(uint8_t address, uint8_t value)
    {
        Wire.beginTransmission(address);
        Wire.write(value);
        return Wire.endTransmission() == 0;
    }


    // Vendor touch reset sequence: INT low, 10 ms, TP_RST low, 100 ms, TP_RST
    // high, 200 ms, release INT. The INT level at the rising reset edge selects
    // the GT911 address, and keeping it low selects 0x5D.
    static void resetTouchController()
    {
        // Keep every other CH422G output (LCD_RST, backlight, SD_CS, USB_SEL) at
        // the idle high level that already drives the working display.
        const bool enabled = writeCh422gByte(
            kCh422gWriteSetAddress, kCh422gOutputsEnabled);
        const bool idle = writeCh422gByte(
            kCh422gWriteIoAddress, kCh422gOutputsIdle);
        if (!enabled || !idle) {
            Serial.println(
                "[Device/Waveshare ESP32-S3-Touch-LCD-7] "
                "touch: CH422G write failed");
        }

        pinMode(kTouchIntPin, OUTPUT);
        digitalWrite(kTouchIntPin, LOW);
        delay(10);

        const bool reset_low = writeCh422gByte(
            kCh422gWriteIoAddress,
            static_cast<uint8_t>(kCh422gOutputsIdle & ~kCh422gPinTouchReset));
        delay(100);
        const bool reset_high = writeCh422gByte(
            kCh422gWriteIoAddress, kCh422gOutputsIdle);
        delay(200);

        pinMode(kTouchIntPin, INPUT);
        if (!reset_low || !reset_high) {
            Serial.println(
                "[Device/Waveshare ESP32-S3-Touch-LCD-7] "
                "touch: GT911 reset pulse failed");
        }
    }


    static bool writeTouchRegister(uint16_t reg, uint8_t value)
    {
        if (!g_touch_address) {
            return false;
        }

        Wire.beginTransmission(g_touch_address);
        Wire.write(static_cast<uint8_t>(reg >> 8));
        Wire.write(static_cast<uint8_t>(reg & 0xFF));
        Wire.write(value);
        return Wire.endTransmission() == 0;
    }


    static bool readTouchRegisters(uint16_t reg, uint8_t* data, size_t len)
    {
        if (!g_touch_address || !data || len == 0 || len > 32) {
            return false;
        }

        Wire.beginTransmission(g_touch_address);
        Wire.write(static_cast<uint8_t>(reg >> 8));
        Wire.write(static_cast<uint8_t>(reg & 0xFF));
        if (Wire.endTransmission(false) != 0) {
            return false;
        }

        const int received =
            Wire.requestFrom(static_cast<int>(g_touch_address),
                             static_cast<int>(len));
        if (received != static_cast<int>(len)) {
            while (Wire.available()) {
                Wire.read();
            }
            return false;
        }

        for (size_t i = 0; i < len; ++i) {
            data[i] = static_cast<uint8_t>(Wire.read());
        }
        return true;
    }


    static bool probeTouchAddress(uint8_t address)
    {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() != 0) {
            return false;
        }

        g_touch_address = address;
        uint8_t product_id[4] = {};
        if (!readTouchRegisters(kTouchProductIdRegister, product_id,
                                sizeof(product_id))) {
            g_touch_address = 0;
            return false;
        }

        Serial.printf(
            "[Device/Waveshare ESP32-S3-Touch-LCD-7] "
            "touch: GT911 at 0x%02X, id=%c%c%c%c\n",
            static_cast<unsigned>(address),
            product_id[0],
            product_id[1],
            product_id[2],
            product_id[3]
        );
        return true;
    }


    static bool initTouch()
    {
        if (!kTouchEnabled) {
            return false;
        }
        if (g_touch_ready) {
            return true;
        }

        Wire.begin(kTouchSda, kTouchScl, kTouchFrequency);
        delay(100);
        resetTouchController();

        if (!probeTouchAddress(kTouchAddressPrimary) &&
            !probeTouchAddress(kTouchAddressAlternate)) {
            Serial.println(
                "[Device/Waveshare ESP32-S3-Touch-LCD-7] "
                "touch: GT911 not found at 0x5D/0x14");
            return false;
        }

        writeTouchRegister(kTouchStatusRegister, 0);
        g_touch_active = false;
        g_touch_ready = true;
        Serial.println(
            "[Device/Waveshare ESP32-S3-Touch-LCD-7] touch: ready");
        return true;
    }


    static void mapTouch(uint16_t raw_x, uint16_t raw_y, int16_t& x, int16_t& y)
    {
        // displaySetRotation() is a no-op, so the controller coordinates are
        // already in the panel's native 800x480 space.
        x = static_cast<int16_t>(std::min<uint16_t>(
            raw_x, static_cast<uint16_t>(EXAMPLE_LCD_WIDTH - 1)));
        y = static_cast<int16_t>(std::min<uint16_t>(
            raw_y, static_cast<uint16_t>(EXAMPLE_LCD_HEIGHT - 1)));
    }


    bool getTouch(int16_t& x, int16_t& y)
    {
        if (!g_touch_ready && !initTouch()) {
            return false;
        }

        const auto return_held_point = [&]() {
            if (!g_touch_active) {
                return false;
            }
            x = g_touch_last_x;
            y = g_touch_last_y;
            return true;
        };
        const auto return_held_or_fail_safe = [&](uint8_t& error_streak) {
            if (error_streak < UINT8_MAX) {
                ++error_streak;
            }
            if (!g_touch_active || error_streak < kTouchErrorReleaseThreshold) {
                return return_held_point();
            }
            g_touch_active = false;
            g_touch_status_error_streak = 0;
            g_touch_point_error_streak = 0;
            return false;
        };

        uint8_t status = 0;
        if (!readTouchRegisters(kTouchStatusRegister, &status, 1)) {
            return return_held_or_fail_safe(g_touch_status_error_streak);
        }
        g_touch_status_error_streak = 0;
        if ((status & 0x80) == 0) {
            // Bit 7 means "new coordinate frame ready", not "finger down".
            return return_held_point();
        }

        const uint8_t points = status & 0x0F;
        if (points == 0) {
            writeTouchRegister(kTouchStatusRegister, 0);
            g_touch_active = false;
            g_touch_point_error_streak = 0;
            return false;
        }
        if (points > 5) {
            writeTouchRegister(kTouchStatusRegister, 0);
            return return_held_or_fail_safe(g_touch_point_error_streak);
        }

        uint8_t point[8] = {};
        const bool read_ok =
            readTouchRegisters(kTouchPointRegister, point, sizeof(point));
        writeTouchRegister(kTouchStatusRegister, 0);
        if (!read_ok) {
            return return_held_or_fail_safe(g_touch_point_error_streak);
        }

        const uint16_t raw_x =
            static_cast<uint16_t>(point[1] | (point[2] << 8));
        const uint16_t raw_y =
            static_cast<uint16_t>(point[3] | (point[4] << 8));
        if (raw_x >= EXAMPLE_LCD_WIDTH || raw_y >= EXAMPLE_LCD_HEIGHT) {
            return return_held_or_fail_safe(g_touch_point_error_streak);
        }

        g_touch_point_error_streak = 0;
        mapTouch(raw_x, raw_y, x, y);
        g_touch_last_x = x;
        g_touch_last_y = y;
        g_touch_active = true;
        return true;
    }


    void displaySleep()
    {
    }


    void displayWake()
    {
    }


    void displayWakeDark()
    {
    }


    void displayPowerSaveOn()
    {
    }


    void displayPowerSaveOff()
    {
    }


    void displayWaitDisplay()
    {
    }


    void prepareForRestart()
    {
    }


    bool initSDCard()
    {
        return false;
    }


    bool storageReady()
    {
        return g_littlefs_ready;
    }


    fs::FS& storageFS()
    {
        return LittleFS;
    }


    void storageWriteBegin()
    {
    }


    void storageWriteEnd()
    {
    }


    bool sdReady()
    {
        return false;
    }


    fs::FS& sdFS()
    {
        return LittleFS;
    }


    bool suspendSDCardForNetworkTransition()
    {
        return false;
    }


    bool resumeSDCardAfterNetworkTransition()
    {
        return false;
    }


    bool initLittleFS()
    {
        if (g_littlefs_ready) {
            return true;
        }

        g_littlefs_ready = LittleFS.begin(
            true,
            "/littlefs",
            10,
            "spiffs"
        );

        return g_littlefs_ready;
    }


    void migrateStorageFromSD()
    {
    }

} // namespace DeviceWaveshareS3TouchLCD7
