#pragma once

// Contract between the shared built-in camera core (local_camera.cpp) and a
// device. The camera core owns everything that is the same on every board:
// CSI/ISP/JPEG pipeline, exposure, colour, snapshot transport, Web Admin and
// the Bridge contract. It never names a device.
//
// A camera-capable device only adds drivers. In device_select.h it defines
//   HOMETILES_LOCAL_CAMERA          the device has a supported camera
//   HOMETILES_CAMERA_SENSOR_<NAME>  which shared sensor driver to compile
//                                   (src/video/local_camera/sensors/<name>/)
//   HOMETILES_LOCAL_CAMERA_BOARD    "path/to/local_camera_board.h"
// and its board file provides, in namespace local_camera_board:
//   using SccbBus = <bus handle>;      what Sensor::attach() takes: an ESP-IDF
//                                      i2c_master bus, or a board transport
//                                      where another driver owns the I2C port
//   using Sensor = <sensor>::Sensor;   attach(bus), detach(), attached(),
//                                      probe(&chip_id), loadDefaultMode(mirror),
//                                      setStream(on), setExposure(lines, gain_x16),
//                                      setOrientation(mirror, flip)
//   constexpr local_camera::SensorMode kMode;
//   local_camera::BoardError acquire(SccbBus* sccb_bus);
//   void release();

#include <stdint.h>

#include <hal/color_types.h>

namespace local_camera {

// Fixed capture mode of one sensor on one board.
struct SensorMode {
  const char* name;             // Protocol token for the Bridge status, e.g. "ov02c10".
  uint16_t chip_id;
  uint8_t sccb_address;
  uint16_t frame_width;         // CSI RAW frame (raw_bits per pixel).
  uint16_t frame_height;
  uint16_t image_width;         // JPEG size; equal to the frame (the sensor window is
                                // the JPEG size, this silicon has no ISP crop).
  uint16_t image_height;
  uint8_t data_lanes;
  uint16_t lane_bit_rate_mbps;
  bool mirror;                  // Board mounting, passed to loadDefaultMode().
  bool rotate_180;              // Board mounting in the default display orientation;
                                // the core adds the active display rotation.
  bool quarter_turn;            // Sensor mounted a quarter turn from the landscape
                                // image: the portrait JPEG (4:2:0) is sent as it is and
                                // the Bridge turns it 90 degrees clockwise (status
                                // "rotate":90). The mirror setting maps to the other flip.
  color_raw_element_order_t bayer_order;  // Constant in every orientation; the sensor
                                          // compensates the phase.
  bool line_sync_packets;       // MIPI line start/end packets enabled by the mode.
  uint8_t black_level;          // Sensor pedestal in 8-bit ISP units, removed in the gamma curve.
  uint16_t frame_ms;            // Frame time at the default frame length.
  uint16_t default_exposure_lines;
  uint16_t default_gain_x16;    // 16 == 1.0x analog gain.
  uint16_t min_exposure_lines;
  uint16_t max_exposure_lines;
  uint16_t min_gain_x16;
  uint16_t max_gain_x16;        // Analog gain maximum.
  uint16_t max_total_gain_x16;  // Analog times sensor digital gain; setExposure() splits it.
  // CSI/ISP input format: RAW10 (default) or RAW8.
  uint8_t raw_bits = 10;
};

enum class BoardError : uint8_t {
  None,
  BusUnavailable,   // The shared SCCB/I2C bus does not exist (yet).
  PhySupplyFailed,  // MIPI PHY supply could not be acquired.
};

}  // namespace local_camera
