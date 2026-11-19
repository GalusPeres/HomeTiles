#include "src/devices/guition_jc1060p470c_v2/local_camera_board.h"

#if defined(DEVICE_GUITION_JC1060P470C_V2) && defined(HOMETILES_LOCAL_CAMERA)

#include <esp_ldo_regulator.h>

#include "src/devices/guition_jc1060p470c_v2/device_guition_jc1060p470c_v2.h"

namespace local_camera_board {
namespace {

// Same channel and voltage as the display's MIPI PHY supply. A non-adjustable
// channel requested with an identical voltage is shared by reference count,
// so neither user can change or switch off the other's supply.
constexpr int kMipiPhyLdoChannel = 3;
constexpr int kMipiPhyLdoVoltageMv = 2500;

esp_ldo_channel_handle_t g_mipi_phy_ldo = nullptr;

}  // namespace

local_camera::BoardError acquire(i2c_master_bus_handle_t* sccb_bus) {
  if (!sccb_bus) return local_camera::BoardError::BusUnavailable;
  i2c_master_bus_handle_t bus = DeviceGuitionJC1060P470CV2::sharedI2cBus();
  if (!bus) return local_camera::BoardError::BusUnavailable;

  if (!g_mipi_phy_ldo) {
    esp_ldo_channel_config_t config = {};
    config.chan_id = kMipiPhyLdoChannel;
    config.voltage_mv = kMipiPhyLdoVoltageMv;
    if (esp_ldo_acquire_channel(&config, &g_mipi_phy_ldo) != ESP_OK) {
      g_mipi_phy_ldo = nullptr;
      return local_camera::BoardError::PhySupplyFailed;
    }
  }
  *sccb_bus = bus;
  return local_camera::BoardError::None;
}

void release() {
  if (g_mipi_phy_ldo) {
    // Drops only this reference; the display keeps its own handle.
    esp_ldo_release_channel(g_mipi_phy_ldo);
    g_mipi_phy_ldo = nullptr;
  }
}

}  // namespace local_camera_board

#endif  // defined(DEVICE_GUITION_JC1060P470C_V2) && defined(HOMETILES_LOCAL_CAMERA)
