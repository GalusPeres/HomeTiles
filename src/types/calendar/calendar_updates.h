#pragma once

#include <stdint.h>

// Loop service of the Calendar tiles (calendar_store.h): parses queued events
// and refreshes the tiles and the popup; 0 drains everything.
void process_calendar_updates(uint8_t budget);
