#include "src/core/security/secure_random.h"

#include <esp_random.h>
#include <soc/soc_caps.h>

#if !SOC_WIFI_SUPPORTED && !SOC_BT_SUPPORTED
#include <bootloader_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace secure_random {

void fill(void* out, size_t length) {
  if (!out || length == 0) return;
#if !SOC_WIFI_SUPPORTED && !SOC_BT_SUPPORTED
  // Without a radio, Espressif documents the RNG as pseudo-random unless the
  // SAR ADC entropy source runs. HomeTiles does not use the ADC; the mutex
  // keeps one caller from switching the source off while another still reads.
  static StaticSemaphore_t mutex_buffer;
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&mutex_buffer);
  xSemaphoreTake(mutex, portMAX_DELAY);
  bootloader_random_enable();
  esp_fill_random(out, length);
  bootloader_random_disable();
  xSemaphoreGive(mutex);
#else
  esp_fill_random(out, length);
#endif
}

}  // namespace secure_random
