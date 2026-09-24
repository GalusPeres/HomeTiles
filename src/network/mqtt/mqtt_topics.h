#ifndef MQTT_TOPICS_H
#define MQTT_TOPICS_H

#include <Arduino.h>

struct TopicSettings {
  String device_base;
  String ha_prefix;
};

enum class TopicKey : uint8_t {
  SENSOR_OUT = 0,
  SENSOR_IN,
  SENSOR_SOC,
  SCENE_CMND,
  LIGHT_CMND,
  SWITCH_CMND,
  MEDIA_CMND,
  CLIMATE_CMND,
  COVER_CMND,
  CAMERA_CMND,
  CAMERA_STAT,
  STAT_CONN,
  STAT_IP,
  TELE_UP,
  HA_WOHN_TEMP,
  DISPLAY_BRIGHTNESS_CMND,
  DISPLAY_BRIGHTNESS_STAT,
  SCREENSAVER_BRIGHTNESS_CMND,
  SCREENSAVER_BRIGHTNESS_STAT,
  DISPLAY_ROTATE_CMND,
  DISPLAY_ROTATE_STAT,
  DISPLAY_SLEEP_CMND,
  DISPLAY_SLEEP_STAT,
  SLEEP_MAINS_CMND,
  SLEEP_MAINS_STAT,
  SLEEP_BAT_CMND,
  SLEEP_BAT_STAT,
  // Built-in camera snapshots (exact camera profiles only subscribe).
  LOCAL_CAMERA_CMND,
  LOCAL_CAMERA_STAT,
  COUNT
};

class MqttTopicRegistry {
public:
  void begin(const TopicSettings& settings);

  const char* topic(TopicKey key) const;
  const String& deviceBase() const { return device_base_; }
  const String& haPrefix() const { return ha_prefix_; }

private:
  enum class TopicDomain : uint8_t {
    Sensor,
    Command,
    State,
    Telemetry,
    HaStatestream
  };

  struct TopicDescriptor {
    TopicKey key;
    TopicDomain domain;
    const char* leaf;
  };

  String device_base_ = "hometiles";
  String ha_prefix_ = "ha/statestream";
  String sensor_root_;
  String command_root_;
  String state_root_;
  String telemetry_root_;

  static const TopicDescriptor kDescriptors[];
  String topics_[static_cast<size_t>(TopicKey::COUNT)];

  void buildTopics();
  String buildPath(TopicDomain domain, const char* leaf) const;
};

extern MqttTopicRegistry mqttTopics;

#endif // MQTT_TOPICS_H
