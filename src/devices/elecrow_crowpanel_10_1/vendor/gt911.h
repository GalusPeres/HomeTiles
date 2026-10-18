#pragma once

#include <driver/i2c_master.h>

namespace Elecrow10 {
namespace GT911 {

bool init(i2c_master_bus_handle_t i2c_bus);
bool readTouch(int16_t& x, int16_t& y, i2c_master_bus_handle_t i2c_bus);

}  // namespace GT911
}  // namespace Elecrow10
