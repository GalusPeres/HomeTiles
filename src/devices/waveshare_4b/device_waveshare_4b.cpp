#include "src/devices/waveshare_4b/device_waveshare_4b.h"

#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_4B)

#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_ldo_regulator.h>
#include <string.h>

#include "src/devices/waveshare_4b/waveshare_sdmmc.h"
#include "src/devices/waveshare_4b/vendor/displays_config.h"
#include "src/devices/waveshare_4b/vendor/gt911.h"

namespace {

constexpr gpio_num_t kBacklightPin = GPIO_NUM_26;
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_1;
constexpr uint32_t kBacklightFreq = 5000;
constexpr ledc_timer_bit_t kBacklightBits = LEDC_TIMER_10_BIT;
constexpr bool kBacklightActiveLow = true;
constexpr int kMipiPhyLdoChannel = 3;
constexpr int kMipiPhyLdoVoltageMv = 2500;
constexpr int kPanelPowerLdoChannel = 4;
constexpr int kPanelPowerLdoVoltageMv = 3300;

Arduino_ESP32DSIPanel* g_dsi_panel = nullptr;
Arduino_DSI_Display* g_gfx = nullptr;
esp_lcd_touch_handle_t g_touch = nullptr;
i2c_master_bus_handle_t g_i2c_bus = nullptr;
esp_ldo_channel_handle_t g_panel_power_ldo = nullptr;

uint8_t g_brightness = 150;
bool g_backlight_ready = false;
bool g_panel_power_cycled = false;
bool g_sd_init_attempted = false;
bool g_sd_available = false;
uint32_t g_sd_retry_tick_ms = 0;
bool g_littlefs_ready = false;

void apply_backlight(uint8_t value);

void hold_panel_reset_low() {
  if (display_cfg.lcd_rst < 0) {
    return;
  }

  const gpio_num_t rst_pin = static_cast<gpio_num_t>(display_cfg.lcd_rst);
  gpio_set_direction(rst_pin, GPIO_MODE_OUTPUT);
  gpio_set_level(rst_pin, 0);
}

void pulse_panel_reset() {
  if (display_cfg.lcd_rst < 0) {
    return;
  }

  const gpio_num_t rst_pin = static_cast<gpio_num_t>(display_cfg.lcd_rst);
  gpio_set_direction(rst_pin, GPIO_MODE_OUTPUT);
  gpio_set_level(rst_pin, 0);
  gpio_hold_dis(rst_pin);
  delay(120);
  gpio_set_level(rst_pin, 1);
  delay(300);
}

bool acquire_panel_power() {
  if (!g_panel_power_ldo) {
    esp_ldo_channel_config_t ldo_cfg = {};
    ldo_cfg.chan_id = kPanelPowerLdoChannel;
    ldo_cfg.voltage_mv = kPanelPowerLdoVoltageMv;
    const esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &g_panel_power_ldo);
    if (err != ESP_OK) {
      Serial.printf("[Device/Waveshare4B] Panel LDO init failed: %s (0x%x)\n",
                    esp_err_to_name(err),
                    static_cast<unsigned>(err));
      return false;
    }
    Serial.println("[Device/Waveshare4B] Panel LDO OK");
  }
  return true;
}

bool power_cycle_mipi_phy() {
  esp_ldo_channel_handle_t phy_ldo = nullptr;
  esp_ldo_channel_config_t ldo_cfg = {};
  ldo_cfg.chan_id = kMipiPhyLdoChannel;
  ldo_cfg.voltage_mv = kMipiPhyLdoVoltageMv;

  const esp_err_t acquire_err = esp_ldo_acquire_channel(&ldo_cfg, &phy_ldo);
  if (acquire_err != ESP_OK) {
    Serial.printf("[Device/Waveshare4B] MIPI PHY LDO cycle skipped: %s (0x%x)\n",
                  esp_err_to_name(acquire_err),
                  static_cast<unsigned>(acquire_err));
    return false;
  }

  Serial.println("[Device/Waveshare4B] MIPI PHY power cycle");
  const esp_err_t release_err = esp_ldo_release_channel(phy_ldo);
  if (release_err != ESP_OK) {
    Serial.printf("[Device/Waveshare4B] MIPI PHY LDO release failed: %s (0x%x)\n",
                  esp_err_to_name(release_err),
                  static_cast<unsigned>(release_err));
    return false;
  }

  delay(150);
  return true;
}

bool power_cycle_panel() {
  hold_panel_reset_low();
  apply_backlight(0);

  if (!acquire_panel_power()) {
    return false;
  }

  Serial.println("[Device/Waveshare4B] Panel power cycle");
  const esp_err_t release_err = esp_ldo_release_channel(g_panel_power_ldo);
  if (release_err != ESP_OK) {
    Serial.printf("[Device/Waveshare4B] Panel LDO release failed: %s (0x%x)\n",
                  esp_err_to_name(release_err),
                  static_cast<unsigned>(release_err));
  }
  g_panel_power_ldo = nullptr;

  delay(350);

  if (!acquire_panel_power()) {
    return false;
  }

  delay(150);
  g_panel_power_cycled = true;
  return true;
}

bool init_display_power() {
  power_cycle_mipi_phy();

  if (!g_panel_power_cycled && !power_cycle_panel()) {
    return false;
  }

  if (!acquire_panel_power()) {
    return false;
  }

  delay(50);
  return true;
}

void apply_backlight(uint8_t value) {
  if (!g_backlight_ready) {
    return;
  }

  constexpr uint32_t kMaxDuty = (1u << 10) - 1u;
  uint32_t duty = ((uint32_t)value * kMaxDuty + 127u) / 255u;
  if (kBacklightActiveLow) {
    duty = kMaxDuty - duty;
  }

  ledc_set_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel);
}

bool init_backlight() {
  if (g_backlight_ready) {
    return true;
  }

  gpio_set_direction(kBacklightPin, GPIO_MODE_OUTPUT);
  gpio_set_level(kBacklightPin, kBacklightActiveLow ? 1 : 0);

  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_cfg.timer_num = kBacklightTimer;
  timer_cfg.duty_resolution = kBacklightBits;
  timer_cfg.freq_hz = kBacklightFreq;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&timer_cfg) != ESP_OK) {
    Serial.println("[Device/Waveshare4B] Backlight timer init failed");
    return false;
  }

  ledc_channel_config_t ch_cfg = {};
  ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_cfg.channel = kBacklightChannel;
  ch_cfg.timer_sel = kBacklightTimer;
  ch_cfg.gpio_num = kBacklightPin;
  ch_cfg.duty = kBacklightActiveLow ? ((1u << 10) - 1u) : 0;
  ch_cfg.hpoint = 0;
  if (ledc_channel_config(&ch_cfg) != ESP_OK) {
    Serial.println("[Device/Waveshare4B] Backlight channel init failed");
    return false;
  }

  g_backlight_ready = true;
  apply_backlight(0);
  return true;
}

bool init_display() {
  if (g_gfx) {
    return true;
  }

  if (!init_display_power()) {
    return false;
  }
  pulse_panel_reset();

  g_dsi_panel = new Arduino_ESP32DSIPanel(
      display_cfg.hsync_pulse_width,
      display_cfg.hsync_back_porch,
      display_cfg.hsync_front_porch,
      display_cfg.vsync_pulse_width,
      display_cfg.vsync_back_porch,
      display_cfg.vsync_front_porch,
      display_cfg.prefer_speed,
      display_cfg.lane_bit_rate);
  if (!g_dsi_panel) {
    Serial.println("[Device/Waveshare4B] DSI panel allocation failed");
    return false;
  }

  g_gfx = new Arduino_DSI_Display(
      display_cfg.width,
      display_cfg.height,
      g_dsi_panel,
      display_cfg.rotation,
      display_cfg.auto_flush,
      display_cfg.lcd_rst,
      display_cfg.init_cmds,
      display_cfg.init_cmds_size);
  if (!g_gfx) {
    Serial.println("[Device/Waveshare4B] Display allocation failed");
    return false;
  }

  if (!g_gfx->begin()) {
    Serial.println("[Device/Waveshare4B] gfx->begin() failed");
    return false;
  }

  g_gfx->fillScreen(0x0000);
  g_gfx->flush();
  g_gfx->displayOn();
  return true;
}

bool init_touch() {
  if (g_touch) {
    return true;
  }

  DEV_I2C_Port port = DEV_I2C_Init();
  g_i2c_bus = port.bus;
  g_touch = touch_gt911_init(port);
  if (!g_touch) {
    Serial.println("[Device/Waveshare4B] Touch init failed");
    return false;
  }

  return true;
}

}  // namespace

bool DeviceWaveshare4B::init() {
  Serial.println("[Device/Waveshare4B] Initialising board...");

  if (!init_backlight()) {
    return false;
  }
  apply_backlight(0);

  if (!init_display()) {
    return false;
  }
  Serial.println("[Device/Waveshare4B] Display OK");

  if (!init_touch()) {
    return false;
  }
  Serial.println("[Device/Waveshare4B] Touch OK");

  setBrightness(g_brightness);
  Serial.println("[Device/Waveshare4B] Init complete");
  return true;
}

void DeviceWaveshare4B::update() {
}

void DeviceWaveshare4B::displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                                          const uint16_t* data) {
  if (!g_gfx || !data) {
    return;
  }

  g_gfx->draw16bitRGBBitmap((int16_t)x, (int16_t)y,
                            const_cast<uint16_t*>(data),
                            (int16_t)w, (int16_t)h);
}

void DeviceWaveshare4B::displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                                             const uint16_t* data) {
  displayPushPixels(x, y, w, h, data);
}

bool DeviceWaveshare4B::displayTryFullFramePreview(
    int32_t x, int32_t y, int32_t w, int32_t h,
    int32_t source_stride, const uint16_t* data, size_t data_size,
    bool byte_swap) {
  // The B4's logical coordinates map directly to its DSI framebuffer. Copy
  // prepared preview rectangles straight into that framebuffer so camera
  // frames do not trigger a complete LVGL image redraw. The same path still
  // accepts native full-screen images used by the image screensaver.
  if (!g_gfx || !data || x < 0 || y < 0 || w <= 0 || h <= 0 ||
      source_stride != w ||
      (x + w) > display_cfg.width || (y + h) > display_cfg.height ||
      g_gfx->getRotation() != 0) {
    return false;
  }

  const size_t required_bytes =
      static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(uint16_t);
  if (data_size < required_bytes) {
    return false;
  }

  uint16_t* framebuffer = g_gfx->getFramebuffer();
  if (!framebuffer) return false;

  if (x == 0 && y == 0 &&
      w == display_cfg.width && h == display_cfg.height &&
      !byte_swap) {
    if (framebuffer != data) memcpy(framebuffer, data, required_bytes);
    g_gfx->flush(true);
    return true;
  }

  if (byte_swap) {
    g_gfx->draw16bitBeRGBBitmap(
        static_cast<int16_t>(x), static_cast<int16_t>(y),
        const_cast<uint16_t*>(data),
        static_cast<int16_t>(w), static_cast<int16_t>(h));
  } else {
    g_gfx->draw16bitRGBBitmap(
        static_cast<int16_t>(x), static_cast<int16_t>(y),
        const_cast<uint16_t*>(data),
        static_cast<int16_t>(w), static_cast<int16_t>(h));
  }
  return true;
}

void DeviceWaveshare4B::displayWaitDMA() {
}

void DeviceWaveshare4B::displayFillScreen(uint16_t color) {
  if (!g_gfx) {
    return;
  }

  g_gfx->fillScreen(color);
  g_gfx->flush();
}

void DeviceWaveshare4B::displaySetRotation(uint8_t rotation) {
  if (!g_gfx) {
    return;
  }

  g_gfx->setRotation(rotation & 0x03);
  g_gfx->flush();
}

void DeviceWaveshare4B::setBrightness(uint8_t value) {
  g_brightness = value;
  apply_backlight(value);
}

uint8_t DeviceWaveshare4B::getBrightness() {
  return g_brightness;
}

bool DeviceWaveshare4B::getTouch(int16_t& x, int16_t& y) {
  if (!g_touch) {
    return false;
  }

  uint16_t px[1] = {0};
  uint16_t py[1] = {0};
  uint16_t strength[1] = {0};
  uint8_t count = 0;

  esp_lcd_touch_read_data(g_touch);
  const bool touched = esp_lcd_touch_get_coordinates(g_touch, px, py, strength, &count, 1);
  if (!touched || count == 0) {
    return false;
  }

  x = static_cast<int16_t>(px[0]);
  y = static_cast<int16_t>(py[0]);
  return true;
}

void DeviceWaveshare4B::displaySleep() {
  if (g_gfx) {
    g_gfx->displayOff();
  }
  apply_backlight(0);
}

void DeviceWaveshare4B::displayWake() {
  if (g_gfx) {
    g_gfx->displayOn();
    g_gfx->flush();
  }
  apply_backlight(g_brightness);
}

void DeviceWaveshare4B::displayPowerSaveOn() {
  if (g_gfx) {
    g_gfx->displayOff();
  }
  apply_backlight(0);
}

void DeviceWaveshare4B::displayPowerSaveOff() {
  if (g_gfx) {
    g_gfx->displayOn();
    g_gfx->flush();
  }
  apply_backlight(g_brightness);
}

void DeviceWaveshare4B::displayWaitDisplay() {
  if (g_gfx) {
    g_gfx->flush();
  }
}

void DeviceWaveshare4B::prepareForRestart() {
  if (g_gfx) {
    g_gfx->fillScreen(0x0000);
    g_gfx->flush();
    g_gfx->displayOff();
  }

  apply_backlight(0);
  ledc_stop(LEDC_LOW_SPEED_MODE, kBacklightChannel, kBacklightActiveLow ? 1u : 0u);
  gpio_set_direction(kBacklightPin, GPIO_MODE_OUTPUT);
  gpio_set_level(kBacklightPin, kBacklightActiveLow ? 1 : 0);

  if (display_cfg.lcd_rst >= 0) {
    Serial.println("[Device/Waveshare4B] Holding panel reset low for restart");
    hold_panel_reset_low();
    const gpio_num_t rst_pin = static_cast<gpio_num_t>(display_cfg.lcd_rst);
    const esp_err_t hold_err = gpio_hold_en(rst_pin);
    if (hold_err != ESP_OK) {
      Serial.printf("[Device/Waveshare4B] Panel reset hold failed: %s (0x%x)\n",
                    esp_err_to_name(hold_err),
                    static_cast<unsigned>(hold_err));
    }
  }
}

i2c_master_bus_handle_t DeviceWaveshare4B::sharedI2cBus() {
  return g_i2c_bus;
}

bool DeviceWaveshare4B::initSDCard() {
  if (g_sd_available && WaveshareSDMMC.cardType() != CARD_NONE) {
    return true;
  }

  const uint32_t now = millis();
  if (g_sd_init_attempted && (now - g_sd_retry_tick_ms) < 1500) {
    return false;
  }
  g_sd_init_attempted = true;
  g_sd_retry_tick_ms = now;

  WaveshareSDMMC.end();

  if (!WaveshareSDMMC.begin("/sdcard", false, SDMMC_FREQ_DEFAULT, 5)) {
    g_sd_available = false;
    Serial.println("[Device/Waveshare4B] SD card mount failed");
    return false;
  }

  const uint8_t card_type = WaveshareSDMMC.cardType();
  if (card_type == CARD_NONE) {
    g_sd_available = false;
    Serial.println("[Device/Waveshare4B] SD card absent after mount");
    WaveshareSDMMC.end();
    return false;
  }

  g_sd_available = true;
  Serial.printf("[Device/Waveshare4B] SD card OK, type=%u, size=%llu MB\n",
                static_cast<unsigned>(card_type),
                static_cast<unsigned long long>(WaveshareSDMMC.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

bool DeviceWaveshare4B::storageReady() {
  return g_littlefs_ready;
}

fs::FS& DeviceWaveshare4B::storageFS() {
  return LittleFS;
}

bool DeviceWaveshare4B::sdReady() {
  return initSDCard();
}

fs::FS& DeviceWaveshare4B::sdFS() {
  return WaveshareSDMMC;
}

bool DeviceWaveshare4B::suspendSDCardForNetworkTransition() {
  const bool was_mounted =
      g_sd_available && WaveshareSDMMC.cardType() != CARD_NONE;
  if (!was_mounted) return false;

  WaveshareSDMMC.end();
  g_sd_available = false;
  g_sd_init_attempted = false;
  Serial.println("[Device/Waveshare4B] SD card suspended for network transition");
  return true;
}

bool DeviceWaveshare4B::resumeSDCardAfterNetworkTransition() {
  g_sd_available = false;
  g_sd_init_attempted = false;
  const bool mounted = initSDCard();
  Serial.printf("[Device/Waveshare4B] SD card remount after network transition: %s\n",
                mounted ? "OK" : "failed");
  return mounted;
}

bool DeviceWaveshare4B::initLittleFS() {
  if (g_littlefs_ready) {
    return true;
  }
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
    Serial.println("[Device/Waveshare4B] LittleFS mount failed");
    return false;
  }
  g_littlefs_ready = true;
  Serial.printf("[Device/Waveshare4B] LittleFS ready, total=%u, used=%u\n",
                static_cast<unsigned>(LittleFS.totalBytes()),
                static_cast<unsigned>(LittleFS.usedBytes()));
  return true;
}

static bool copyFile(fs::FS& srcFS, fs::FS& dstFS, const char* path) {
  File src = srcFS.open(path, FILE_READ);
  if (!src) {
    return false;
  }
  File dst = dstFS.open(path, FILE_WRITE);
  if (!dst) {
    src.close();
    return false;
  }
  uint8_t buf[512];
  while (src.available()) {
    const size_t n = src.read(buf, sizeof(buf));
    if (n == 0) break;
    dst.write(buf, n);
  }
  dst.close();
  src.close();
  return true;
}

static void copyDirectory(fs::FS& srcFS, fs::FS& dstFS, const char* dirPath) {
  File dir = srcFS.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    return;
  }
  dstFS.mkdir(dirPath);
  File entry = dir.openNextFile();
  while (entry) {
    String fullPath = String(dirPath) + "/" + entry.name();
    if (entry.isDirectory()) {
      copyDirectory(srcFS, dstFS, fullPath.c_str());
    } else {
      copyFile(srcFS, dstFS, fullPath.c_str());
      Serial.printf("[Storage] Migrated: %s (%u bytes)\n", fullPath.c_str(),
                    static_cast<unsigned>(entry.size()));
    }
    entry = dir.openNextFile();
  }
}

void DeviceWaveshare4B::migrateStorageFromSD() {
  if (!g_littlefs_ready) {
    return;
  }
  if (LittleFS.exists("/_migrated")) {
    return;
  }

  LittleFS.mkdir("/_tile_grids");
  LittleFS.mkdir("/_tile_links");
  LittleFS.mkdir("/icons");

  if (initSDCard()) {
    Serial.println("[Storage] Migrating data from SD to LittleFS...");
    copyDirectory(WaveshareSDMMC, LittleFS, "/_tile_grids");
    copyDirectory(WaveshareSDMMC, LittleFS, "/_tile_links");
    copyDirectory(WaveshareSDMMC, LittleFS, "/icons");
    Serial.println("[Storage] Migration complete");
  } else {
    Serial.println("[Storage] No SD card, starting fresh");
  }

  File flag = LittleFS.open("/_migrated", FILE_WRITE);
  if (flag) {
    flag.print("1");
    flag.close();
  }
}

#endif  // defined(DEVICE_WAVESHARE_4B)
