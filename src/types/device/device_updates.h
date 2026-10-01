#pragma once

#include <stdint.h>

// Loop service of the Lock, Alarm panel and Fan tiles (device_control.h):
// applies queued detail states and Bridge answers; 0 drains everything.
void process_device_updates(uint8_t budget);
