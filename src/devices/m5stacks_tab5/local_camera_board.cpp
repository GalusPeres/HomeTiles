#include "src/devices/m5stacks_tab5/local_camera_board.h"

#if defined(DEVICE_M5STACKS_TAB5) && defined(HOMETILES_LOCAL_CAMERA)

#include <M5Unified.h>
#include <esp_ldo_regulator.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace local_camera_board {
namespace {

// Same channel and voltage as the M5GFX DSI bus (Bus_DSI, ldo_chan_id 3,
// 2500 mV). A non-adjustable channel requested with an identical voltage is
// shared by reference count, so neither user can change or switch off the
// other's supply.
constexpr int kMipiPhyLdoChannel = 3;
constexpr int kMipiPhyLdoVoltageMv = 2500;

// CAM_RST: PI4IOE5V6408 at 0x43 (M5.getIOExpander(0)), pin P6, high = run.
// esp-bsp m5stack_tab5 drives it high and waits 100 ms before detection.
constexpr uint8_t kResetExpander = 0;
constexpr uint8_t kResetPin = 6;
constexpr uint32_t kResetReleaseDelayMs = 100;

esp_ldo_channel_handle_t g_mipi_phy_ldo = nullptr;
bool g_reset_released = false;

// SCCB through M5Unified's bus (m5gfx::i2c holds a per-port mutex from start
// to stop, and releases it itself when a transfer fails): 16-bit register
// address, 8-bit value.
class M5Transport final : public sc202cs::Transport {
 public:
  esp_err_t probe() override {
    return M5.In_I2C.scanID(sc202cs::kSccbAddress, sc202cs::kSccbFrequencyHz) ? ESP_OK
                                                                              : ESP_ERR_NOT_FOUND;
  }

  esp_err_t write(uint16_t reg, uint8_t value) override {
    const uint8_t buffer[3] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xff),
                               value};
    const bool ok = M5.In_I2C.start(sc202cs::kSccbAddress, false, sc202cs::kSccbFrequencyHz) &&
                    M5.In_I2C.write(buffer, sizeof(buffer)) && M5.In_I2C.stop();
    return ok ? ESP_OK : ESP_FAIL;
  }

  esp_err_t read(uint16_t reg, uint8_t* value) override {
    if (!value) return ESP_ERR_INVALID_ARG;
    const uint8_t address[2] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xff)};
    const bool ok =
        M5.In_I2C.start(sc202cs::kSccbAddress, false, sc202cs::kSccbFrequencyHz) &&
        M5.In_I2C.write(address, sizeof(address)) &&
        M5.In_I2C.restart(sc202cs::kSccbAddress, true, sc202cs::kSccbFrequencyHz) &&
        M5.In_I2C.read(value, 1, true) && M5.In_I2C.stop();
    return ok ? ESP_OK : ESP_FAIL;
  }
};

M5Transport g_transport;

}  // namespace

local_camera::BoardError acquire(SccbBus* sccb_bus) {
  if (!sccb_bus) return local_camera::BoardError::BusUnavailable;
  // M5.begin() in the display init sets up the bus and the expanders.
  if (M5.getBoard() != m5::board_t::board_M5Tab5) {
    return local_camera::BoardError::BusUnavailable;
  }
  if (!g_reset_released) {
    // Output level first, then drive it, so the line never glitches low.
    auto& expander = M5.getIOExpander(kResetExpander);
    expander.digitalWrite(kResetPin, true);
    expander.setDirection(kResetPin, true);
    expander.setHighImpedance(kResetPin, false);
    vTaskDelay(pdMS_TO_TICKS(kResetReleaseDelayMs));
    g_reset_released = true;
  }

  if (!g_mipi_phy_ldo) {
    esp_ldo_channel_config_t config = {};
    config.chan_id = kMipiPhyLdoChannel;
    config.voltage_mv = kMipiPhyLdoVoltageMv;
    if (esp_ldo_acquire_channel(&config, &g_mipi_phy_ldo) != ESP_OK) {
      g_mipi_phy_ldo = nullptr;
      return local_camera::BoardError::PhySupplyFailed;
    }
  }
  *sccb_bus = &g_transport;
  return local_camera::BoardError::None;
}

void release() {
  if (g_mipi_phy_ldo) {
    // Drops only this reference; the display keeps its own handle.
    esp_ldo_release_channel(g_mipi_phy_ldo);
    g_mipi_phy_ldo = nullptr;
  }
  // The reset line stays released: the sensor sits in software standby.
}

}  // namespace local_camera_board

#endif  // defined(DEVICE_M5STACKS_TAB5) && defined(HOMETILES_LOCAL_CAMERA)
