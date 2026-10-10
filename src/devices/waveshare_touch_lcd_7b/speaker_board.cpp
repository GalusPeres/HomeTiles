#include "src/audio/speaker_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) && defined(HOMETILES_SPEAKER)

#include "src/devices/waveshare_touch_lcd_7b/speaker_board.h"

#include "src/devices/waveshare_touch_lcd_7b/device_waveshare_touch_lcd_7b.h"

namespace speaker_board {

i2c_master_bus_handle_t bus() { return DeviceWaveshareTouchLCD7B::sharedI2cBus(); }

}  // namespace speaker_board

#endif  // defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) && defined(HOMETILES_SPEAKER)
