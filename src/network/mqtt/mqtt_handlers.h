#ifndef MQTT_HANDLERS_H
#define MQTT_HANDLERS_H

#include <Arduino.h>

// MQTT callback functions.
void mqttCallback(char* topic, uint8_t* payload, unsigned int length);
// Drains inbound MQTT messages that mqttCallback() queued (see mqtt_handlers.cpp
// header comment) and runs the real per-topic processing on the caller's task.
// Call from the main loop(). max_msgs=0 drains everything currently queued.
void mqtt_process_inbound_queue(uint8_t max_msgs = 0);
// True once after mqttCallback() had to drop a state message (queue full or
// no memory); the loop then asks the Bridge for a refresh.
bool mqttTakeInboundDropped();
// True while received messages wait for the loop or one arrived within
// `quiet_ms`: a long loop block now would overflow the inbound queue.
bool mqttInboundBusy(uint32_t quiet_ms = 0);
// Consumes the post-connect pending flag of the MQTT worker and brings the
// application layer up: subscribes, discovery, settings, snapshot. Must run on
// the loop task because of flash, LVGL and I2C access; call once per loop
// iteration.
void mqttServicePostConnect();
void mqttSubscribeTopics();
void mqttPublishDiscovery();
void mqttPublishScene(const char* scene_name);
void mqttPublishSwitchCommand(const char* entity_id, const char* state);
void mqttPublishMediaCommand(const char* entity_id, const char* command);
void mqttPublishMediaSeek(const char* entity_id, float position_seconds);
void mqttPublishMediaVolume(const char* entity_id, float volume_level);
void mqttPublishMediaMute(const char* entity_id, bool muted);
// fps 0 asks for camera_geometry::kFps.
void mqttPublishCameraCommand(const char* entity_id, const char* command,
                              uint8_t fps = 0);
void mqttPublishClimateTemperature(const char* entity_id,
                                   float temperature,
                                   bool use_range = false,
                                   float target_low = 0.0f,
                                   float target_high = 0.0f);
void mqttPublishClimateHumidity(const char* entity_id, float humidity);
void mqttPublishClimateHvacMode(const char* entity_id, const char* hvac_mode);
void mqttPublishClimatePresetMode(const char* entity_id, const char* preset_mode);
void mqttPublishClimateFanMode(const char* entity_id, const char* fan_mode);
void mqttPublishClimateSwingMode(const char* entity_id, const char* swing_mode);
void mqttPublishClimateHorizontalSwingMode(
    const char* entity_id, const char* swing_mode);
void mqttPublishCoverCommand(const char* entity_id,
                             const char* command,
                             int position = -1);
void mqttPublishLightCommand(const char* entity_id,
                             const char* state,
                             int brightness_pct,
                             bool has_color,
                             uint32_t color,
                             int color_temp_kelvin = -1);
void mqttPublishHistoryRequest(const char* entity_id,
                               uint16_t hours = 24,
                               uint16_t period_minutes = 5,
                               uint16_t points = 288);
void mqttPublishBinaryHistoryRequest(const char* entity_id,
                                     uint16_t hours = 24,
                                     uint16_t max_transitions = 48);
void mqttPublishStateHistoryRequest(const char* entity_id,
                                    uint16_t hours = 24,
                                    uint16_t max_transitions = 48);
void mqttPublishWeatherRequest(const char* entity_id);
bool mqttPublishEnergyRequest(const char* period = "day");
void mqttPublishHomeSnapshot();
void mqttPublishDeviceSettings();
void mqttServiceLocalSensors();
void mqttReloadDynamicSlots(bool subscribe_all = false);
void mqttRequestDynamicSlotsReload(uint32_t quiet_ms = 3000);
void mqttServiceDynamicSlotsReload();
// Boot scan for setup(): is there a media tile in the stored configuration? The
// result sets the initial size of the MQTT receive buffer, because cover
// payloads reach about 19 KB.
bool mqttAnyMediaTileConfigured();

#endif // MQTT_HANDLERS_H
