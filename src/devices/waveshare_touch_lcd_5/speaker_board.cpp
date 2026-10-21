#include "src/audio/speaker_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_5) && defined(HOMETILES_SPEAKER)

#include "src/devices/waveshare_touch_lcd_5/speaker_board.h"

#include "src/devices/waveshare_touch_lcd_5/device_waveshare_touch_lcd_5.h"

namespace speaker_board {

i2c_master_bus_handle_t bus() { return DeviceWaveshareTouchLCD5::sharedI2cBus(); }

}  // namespace speaker_board

#endif  // defined(DEVICE_WAVESHARE_TOUCH_LCD_5) && defined(HOMETILES_SPEAKER)
