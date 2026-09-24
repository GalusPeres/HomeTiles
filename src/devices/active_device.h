#pragma once

#include "src/devices/device_select.h"

#if defined(DEVICE_M5STACKS_TAB5)
#include "src/devices/m5stacks_tab5/device_m5stacks_tab5.h"
namespace DeviceImpl = DeviceM5StacksTab5;
#elif defined(DEVICE_GUITION_JC8012P4A1)
#include "src/devices/guition_jc8012p4a1/device_guition_jc8012p4a1.h"
namespace DeviceImpl = DeviceGuitionJC8012P4A1;
#elif defined(DEVICE_GUITION_JC8012P4A1_V2)
#include "src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.h"
namespace DeviceImpl = DeviceGuitionJC8012P4A1V2;
#elif defined(DEVICE_GUITION_JC1060P470C)
#include "src/devices/guition_jc1060p470c/device_guition_jc1060p470c.h"
namespace DeviceImpl = DeviceGuitionJC1060P470C;
#elif defined(DEVICE_GUITION_JC1060P470C_V2)
#include "src/devices/guition_jc1060p470c_v2/device_guition_jc1060p470c_v2.h"
namespace DeviceImpl = DeviceGuitionJC1060P470CV2;
#elif defined(DEVICE_GUITION_JC4880P443_PORTRAIT)
#include "src/devices/guition_jc4880p443_portrait/device_guition_jc4880p443_portrait.h"
namespace DeviceImpl = DeviceGuitionJC4880P443Portrait;
#elif defined(DEVICE_GUITION_ESP32_4848S040)
#include "src/devices/guition_esp32_4848s040/device_guition_esp32_4848s040.h"
namespace DeviceImpl = DeviceGuitionESP324848S040;
#elif defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4)
#include "src/devices/waveshare_s3_touch_lcd_4/device_waveshare_s3_touch_lcd_4.h"
namespace DeviceImpl = DeviceWaveshareS3TouchLCD4;
#elif defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4B)
#include "src/devices/waveshare_s3_touch_lcd_4b/device_waveshare_s3_touch_lcd_4b.h"
namespace DeviceImpl = DeviceWaveshareS3TouchLCD4B;
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_4_3)
#include "src/devices/waveshare_touch_lcd_4_3/device_waveshare_touch_lcd_4_3.h"
namespace DeviceImpl = DeviceWaveshareTouchLCD4_3;
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_7)
#include "src/devices/waveshare_touch_lcd_7/device_waveshare_touch_lcd_7.h"
namespace DeviceImpl = DeviceWaveshareTouchLCD7;
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)
#include "src/devices/waveshare_touch_lcd_7b/device_waveshare_touch_lcd_7b.h"
namespace DeviceImpl = DeviceWaveshareTouchLCD7B;
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_8)
#include "src/devices/waveshare_touch_lcd_8/device_waveshare_touch_lcd_8.h"
namespace DeviceImpl = DeviceWaveshareTouchLCD8;
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
#include "src/devices/waveshare_touch_lcd_10_1/device_waveshare_touch_lcd_10_1.h"
namespace DeviceImpl = DeviceWaveshareTouchLCD10;
#elif defined(DEVICE_LAYOUT_TEST_1024X600)
#include "src/devices/layout_test_1024x600/device_layout_test_1024x600.h"
namespace DeviceImpl = DeviceLayoutTest1024x600;
#elif defined(DEVICE_LAYOUT_TEST_480X480)
#include "src/devices/layout_test_480x480/device_layout_test_480x480.h"
namespace DeviceImpl = DeviceLayoutTest480x480;
#elif defined(DEVICE_WAVESHARE_4B)
#include "src/devices/waveshare_4b/device_waveshare_4b.h"
namespace DeviceImpl = DeviceWaveshare4B;
#else
#error "No supported device target selected."
#endif
