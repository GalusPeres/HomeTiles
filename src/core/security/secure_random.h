#pragma once

#include <stddef.h>

namespace secure_random {

// Fills `out` with hardware random bytes for keys, nonces, challenges, session
// ids and pairing codes. Chips without a radio (ESP32-P4) switch on the SAR ADC
// entropy source for the duration of the call; on chips with Wi-Fi the running
// radio is the entropy source.
void fill(void* out, size_t length);

}  // namespace secure_random
