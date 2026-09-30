#pragma once

#include <stdint.h>

#include "src/core/diagnostics/loop_stall.h"

// Media cover timing: where a Media cover change spends its time. Recoloring
// a visible card for "From cover" took 230-260 ms on the Guition S3 and
// 61-68 ms on the Guition V2. Logging only, on the S3 diagnostics build and
// the Guition V2 for comparison; other profiles compile nothing.
#if HOMETILES_LOOP_STALL_DIAGNOSTICS || defined(DEVICE_GUITION_JC8012P4A1_V2)
#define HOMETILES_MEDIA_TIMING 1
#else
#define HOMETILES_MEDIA_TIMING 0
#endif

#if HOMETILES_MEDIA_TIMING
namespace media_timing {
// Summed by ui_surface_style while tile_icon_source recolors a card; reset
// and logged once per recolor ([CoverColor]).
extern uint32_t disc_style_us;
extern uint32_t control_fill_us;
extern uint32_t add_style_us;
extern uint32_t control_fills;
}  // namespace media_timing
#endif
