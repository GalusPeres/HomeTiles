#include "src/audio/speaker_select.h"

#if defined(DEVICE_ELECROW_CROWPANEL_10_1) && defined(HOMETILES_SPEAKER)

#include "src/devices/elecrow_crowpanel_10_1/speaker_board.h"

namespace speaker_board {

// The amplifier and the PDM microphone need no I2C bus.
i2c_master_bus_handle_t bus() { return nullptr; }

}  // namespace speaker_board

#endif  // defined(DEVICE_ELECROW_CROWPANEL_10_1) && defined(HOMETILES_SPEAKER)
