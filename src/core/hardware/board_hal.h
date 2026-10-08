#ifndef BOARD_HAL_H
#define BOARD_HAL_H

#include <Arduino.h>

namespace BoardHAL {

bool init();
void update();

void displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                       const uint16_t* data);
void displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                          const uint16_t* data);
void displayWaitDMA();
void displayFillScreen(uint16_t color);
void displaySetRotation(uint8_t rotation);
void displaySetUpright(bool upright);

void setBrightness(uint8_t value);
uint8_t getBrightness();

struct TouchPoint {
  int16_t x;
  int16_t y;
};

bool getTouch(TouchPoint* tp);

void displaySleep();
void displayWake();
void displayWakeDark();
void displayPowerSaveOn();
void displayPowerSaveOff();
void displayWaitDisplay();
void prepareForRestart();
void restart();

bool initSDCard();

}  // namespace BoardHAL

#endif // BOARD_HAL_H
