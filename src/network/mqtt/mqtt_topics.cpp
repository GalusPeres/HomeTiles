#include "src/network/mqtt/mqtt_topics.h"

MqttTopicRegistry mqttTopics;

const MqttTopicRegistry::TopicDescriptor MqttTopicRegistry::kDescriptors[] = {
  {TopicKey::SENSOR_OUT, TopicDomain::Sensor, "outside_c"},
  {TopicKey::SENSOR_IN, TopicDomain::Sensor, "inside_c"},
  {TopicKey::SENSOR_SOC, TopicDomain::Sensor, "soc_pct"},
  {TopicKey::SCENE_CMND, TopicDomain::Command, "scene"},
  {TopicKey::LIGHT_CMND, TopicDomain::Command, "light"},
  {TopicKey::SWITCH_CMND, TopicDomain::Command, "switch"},
  {TopicKey::MEDIA_CMND, TopicDomain::Command, "media"},
  {TopicKey::CLIMATE_CMND, TopicDomain::Command, "climate"},
  {TopicKey::COVER_CMND, TopicDomain::Command, "cover"},
  {TopicKey::CAMERA_CMND, TopicDomain::Command, "camera"},
  {TopicKey::CAMERA_STAT, TopicDomain::State, "camera"},
  {TopicKey::STAT_CONN, TopicDomain::State, "connected"},
  {TopicKey::STAT_IP, TopicDomain::State, "ip"},
  {TopicKey::TELE_UP, TopicDomain::Telemetry, "uptime"},
  {TopicKey::HA_WOHN_TEMP, TopicDomain::HaStatestream, "sensor/og_wohnbereich_sensor_temperatur/state"},
  {TopicKey::DISPLAY_BRIGHTNESS_CMND, TopicDomain::Command, "display_brightness"},
  {TopicKey::DISPLAY_BRIGHTNESS_STAT, TopicDomain::State, "display_brightness"},
  {TopicKey::SCREENSAVER_BRIGHTNESS_CMND, TopicDomain::Command, "screensaver_brightness"},
  {TopicKey::SCREENSAVER_BRIGHTNESS_STAT, TopicDomain::State, "screensaver_brightness"},
  {TopicKey::DISPLAY_ROTATE_CMND, TopicDomain::Command, "display_rotate"},
  {TopicKey::DISPLAY_ROTATE_STAT, TopicDomain::State, "display_rotate"},
  {TopicKey::DISPLAY_SLEEP_CMND, TopicDomain::Command, "display_sleep"},
  {TopicKey::DISPLAY_SLEEP_STAT, TopicDomain::State, "display_sleep"},
  {TopicKey::SLEEP_MAINS_CMND, TopicDomain::Command, "sleep_mains"},
  {TopicKey::SLEEP_MAINS_STAT, TopicDomain::State, "sleep_mains"},
  {TopicKey::SLEEP_BAT_CMND, TopicDomain::Command, "sleep_battery"},
  {TopicKey::SLEEP_BAT_STAT, TopicDomain::State, "sleep_battery"},
  {TopicKey::LOCAL_CAMERA_CMND, TopicDomain::Command, "local_camera"},
  {TopicKey::LOCAL_CAMERA_STAT, TopicDomain::State, "local_camera"},
};

void MqttTopicRegistry::begin(const TopicSettings& settings) {
  if (settings.device_base.length()) {
    device_base_ = settings.device_base;
  } else if (device_base_.isEmpty()) {
    device_base_ = "hometiles";
  }

  if (settings.ha_prefix.length()) {
    ha_prefix_ = settings.ha_prefix;
  } else if (ha_prefix_.isEmpty()) {
    ha_prefix_ = "ha/statestream";
  }

  while (device_base_.endsWith("/")) {
    device_base_.remove(device_base_.length() - 1);
  }
  while (ha_prefix_.endsWith("/")) {
    ha_prefix_.remove(ha_prefix_.length() - 1);
  }

  sensor_root_ = device_base_ + "/sensor/";
  command_root_ = device_base_ + "/cmnd/";
  state_root_ = device_base_ + "/stat/";
  telemetry_root_ = device_base_ + "/tele/";

  buildTopics();
}

const char* MqttTopicRegistry::topic(TopicKey key) const {
  size_t idx = static_cast<size_t>(key);
  if (idx >= static_cast<size_t>(TopicKey::COUNT)) {
    return nullptr;
  }
  return topics_[idx].c_str();
}

void MqttTopicRegistry::buildTopics() {
  for (const auto& desc : kDescriptors) {
    size_t idx = static_cast<size_t>(desc.key);
    topics_[idx] = buildPath(desc.domain, desc.leaf);
  }
}

String MqttTopicRegistry::buildPath(TopicDomain domain, const char* leaf) const {
  switch (domain) {
    case TopicDomain::Sensor:
      return sensor_root_ + leaf;
    case TopicDomain::Command:
      return command_root_ + leaf;
    case TopicDomain::State:
      return state_root_ + leaf;
    case TopicDomain::Telemetry:
      return telemetry_root_ + leaf;
    case TopicDomain::HaStatestream:
      return ha_prefix_ + "/" + leaf;
  }
  return "";
}
