#pragma once

#include <driver/i2c_master.h>
#include <stdint.h>

namespace Elecrow10 {
namespace Touch {

struct TouchPoint {
    int16_t x;
    int16_t y;
    bool pressed;
};

bool init(i2c_master_bus_handle_t i2c_bus);
bool readPoint(TouchPoint& point, i2c_master_bus_handle_t i2c_bus);

}  // namespace Touch
}  // namespace Elecrow10
