#include "src/devices/elecrow_crowpanel_10_1/local_camera_board.h"

#if defined(DEVICE_ELECROW_CROWPANEL_10_1) && defined(HOMETILES_LOCAL_CAMERA)

#include <driver/i2c_master.h>
#include <esp_ldo_regulator.h>

namespace local_camera_board {
namespace {

// Same channel and voltage as the display's MIPI PHY supply
// (device_elecrow_crowpanel_10_1.cpp): shared by reference count.
constexpr int kMipiPhyLdoChannel = 3;
constexpr int kMipiPhyLdoVoltageMv = 2500;
// Elecrow bsp_camera: SCCB_MASTER_PORT 1, SCL 13, SDA 12.
constexpr i2c_port_t kSccbPort = I2C_NUM_1;
constexpr gpio_num_t kSccbScl = GPIO_NUM_13;
constexpr gpio_num_t kSccbSda = GPIO_NUM_12;
constexpr int kSccbTimeoutMs = 50;

esp_ldo_channel_handle_t g_mipi_phy_ldo = nullptr;
i2c_master_bus_handle_t g_bus = nullptr;
i2c_master_dev_handle_t g_device = nullptr;

// SCCB on the camera's own bus: 16-bit register address, 8-bit value.
class I2cTransport final : public sc2336::Transport {
 public:
  esp_err_t probe() override {
    return g_bus ? i2c_master_probe(g_bus, sc2336::kSccbAddress, kSccbTimeoutMs) : ESP_ERR_INVALID_STATE;
  }

  esp_err_t write(uint16_t reg, uint8_t value) override {
    if (!g_device) return ESP_ERR_INVALID_STATE;
    const uint8_t buffer[3] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xff),
                               value};
    return i2c_master_transmit(g_device, buffer, sizeof(buffer), kSccbTimeoutMs);
  }

  esp_err_t read(uint16_t reg, uint8_t* value) override {
    if (!value) return ESP_ERR_INVALID_ARG;
    if (!g_device) return ESP_ERR_INVALID_STATE;
    const uint8_t address[2] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xff)};
    return i2c_master_transmit_receive(g_device, address, sizeof(address), value, 1, kSccbTimeoutMs);
  }
};

I2cTransport g_transport;

}  // namespace

local_camera::BoardError acquire(SccbBus* sccb_bus) {
  if (!sccb_bus) return local_camera::BoardError::BusUnavailable;
  if (!g_bus) {
    i2c_master_bus_config_t config = {};
    config.i2c_port = kSccbPort;
    config.sda_io_num = kSccbSda;
    config.scl_io_num = kSccbScl;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&config, &g_bus) != ESP_OK) {
      g_bus = nullptr;
      return local_camera::BoardError::BusUnavailable;
    }
  }
  if (!g_device) {
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = sc2336::kSccbAddress;
    device.scl_speed_hz = sc2336::kSccbFrequencyHz;
    if (i2c_master_bus_add_device(g_bus, &device, &g_device) != ESP_OK) {
      g_device = nullptr;
      return local_camera::BoardError::BusUnavailable;
    }
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
  // The bus stays open: the sensor sits in software standby.
}

}  // namespace local_camera_board

#endif  // defined(DEVICE_ELECROW_CROWPANEL_10_1) && defined(HOMETILES_LOCAL_CAMERA)
