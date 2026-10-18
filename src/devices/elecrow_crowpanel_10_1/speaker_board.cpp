#include "src/audio/speaker_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1) && defined(HOMETILES_SPEAKER)

#include "src/devices/elecrow_crowpanel_10_1/speaker_board.h"

#include "src/devices/elecrow_crowpanel_10_1/device_elecrow_crowpanel_10_1.h"

namespace speaker_board {

i2c_master_bus_handle_t bus() { return DeviceElecrowCrowPanel10::sharedI2cBus(); }

}  // namespace speaker_board

#endif  // defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1) && defined(HOMETILES_SPEAKER)
