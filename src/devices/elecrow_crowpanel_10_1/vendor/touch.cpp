#include "src/devices/elecrow_crowpanel_10_1/vendor/touch.h"
#include "src/devices/elecrow_crowpanel_10_1/vendor/gt911.h"

#include <esp_log.h>

namespace Elecrow10 {
namespace Touch {

static const char* TAG = "Touch";

bool init(i2c_master_bus_handle_t i2c_bus) {
    return GT911::init(i2c_bus);
}

bool readPoint(TouchPoint& point, i2c_master_bus_handle_t i2c_bus) {
    point.pressed = GT911::readTouch(point.x, point.y, i2c_bus);
    return point.pressed;
}

}  // namespace Touch
}  // namespace Elecrow10
