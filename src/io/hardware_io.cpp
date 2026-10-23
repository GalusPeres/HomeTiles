#include "src/io/hardware_io.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <cmath>
#include <cstring>
#include <strings.h>
#include <driver/gpio.h>

#include "src/devices/device.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/network_manager.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"

namespace {

constexpr char kConfigDir[] = "/_hardware_io";
constexpr char kConfigPath[] = "/_hardware_io/config_v1.json";
constexpr char kConfigTmpPath[] = "/_hardware_io/config_v1.json.tmp";
constexpr char kConfigBackupPath[] = "/_hardware_io/config_v1.json.bak";
constexpr uint16_t kConfigVersion = 1;
constexpr size_t kMaxConfigBytes = 12U * 1024U;
constexpr uint32_t kTemperatureConversionMs = 800;
constexpr uint32_t kTemperatureSampleMs = 3000;
constexpr uint32_t kTemperatureRetryMs = 3000;
constexpr uint32_t kStateRepublishMs = 60000;

bool replace_file_atomically(fs::FS& fs) {
  if (!fs.exists(kConfigTmpPath)) return false;
  if (fs.exists(kConfigBackupPath)) fs.remove(kConfigBackupPath);
  if (fs.exists(kConfigPath) && !fs.rename(kConfigPath, kConfigBackupPath)) {
    return false;
  }
  if (fs.rename(kConfigTmpPath, kConfigPath)) {
    if (fs.exists(kConfigBackupPath)) fs.remove(kConfigBackupPath);
    return true;
  }
  if (fs.exists(kConfigBackupPath) && !fs.exists(kConfigPath)) {
    fs.rename(kConfigBackupPath, kConfigPath);
  }
  return false;
}

const char* type_name(HardwareIoType type) {
  return type == HardwareIoType::Temperature ? "temperature" : "relay";
}

bool parse_type(const char* raw, HardwareIoType& out) {
  if (!raw) return false;
  if (strcasecmp(raw, "relay") == 0 || strcasecmp(raw, "switch") == 0) {
    out = HardwareIoType::Relay;
    return true;
  }
  if (strcasecmp(raw, "temperature") == 0 ||
      strcasecmp(raw, "ds18b20") == 0) {
    out = HardwareIoType::Temperature;
    return true;
  }
  return false;
}

const char* default_hardware_variant() {
  const char* variant = Device::profile().hardware_io.default_variant;
  return variant && *variant ? variant : "standard";
}

bool pin_supports_type(const Device::HardwareIoPinOption& option,
                       HardwareIoType type) {
  const uint8_t wanted = type == HardwareIoType::Temperature
                             ? Device::kHardwareIoPinTemperature
                             : Device::kHardwareIoPinSwitch;
  return (option.capabilities & wanted) != 0;
}

bool pin_matches_variant(const Device::HardwareIoPinOption& option,
                         const String& board_variant) {
  return !option.required_variant || !*option.required_variant ||
         board_variant.equalsIgnoreCase(option.required_variant);
}

bool valid_hardware_variant(const String& board_variant) {
  if (board_variant.equalsIgnoreCase(default_hardware_variant())) return true;
  const Device::HardwareIoProfile& profile = Device::profile().hardware_io;
  for (uint8_t i = 0; i < profile.pin_count; ++i) {
    const char* required = profile.pin_options[i].required_variant;
    if (required && *required && board_variant.equalsIgnoreCase(required)) {
      return true;
    }
  }
  return false;
}

const Device::HardwareIoPinOption* find_pin_option(
    int gpio, HardwareIoType type, const String& board_variant) {
  const Device::HardwareIoProfile& profile = Device::profile().hardware_io;
  for (uint8_t i = 0; i < profile.pin_count; ++i) {
    const Device::HardwareIoPinOption& option = profile.pin_options[i];
    if (option.gpio == gpio && pin_supports_type(option, type) &&
        pin_matches_variant(option, board_variant)) {
      return &option;
    }
  }
  return nullptr;
}

bool valid_id(const String& id) {
  if (!id.length() || id.length() > 32) return false;
  const char first = id.charAt(0);
  if (!((first >= 'a' && first <= 'z') || (first >= '0' && first <= '9'))) {
    return false;
  }
  for (size_t i = 0; i < id.length(); ++i) {
    const char c = id.charAt(i);
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

void append_json_escaped(String& out, const String& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
}

uint8_t one_wire_crc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0;
  while (length--) {
    uint8_t in_byte = *data++;
    for (uint8_t i = 0; i < 8; ++i) {
      const uint8_t mix = (crc ^ in_byte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      in_byte >>= 1;
    }
  }
  return crc;
}

inline void one_wire_release(gpio_num_t pin) {
  gpio_set_level(pin, 1);
}

inline void one_wire_low(gpio_num_t pin) {
  gpio_set_level(pin, 0);
}

bool one_wire_reset(gpio_num_t pin) {
  one_wire_release(pin);
  delayMicroseconds(5);
  one_wire_low(pin);
  delayMicroseconds(480);
  // The long reset-low phase needs no interrupt lock. Protect only the narrow
  // presence window so display/network work is not blocked for over 500 us.
  noInterrupts();
  one_wire_release(pin);
  delayMicroseconds(70);
  const bool present = gpio_get_level(pin) == 0;
  interrupts();
  delayMicroseconds(410);
  // DATA must return high after the presence pulse. A bus shorted to ground
  // would otherwise look like a sensor and produce a zero scratchpad with a
  // valid CRC.
  const bool released = gpio_get_level(pin) != 0;
  return present && released;
}

void one_wire_write_bit(gpio_num_t pin, bool value) {
  noInterrupts();
  one_wire_low(pin);
  if (value) {
    delayMicroseconds(6);
    one_wire_release(pin);
    delayMicroseconds(64);
  } else {
    delayMicroseconds(60);
    one_wire_release(pin);
    delayMicroseconds(10);
  }
  interrupts();
}

bool one_wire_read_bit(gpio_num_t pin) {
  noInterrupts();
  one_wire_low(pin);
  delayMicroseconds(3);
  one_wire_release(pin);
  delayMicroseconds(10);
  const bool value = gpio_get_level(pin) != 0;
  interrupts();
  delayMicroseconds(53);
  return value;
}

void one_wire_write_byte(gpio_num_t pin, uint8_t value) {
  for (uint8_t i = 0; i < 8; ++i) {
    one_wire_write_bit(pin, (value & 0x01U) != 0);
    value >>= 1;
  }
}

uint8_t one_wire_read_byte(gpio_num_t pin) {
  uint8_t value = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    if (one_wire_read_bit(pin)) value |= static_cast<uint8_t>(1U << i);
  }
  return value;
}

void configure_one_wire_pin(int gpio) {
  const gpio_num_t pin = static_cast<gpio_num_t>(gpio);
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << gpio;
  config.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
  one_wire_release(pin);
}

String local_ascii_slug(const char* value) {
  String slug;
  slug.reserve(value ? strlen(value) : 0);
  bool separator_pending = false;
  for (const unsigned char* p =
           reinterpret_cast<const unsigned char*>(value);
       p && *p; ++p) {
    char c = static_cast<char>(*p);
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    const bool valid = (c >= 'a' && c <= 'z') ||
                       (c >= '0' && c <= '9');
    if (!valid) {
      separator_pending = slug.length() > 0;
      continue;
    }
    if (separator_pending) slug += '_';
    separator_pending = false;
    slug += c;
  }
  return slug;
}

String local_model_slug() {
  String slug = local_ascii_slug(Device::profile().key);
  if (!slug.length()) slug = local_ascii_slug(Device::displayName());
  if (!slug.length()) slug = "panel";
  return slug;
}

String legacy_model_slug() {
  String slug = local_ascii_slug(Device::displayName());
  if (!slug.length()) slug = local_model_slug();
  return slug;
}

String local_device_slug() {
  return local_model_slug();
}

String local_entity_name_slug(const HardwareIoChannelConfig& config) {
  String slug = local_ascii_slug(config.name.c_str());
  // The hidden channel ID remains the deterministic fallback for names that
  // contain no ASCII characters. It also keeps the entity usable while an
  // empty name is being corrected by the configuration parser.
  if (!slug.length()) slug = config.id;
  return slug;
}

String make_local_entity_id(const HardwareIoChannelConfig& config) {
  String entity(config.type == HardwareIoType::Temperature
                    ? "sensor."
                    : "switch.");
  entity += local_device_slug();
  entity += '_';
  entity += local_entity_name_slug(config);
  return entity;
}

String make_legacy_name_local_entity_id(
    const HardwareIoChannelConfig& config) {
  String entity(config.type == HardwareIoType::Temperature
                    ? "sensor."
                    : "switch.");
  entity += legacy_model_slug();
  entity += '_';
  entity += local_entity_name_slug(config);
  return entity;
}

String make_legacy_channel_local_entity_id(
    const HardwareIoChannelConfig& config) {
  String entity(config.type == HardwareIoType::Temperature
                    ? "sensor."
                    : "switch.");
  entity += legacy_model_slug();
  entity += '_';
  entity += config.id;
  return entity;
}

void update_local_entity_grids(const String& entity_id, const char* payload) {
  if (!entity_id.length() || !payload) return;
  tiles_update_sensor_by_entity(GridType::TAB0, entity_id.c_str(), payload);
  tiles_update_sensor_by_entity(GridType::TAB1, entity_id.c_str(), payload);
  tiles_update_sensor_by_entity(GridType::TAB2, entity_id.c_str(), payload);
  tiles_update_sensor_by_entity(GridType::SCREENSAVER, entity_id.c_str(), payload);
}

}  // namespace

HardwareIoManager hardwareIo;

HardwareIoManager::HardwareIoManager() {
  reset();
}

const Device::HardwareIoPinOption* HardwareIoManager::pinOptions(
    size_t& count) {
  const Device::HardwareIoProfile& profile = Device::profile().hardware_io;
  count = profile.pin_count;
  return profile.pin_options;
}

String HardwareIoManager::localEntityId(uint8_t index) const {
  if (index >= channel_count_) return String();
  if (runtime_[index].local_entity_id.length()) {
    return runtime_[index].local_entity_id;
  }
  return make_local_entity_id(channels_[index]);
}

bool HardwareIoManager::localEntityInfo(uint8_t index, String& entity_id,
                                        String& name,
                                        HardwareIoType& type) const {
  if (index >= channel_count_) return false;
  entity_id = localEntityId(index);
  name = channels_[index].name;
  type = channels_[index].type;
  return entity_id.length() > 0;
}

bool HardwareIoManager::isLocalEntityId(const char* entity_id) const {
  if (!entity_id || !*entity_id) return false;
  // A visible, device-specific ID always wins over compatibility aliases.
  for (uint8_t i = 0; i < channel_count_; ++i) {
    if (runtime_[i].local_entity_id.length()) {
      if (runtime_[i].local_entity_id.equalsIgnoreCase(entity_id)) return true;
    } else if (make_local_entity_id(channels_[i]).equalsIgnoreCase(entity_id)) {
      return true;
    }
  }
  for (uint8_t i = 0; i < channel_count_; ++i) {
    if (make_legacy_name_local_entity_id(channels_[i]).equalsIgnoreCase(
            entity_id)) {
      return true;
    }
  }
  for (uint8_t i = 0; i < channel_count_; ++i) {
    if (make_legacy_channel_local_entity_id(channels_[i]).equalsIgnoreCase(
            entity_id)) {
      return true;
    }
  }
  return false;
}

bool HardwareIoManager::handleLocalEntityCommand(const char* entity_id,
                                                  const char* action) {
  if (!entity_id || !*entity_id) return false;
  int8_t matched_index = -1;
  // Prefer the visible ID. This prevents a legacy alias from claiming the
  // name-derived ID of another channel after an upgrade.
  for (uint8_t i = 0; i < channel_count_; ++i) {
    if (localEntityId(i).equalsIgnoreCase(entity_id)) {
      matched_index = static_cast<int8_t>(i);
      break;
    }
  }
  if (matched_index < 0) {
    for (uint8_t i = 0; i < channel_count_; ++i) {
      if (make_legacy_name_local_entity_id(channels_[i]).equalsIgnoreCase(
              entity_id)) {
        matched_index = static_cast<int8_t>(i);
        break;
      }
    }
  }
  if (matched_index < 0) {
    for (uint8_t i = 0; i < channel_count_; ++i) {
      if (make_legacy_channel_local_entity_id(channels_[i]).equalsIgnoreCase(
              entity_id)) {
        matched_index = static_cast<int8_t>(i);
        break;
      }
    }
  }
  if (matched_index < 0) return false;

  const uint8_t index = static_cast<uint8_t>(matched_index);
  if (channels_[index].type != HardwareIoType::Relay) {
    Serial.printf("[HardwareIO] Local sensor entity is not switchable: %s\n",
                  entity_id);
    return true;
  }

  String command = action && *action ? String(action) : String("toggle");
  command.trim();
  command.toLowerCase();
  bool state = runtime_[index].relay_state;
  if (command == "toggle") {
    state = !state;
  } else if (command == "on" || command == "1" || command == "true") {
    state = true;
  } else if (command == "off" || command == "0" || command == "false") {
    state = false;
  } else {
    Serial.printf("[HardwareIO] Invalid local relay command for %s\n",
                  entity_id);
    return true;
  }
  applyRelay(index, state, true);
  Serial.printf("[HardwareIO] Local relay '%s' -> %s\n",
                entity_id, state ? "ON" : "OFF");
  return true;
}

void HardwareIoManager::reset() {
  channel_count_ = 0;
  service_cursor_ = 0;
  stale_state_topic_count_ = 0;
  stale_state_retry_ms_ = 0;
  board_variant_ = default_hardware_variant();
  for (uint8_t i = 0; i < kHardwareIoMaxChannels; ++i) {
    channels_[i] = HardwareIoChannelConfig{};
    runtime_[i] = RuntimeChannel{};
    stale_state_topics_[i] = "";
  }
}

bool HardwareIoManager::parseDocument(
    const String& json, HardwareIoChannelConfig* out_channels,
    uint8_t& out_count, String& out_board_variant,
    String& error, bool allow_missing_names) const {
  out_count = 0;
  out_board_variant = default_hardware_variant();
  if (!json.length() || json.length() > kMaxConfigBytes) {
    error = "Invalid configuration size";
    return false;
  }

  JsonDocument doc;
  const DeserializationError parse_error = deserializeJson(doc, json);
  if (parse_error) {
    error = String("Invalid JSON: ") + parse_error.c_str();
    return false;
  }
  out_board_variant = String(doc["board_variant"] |
                             default_hardware_variant());
  out_board_variant.trim();
  out_board_variant.toLowerCase();
  if (!out_board_variant.length()) {
    out_board_variant = default_hardware_variant();
  }
  if (!valid_hardware_variant(out_board_variant)) {
    if (!allow_missing_names) {
      error = "Unknown hardware variant";
      return false;
    }
    // A stored file (the lenient load) may name a variant this firmware no
    // longer has: the GS-T3E tied each relay to its own variant until issue
    // #93. Its channels are checked against the default variant below, so
    // only pins that are still offered come back.
    Serial.printf("[HardwareIO] Stored variant '%s' no longer exists, using '%s'\n",
                  out_board_variant.c_str(), default_hardware_variant());
    out_board_variant = default_hardware_variant();
  }
  JsonArrayConst input = doc["channels"].as<JsonArrayConst>();
  if (input.size() > kHardwareIoMaxChannels) {
    error = "Too many channels";
    return false;
  }

  for (JsonObjectConst item : input) {
    HardwareIoChannelConfig config;
    if (!parse_type(item["type"] | "", config.type)) {
      error = "Unknown channel type";
      return false;
    }
    const JsonVariantConst raw_gpio = item["gpio"];
    if (!raw_gpio.is<int>()) {
      error = "GPIO must be an integer";
      return false;
    }
    const int gpio = raw_gpio.as<int>();
    if (gpio < 0 || gpio > 63) {
      error = "GPIO must be between 0 and 63";
      return false;
    }
    config.gpio = static_cast<int8_t>(gpio);
    config.id = String(item["id"] | "");
    config.id.trim();
    config.id.toLowerCase();
    if (!config.id.length()) {
      config.id = type_name(config.type);
      config.id += "_";
      config.id += String(static_cast<unsigned>(config.gpio));
    }
    if (!valid_id(config.id)) {
      error = "Channel IDs may only contain lowercase letters, numbers and underscores";
      return false;
    }
    config.name = String(item["name"] | "");
    config.name.trim();
    if (!config.name.length()) {
      if (!allow_missing_names) {
        error = "Channel name is required";
        return false;
      }
      config.name = config.type == HardwareIoType::Temperature
                        ? "Temperature GPIO "
                        : "Switch GPIO ";
      config.name += String(static_cast<unsigned>(config.gpio));
    }
    if (config.name.length() > 48) {
      error = "Channel name is too long";
      return false;
    }
    config.inverted = item["inverted"] | false;
    const String boot_state = String(item["boot_state"] | "off");
    config.boot_on = boot_state.equalsIgnoreCase("on");
    config.precision = static_cast<uint8_t>(
        constrain(item["precision"] | 1, 0, 3));

    const Device::HardwareIoPinOption* pin_option =
        find_pin_option(config.gpio, config.type, out_board_variant);
    if (!pin_option) {
      error = "GPIO ";
      error += String(static_cast<int>(config.gpio));
      error += " is not available for this channel type on ";
      error += Device::displayName();
      return false;
    }
    if (config.type == HardwareIoType::Relay) {
      const Device::HardwareIoOutputLogic logic = pin_option->output_logic;
      if ((logic == Device::HardwareIoOutputLogic::ActiveHigh &&
           config.inverted) ||
          (logic == Device::HardwareIoOutputLogic::ActiveLow &&
           !config.inverted)) {
        error = pin_option->label;
        error += logic == Device::HardwareIoOutputLogic::ActiveLow
                     ? " is fixed active-low"
                     : " is fixed active-high";
        return false;
      }
    }
    for (uint8_t i = 0; i < out_count; ++i) {
      if (out_channels[i].gpio == config.gpio) {
        error = "A GPIO can only be assigned once";
        return false;
      }
      if (out_channels[i].id.equalsIgnoreCase(config.id)) {
        error = "Channel IDs must be unique";
        return false;
      }
      // Only visible IDs must be unique. A newly allocated hidden ID may
      // legitimately equal another channel's visible name slug (for example
      // old relay_1 + new switch_1). Ambiguous legacy aliases are suppressed
      // at runtime and must not block an otherwise valid configuration.
      if (make_local_entity_id(out_channels[i]).equalsIgnoreCase(
              make_local_entity_id(config))) {
        error = "Channel names must create unique entity IDs";
        return false;
      }
    }
    out_channels[out_count++] = config;
  }
  return true;
}

bool HardwareIoManager::loadPath(const char* path) {
  if (!path || !Device::storageReady()) return false;
  fs::File file = Device::storageFS().open(path, FILE_READ);
  if (!file) return false;
  if (file.size() == 0 || static_cast<size_t>(file.size()) > kMaxConfigBytes) {
    file.close();
    return false;
  }
  String json;
  json.reserve(file.size());
  while (file.available()) json += static_cast<char>(file.read());
  file.close();

  HardwareIoChannelConfig loaded[kHardwareIoMaxChannels];
  uint8_t count = 0;
  String board_variant;
  String error;
  if (!parseDocument(json, loaded, count, board_variant, error, true)) {
    Serial.printf("[HardwareIO] %s invalid: %s\n", path, error.c_str());
    return false;
  }
  channel_count_ = count;
  board_variant_ = board_variant;
  for (uint8_t i = 0; i < count; ++i) channels_[i] = loaded[i];
  return true;
}

bool HardwareIoManager::load() {
  reset();
  if (!Device::storageReady()) return false;
  fs::FS& fs = Device::storageFS();
  if (loadPath(kConfigTmpPath)) {
#if defined(DEVICE_ESP32_S3_RGB_480)
    Device::ScopedStorageWrite storage_write;
#endif
    fs.remove(kConfigPath);
    fs.rename(kConfigTmpPath, kConfigPath);
    Serial.println("[HardwareIO] Restored from .tmp");
    return true;
  }
  if (loadPath(kConfigPath)) return true;
  if (loadPath(kConfigBackupPath)) {
#if defined(DEVICE_ESP32_S3_RGB_480)
    Device::ScopedStorageWrite storage_write;
#endif
    fs.remove(kConfigPath);
    fs.rename(kConfigBackupPath, kConfigPath);
    Serial.println("[HardwareIO] Restored from .bak");
    return true;
  }
  Serial.println("[HardwareIO] No mappings, all GPIOs remain free");
  return false;
}

void HardwareIoManager::stopRuntime() {
  for (uint8_t i = 0; i < channel_count_; ++i) {
    const HardwareIoChannelConfig& config = channels_[i];
    if (config.gpio < 0) continue;
    if (config.type == HardwareIoType::Relay) {
      const int off_level = config.inverted ? 1 : 0;
      gpio_set_level(static_cast<gpio_num_t>(config.gpio), off_level);
    } else {
      one_wire_release(static_cast<gpio_num_t>(config.gpio));
    }
    gpio_set_direction(static_cast<gpio_num_t>(config.gpio), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(config.gpio), GPIO_FLOATING);
  }
}

void HardwareIoManager::startRuntime() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < channel_count_; ++i) {
    runtime_[i] = RuntimeChannel{};
    const HardwareIoChannelConfig& config = channels_[i];
    if (config.type == HardwareIoType::Relay) {
      const int level = config.boot_on
                            ? (config.inverted ? 0 : 1)
                            : (config.inverted ? 1 : 0);
      // Set the safe level before enabling output to prevent a startup relay pulse.
      gpio_set_level(static_cast<gpio_num_t>(config.gpio), level);
      gpio_set_direction(static_cast<gpio_num_t>(config.gpio), GPIO_MODE_OUTPUT);
      runtime_[i].relay_state = config.boot_on;
      Serial.printf("[HardwareIO] Relay '%s' GPIO %d: %s (active-%s)\n",
                    config.id.c_str(), static_cast<int>(config.gpio),
                    config.boot_on ? "ON" : "OFF",
                    config.inverted ? "low" : "high");
    } else {
      configure_one_wire_pin(config.gpio);
      runtime_[i].next_action_ms = now + 250U + static_cast<uint32_t>(i) * 350U;
      Serial.printf("[HardwareIO] DS18x20 '%s' enabled on GPIO %d\n",
                    config.id.c_str(), static_cast<int>(config.gpio));
    }
  }
  rebuildMqttTopics();
  syncAllLocalEntityStates(true);
}

void HardwareIoManager::transitionRuntime(
    const HardwareIoChannelConfig* next_channels, uint8_t next_count) {
  HardwareIoChannelConfig previous_channels[kHardwareIoMaxChannels];
  RuntimeChannel previous_runtime[kHardwareIoMaxChannels];
  const uint8_t previous_count = channel_count_;
  for (uint8_t i = 0; i < previous_count; ++i) {
    previous_channels[i] = channels_[i];
    previous_runtime[i] = runtime_[i];
  }

  bool previous_kept[kHardwareIoMaxChannels] = {false};
  int8_t previous_match[kHardwareIoMaxChannels];
  memset(previous_match, -1, sizeof(previous_match));
  for (uint8_t next = 0; next < next_count; ++next) {
    for (uint8_t old = 0; old < previous_count; ++old) {
      if (previous_kept[old] ||
          previous_channels[old].gpio != next_channels[next].gpio ||
          previous_channels[old].type != next_channels[next].type) {
        continue;
      }
      // Relay polarity belongs to the physical mapping. Changing it requires
      // safely turning the old output off before reinitializing it. Changes to
      // name, ID or boot preference must preserve the current switched state.
      if (next_channels[next].type == HardwareIoType::Relay &&
          previous_channels[old].inverted != next_channels[next].inverted) {
        continue;
      }
      previous_kept[old] = true;
      previous_match[next] = static_cast<int8_t>(old);
      break;
    }
  }

  // Touch only removed or physically changed channels. Autosaving a rename
  // must not briefly turn off a running relay.
  for (uint8_t old = 0; old < previous_count; ++old) {
    if (previous_kept[old]) continue;
    const HardwareIoChannelConfig& config = previous_channels[old];
    const gpio_num_t pin = static_cast<gpio_num_t>(config.gpio);
    if (config.type == HardwareIoType::Relay) {
      gpio_set_level(pin, config.inverted ? 1 : 0);
    } else {
      one_wire_release(pin);
    }
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_FLOATING);
  }

  channel_count_ = next_count;
  for (uint8_t i = 0; i < kHardwareIoMaxChannels; ++i) {
    channels_[i] = i < next_count ? next_channels[i]
                                 : HardwareIoChannelConfig{};
    runtime_[i] = RuntimeChannel{};
  }

  const uint32_t now = millis();
  for (uint8_t next = 0; next < next_count; ++next) {
    const int8_t old = previous_match[next];
    if (old >= 0) {
      runtime_[next] = previous_runtime[static_cast<uint8_t>(old)];
      continue;
    }

    const HardwareIoChannelConfig& config = channels_[next];
    const gpio_num_t pin = static_cast<gpio_num_t>(config.gpio);
    if (config.type == HardwareIoType::Relay) {
      const int level = config.boot_on
                            ? (config.inverted ? 0 : 1)
                            : (config.inverted ? 1 : 0);
      gpio_set_level(pin, level);
      gpio_set_direction(pin, GPIO_MODE_OUTPUT);
      runtime_[next].relay_state = config.boot_on;
    } else {
      configure_one_wire_pin(config.gpio);
      runtime_[next].next_action_ms =
          now + 250U + static_cast<uint32_t>(next) * 350U;
    }
  }
  if (channel_count_ > 0) service_cursor_ %= channel_count_;
  else service_cursor_ = 0;
  rebuildMqttTopics();
}

void HardwareIoManager::begin() {
  if (begun_) stopRuntime();
  begun_ = true;
  startRuntime();
}

bool HardwareIoManager::saveChannels(
    const HardwareIoChannelConfig* channels, uint8_t count,
    const String& board_variant) const {
  if (!Device::storageReady()) return false;
  JsonDocument doc;
  doc["version"] = kConfigVersion;
  doc["board_variant"] = board_variant;
  JsonArray array = doc["channels"].to<JsonArray>();
  for (uint8_t i = 0; i < count; ++i) {
    const HardwareIoChannelConfig& config = channels[i];
    JsonObject item = array.add<JsonObject>();
    item["id"] = config.id;
    item["name"] = config.name;
    item["type"] = type_name(config.type);
    item["gpio"] = config.gpio;
    if (config.type == HardwareIoType::Relay) {
      item["inverted"] = config.inverted;
      item["boot_state"] = config.boot_on ? "on" : "off";
    } else {
      item["precision"] = config.precision;
    }
  }

  String json;
  serializeJson(doc, json);
#if defined(DEVICE_ESP32_S3_RGB_480)
  Device::ScopedStorageWrite storage_write;
#endif
  fs::FS& fs = Device::storageFS();
  if (!fs.exists(kConfigDir) && !fs.mkdir(kConfigDir)) return false;
  if (fs.exists(kConfigTmpPath)) fs.remove(kConfigTmpPath);

  fs::File file = fs.open(kConfigTmpPath, FILE_WRITE);
  if (!file) return false;
  const size_t written = file.print(json);
  file.flush();
  file.close();
  const bool complete = written == json.length();
  const bool replaced = complete && replace_file_atomically(fs);
  if (!replaced) fs.remove(kConfigTmpPath);
  return replaced;
}

bool HardwareIoManager::replaceFromJson(const String& json, String& error) {
  HardwareIoChannelConfig candidate[kHardwareIoMaxChannels];
  uint8_t candidate_count = 0;
  String candidate_board_variant;
  if (!parseDocument(json, candidate, candidate_count,
                     candidate_board_variant, error, false)) {
    return false;
  }
  if (!saveChannels(candidate, candidate_count, candidate_board_variant)) {
    error = "Could not save hardware configuration";
    return false;
  }

  // Renamed or removed channels must not leave retained ghost topics. Save
  // the old topics before rebuilding runtime state, then clear them with empty
  // retained payloads on this connection or the next one.
  for (uint8_t old = 0; old < channel_count_; ++old) {
    bool topic_kept = false;
    for (uint8_t next = 0; next < candidate_count; ++next) {
      if (channels_[old].id == candidate[next].id) {
        topic_kept = true;
      }
    }
    if (!topic_kept && runtime_[old].state_topic.length()) {
      rememberStaleStateTopic(runtime_[old].state_topic);
    }

    // Clear every obsolete local ID, but never clear an ID still claimed by
    // any candidate as its primary ID or one of the two upgrade aliases.
    const String old_entity_ids[] = {
        make_local_entity_id(channels_[old]),
        make_legacy_name_local_entity_id(channels_[old]),
        make_legacy_channel_local_entity_id(channels_[old]),
    };
    for (size_t old_id_index = 0;
         old_id_index < sizeof(old_entity_ids) / sizeof(old_entity_ids[0]);
         ++old_id_index) {
      const String& old_entity_id = old_entity_ids[old_id_index];
      bool duplicate_old_id = false;
      for (size_t previous = 0; previous < old_id_index; ++previous) {
        if (old_entity_ids[previous].equalsIgnoreCase(old_entity_id)) {
          duplicate_old_id = true;
          break;
        }
      }
      if (duplicate_old_id) continue;

      bool entity_kept = false;
      for (uint8_t next = 0; next < candidate_count; ++next) {
        if (make_local_entity_id(candidate[next]).equalsIgnoreCase(
                old_entity_id) ||
            make_legacy_name_local_entity_id(candidate[next]).equalsIgnoreCase(
                old_entity_id) ||
            make_legacy_channel_local_entity_id(candidate[next])
                .equalsIgnoreCase(old_entity_id)) {
          entity_kept = true;
          break;
        }
      }
      if (!entity_kept) {
        markLocalEntityUnavailable(channels_[old], old_entity_id);
      }
    }
  }

  if (begun_) unsubscribeMqttTopics();
  board_variant_ = candidate_board_variant;
  if (begun_) {
    transitionRuntime(candidate, candidate_count);
  } else {
    channel_count_ = candidate_count;
    for (uint8_t i = 0; i < kHardwareIoMaxChannels; ++i) {
      channels_[i] = i < candidate_count ? candidate[i]
                                         : HardwareIoChannelConfig{};
    }
  }
  syncAllLocalEntityStates(true);
  if (networkManager.isMqttConnected()) subscribeMqttTopics();
  Serial.printf("[HardwareIO] %u mapping(s) saved\n",
                static_cast<unsigned>(channel_count_));
  return true;
}

String HardwareIoManager::toJson(bool include_device_meta) const {
  JsonDocument doc;
  doc["version"] = kConfigVersion;
  doc["max_channels"] = kHardwareIoMaxChannels;
  doc["board_variant"] = board_variant_;
  if (include_device_meta) {
    doc["device_key"] = Device::profile().key;
    doc["device_name"] = Device::displayName();
    doc["entity_prefix"] = local_device_slug();
  }
  JsonArray options = doc["pin_options"].to<JsonArray>();
  size_t option_count = 0;
  const Device::HardwareIoPinOption* pins = pinOptions(option_count);
  for (size_t i = 0; i < option_count; ++i) {
    JsonObject option = options.add<JsonObject>();
    option["gpio"] = pins[i].gpio;
    option["label"] = pins[i].label;
    option["relay"] =
        (pins[i].capabilities & Device::kHardwareIoPinSwitch) != 0;
    option["temperature"] =
        (pins[i].capabilities & Device::kHardwareIoPinTemperature) != 0;
    option["onboard"] =
        (pins[i].capabilities & Device::kHardwareIoPinOnboard) != 0;
    if (pins[i].required_variant && *pins[i].required_variant) {
      option["requires_variant"] = pins[i].required_variant;
    }
    if (pins[i].output_logic == Device::HardwareIoOutputLogic::ActiveHigh) {
      option["fixed_output_logic"] = "high";
    } else if (pins[i].output_logic ==
               Device::HardwareIoOutputLogic::ActiveLow) {
      option["fixed_output_logic"] = "low";
    }
  }
  JsonArray channels = doc["channels"].to<JsonArray>();
  for (uint8_t i = 0; i < channel_count_; ++i) {
    const HardwareIoChannelConfig& config = channels_[i];
    JsonObject item = channels.add<JsonObject>();
    item["id"] = config.id;
    item["entity_id"] = localEntityId(i);
    item["name"] = config.name;
    item["type"] = type_name(config.type);
    item["gpio"] = config.gpio;
    item["inverted"] = config.inverted;
    item["boot_state"] = config.boot_on ? "on" : "off";
    item["precision"] = config.precision;
  }
  String json;
  serializeJson(doc, json);
  return json;
}

void HardwareIoManager::appendBridgeJson(String& json) const {
  json += ",\"local_io\":[";
  for (uint8_t i = 0; i < channel_count_; ++i) {
    if (i) json += ',';
    const HardwareIoChannelConfig& config = channels_[i];
    json += "{\"id\":\"";
    append_json_escaped(json, config.id);
    json += "\",\"entity_id\":\"";
    append_json_escaped(json, make_local_entity_id(config));
    json += "\",\"legacy_entity_ids\":[";
    const String current_entity_id = make_local_entity_id(config);
    const String legacy_name_entity_id =
        make_legacy_name_local_entity_id(config);
    const String legacy_channel_entity_id =
        make_legacy_channel_local_entity_id(config);
    const auto conflicts_with_primary_id = [&](const String& candidate_id) {
      for (uint8_t other = 0; other < channel_count_; ++other) {
        if (make_local_entity_id(channels_[other]).equalsIgnoreCase(
                candidate_id)) {
          return true;
        }
      }
      return false;
    };
    const auto channel_alias_conflicts_with_name_alias = [&]() {
      for (uint8_t other = 0; other < channel_count_; ++other) {
        if (other != i &&
            make_legacy_name_local_entity_id(channels_[other])
                .equalsIgnoreCase(legacy_channel_entity_id)) {
          return true;
        }
      }
      return false;
    };
    bool first_legacy_id = true;
    const auto append_legacy_id = [&](const String& legacy_entity_id) {
      if (!legacy_entity_id.length() ||
          legacy_entity_id.equalsIgnoreCase(current_entity_id)) {
        return;
      }
      if (!first_legacy_id) json += ',';
      json += '"';
      append_json_escaped(json, legacy_entity_id);
      json += '"';
      first_legacy_id = false;
    };
    if (!conflicts_with_primary_id(legacy_name_entity_id)) {
      append_legacy_id(legacy_name_entity_id);
    }
    if (!legacy_channel_entity_id.equalsIgnoreCase(legacy_name_entity_id) &&
        !conflicts_with_primary_id(legacy_channel_entity_id) &&
        !channel_alias_conflicts_with_name_alias()) {
      append_legacy_id(legacy_channel_entity_id);
    }
    json += ']';
    json += ",\"type\":\"";
    json += type_name(config.type);
    json += "\",\"name\":\"";
    append_json_escaped(json, config.name);
    json += '"';
    if (config.type == HardwareIoType::Temperature) {
      json += ",\"unit\":\"C\",\"precision\":";
      json += String(static_cast<unsigned>(config.precision));
    }
    json += '}';
  }
  json += ']';
}

void HardwareIoManager::rebuildMqttTopics() {
  const String base = mqttTopics.deviceBase();
  for (uint8_t i = 0; i < channel_count_; ++i) {
    runtime_[i].local_entity_id = make_local_entity_id(channels_[i]);
    runtime_[i].state_topic = base + "/stat/io/" + channels_[i].id;
    if (channels_[i].type == HardwareIoType::Relay) {
      runtime_[i].command_topic = base + "/cmnd/io/" + channels_[i].id;
    } else {
      runtime_[i].command_topic = "";
    }
  }
}

void HardwareIoManager::unsubscribeMqttTopics() {
  if (!networkManager.isMqttConnected()) return;
  for (uint8_t i = 0; i < channel_count_; ++i) {
    if (channels_[i].type == HardwareIoType::Relay &&
        runtime_[i].command_topic.length()) {
      networkManager.mqttEnqueueUnsubscribe(runtime_[i].command_topic.c_str());
    }
  }
}

void HardwareIoManager::rememberStaleStateTopic(const String& topic) {
  if (!topic.length()) return;
  for (uint8_t i = 0; i < stale_state_topic_count_; ++i) {
    if (stale_state_topics_[i] == topic) return;
  }
  if (stale_state_topic_count_ >= kHardwareIoMaxChannels) {
    Serial.println("[HardwareIO] WARNING: State cleanup list full");
    return;
  }
  stale_state_topics_[stale_state_topic_count_++] = topic;
  stale_state_retry_ms_ = 0;
}

void HardwareIoManager::flushStaleStateTopics() {
  if (!stale_state_topic_count_) return;
  const bool mqtt_connected = networkManager.isMqttConnected();
  uint8_t kept = 0;
  for (uint8_t i = 0; i < stale_state_topic_count_; ++i) {
    const String& topic = stale_state_topics_[i];
    bool active_again = false;
    for (uint8_t channel = 0; channel < channel_count_; ++channel) {
      if (runtime_[channel].state_topic == topic) {
        active_again = true;
        break;
      }
    }
    // A reused ID no longer belongs in the cleanup list; a delayed retry must
    // not clear the new channel state.
    if (active_again) continue;

    if (!mqtt_connected ||
        !networkManager.mqttEnqueuePublish(topic.c_str(), "", true)) {
      if (kept != i) stale_state_topics_[kept] = topic;
      ++kept;
    } else {
      Serial.printf("[HardwareIO] Retained state removed: %s\n",
                    topic.c_str());
    }
  }
  for (uint8_t i = kept; i < stale_state_topic_count_; ++i) {
    stale_state_topics_[i] = "";
  }
  stale_state_topic_count_ = kept;
  stale_state_retry_ms_ = millis() + 5000U;
}

void HardwareIoManager::subscribeMqttTopics() {
  rebuildMqttTopics();
  if (!networkManager.isMqttConnected()) return;
  flushStaleStateTopics();
  for (uint8_t i = 0; i < channel_count_; ++i) {
    if (channels_[i].type == HardwareIoType::Relay &&
        runtime_[i].command_topic.length()) {
      networkManager.mqttEnqueueSubscribe(runtime_[i].command_topic.c_str());
    }
  }
  publishAllStates();
}

void HardwareIoManager::applyRelay(uint8_t index, bool on, bool publish) {
  if (index >= channel_count_ ||
      channels_[index].type != HardwareIoType::Relay) return;
  const HardwareIoChannelConfig& config = channels_[index];
  const int level = on ? (config.inverted ? 0 : 1)
                       : (config.inverted ? 1 : 0);
  gpio_set_level(static_cast<gpio_num_t>(config.gpio), level);
  runtime_[index].relay_state = on;
  if (publish) publishChannelState(index, true);
}

void HardwareIoManager::buildChannelPayload(uint8_t index, char* payload,
                                            size_t payload_size) const {
  if (!payload || payload_size == 0) return;
  payload[0] = '\0';
  if (index >= channel_count_) return;
  const RuntimeChannel& runtime = runtime_[index];
  if (channels_[index].type == HardwareIoType::Relay) {
    snprintf(payload, payload_size, "%s", runtime.relay_state ? "ON" : "OFF");
  } else if (!runtime.temperature_valid || std::isnan(runtime.temperature_c) ||
             std::isinf(runtime.temperature_c)) {
    snprintf(payload, payload_size, "%s", "unavailable");
  } else {
    dtostrf(runtime.temperature_c, 0, channels_[index].precision, payload);
    size_t leading = 0;
    while (payload[leading] == ' ') ++leading;
    if (leading) memmove(payload, payload + leading,
                         strlen(payload + leading) + 1);
  }
}

void HardwareIoManager::markLocalEntityUnavailable(
    const HardwareIoChannelConfig& config, const String& entity_id) {
  if (!entity_id.length()) return;
  haBridgeConfig.updateEntityMeta(
      entity_id, config.name,
      config.type == HardwareIoType::Temperature ? "C" : "",
      "");
  haBridgeConfig.updateSensorValue(entity_id, "unavailable");
  update_local_entity_grids(entity_id, "unavailable");
}

void HardwareIoManager::syncLocalEntityState(uint8_t index,
                                             const char* payload,
                                             bool force) {
  if (index >= channel_count_ || !payload) return;
  RuntimeChannel& runtime = runtime_[index];
  if (!runtime.local_entity_id.length()) {
    runtime.local_entity_id = make_local_entity_id(channels_[index]);
  }
  const String& entity_id = runtime.local_entity_id;
  if (!entity_id.length()) return;
  if (force) {
    haBridgeConfig.updateEntityMeta(
        entity_id, channels_[index].name,
        channels_[index].type == HardwareIoType::Temperature ? "C" : "",
        "");
  }
  if (!force && runtime.last_local_payload[0] &&
      strcmp(payload, runtime.last_local_payload) == 0) {
    return;
  }
  haBridgeConfig.updateSensorValue(entity_id, payload);
  update_local_entity_grids(entity_id, payload);

  // Keep local tiles made with both earlier beta ID formats operational:
  // model + visible name and model + hidden channel ID. Primary IDs always
  // win; the newer name alias wins over the oldest channel-ID alias.
  const String legacy_name_entity_id =
      make_legacy_name_local_entity_id(channels_[index]);
  bool legacy_name_conflicts = false;
  for (uint8_t other = 0; other < channel_count_; ++other) {
    if (other != index &&
        localEntityId(other).equalsIgnoreCase(legacy_name_entity_id)) {
      legacy_name_conflicts = true;
      break;
    }
  }
  if (!legacy_name_conflicts &&
      !legacy_name_entity_id.equalsIgnoreCase(entity_id)) {
    update_local_entity_grids(legacy_name_entity_id, payload);
  }

  const String legacy_channel_entity_id =
      make_legacy_channel_local_entity_id(channels_[index]);
  bool legacy_channel_conflicts = false;
  for (uint8_t other = 0; other < channel_count_; ++other) {
    if (other != index &&
        (localEntityId(other).equalsIgnoreCase(legacy_channel_entity_id) ||
         make_legacy_name_local_entity_id(channels_[other])
             .equalsIgnoreCase(legacy_channel_entity_id))) {
      legacy_channel_conflicts = true;
      break;
    }
  }
  if (!legacy_channel_conflicts &&
      !legacy_channel_entity_id.equalsIgnoreCase(entity_id) &&
      !legacy_channel_entity_id.equalsIgnoreCase(legacy_name_entity_id)) {
    update_local_entity_grids(legacy_channel_entity_id, payload);
  }
  strlcpy(runtime.last_local_payload, payload,
          sizeof(runtime.last_local_payload));
}

void HardwareIoManager::syncAllLocalEntityStates(bool force) {
  for (uint8_t i = 0; i < channel_count_; ++i) {
    char payload[24] = {0};
    buildChannelPayload(i, payload, sizeof(payload));
    syncLocalEntityState(i, payload, force);
  }
}

void HardwareIoManager::refreshLocalEntityCache() {
  syncAllLocalEntityStates(true);
}

void HardwareIoManager::publishChannelState(uint8_t index, bool force) {
  if (index >= channel_count_) return;
  RuntimeChannel& runtime = runtime_[index];
  char payload[24] = {0};
  buildChannelPayload(index, payload, sizeof(payload));
  syncLocalEntityState(index, payload, force);
  if (!networkManager.isMqttConnected()) return;
  const uint32_t now = millis();
  if (!force && runtime.last_payload[0] &&
      strcmp(payload, runtime.last_payload) == 0 &&
      runtime.last_publish_ms != 0 &&
      static_cast<int32_t>(now - runtime.last_publish_ms) <
          static_cast<int32_t>(kStateRepublishMs)) {
    return;
  }
  if (runtime.state_topic.length() &&
      networkManager.mqttEnqueuePublish(runtime.state_topic.c_str(),
                                        payload, true)) {
    strlcpy(runtime.last_payload, payload, sizeof(runtime.last_payload));
    runtime.last_publish_ms = now;
  }
}

void HardwareIoManager::publishAllStates() {
  for (uint8_t i = 0; i < channel_count_; ++i) {
    publishChannelState(i, true);
  }
}

bool HardwareIoManager::handleMqttMessage(
    const char* topic, const uint8_t* payload, size_t length) {
  if (!topic || !payload) return false;
  for (uint8_t i = 0; i < channel_count_; ++i) {
    if (channels_[i].type != HardwareIoType::Relay ||
        !runtime_[i].command_topic.length() ||
        strcmp(topic, runtime_[i].command_topic.c_str()) != 0) {
      continue;
    }
    char command[16];
    const size_t copy_len = min(length, sizeof(command) - 1);
    memcpy(command, payload, copy_len);
    command[copy_len] = '\0';
    String normalized(command);
    normalized.trim();
    normalized.toLowerCase();
    bool state = runtime_[i].relay_state;
    if (normalized == "on" || normalized == "1" || normalized == "true") {
      state = true;
    } else if (normalized == "off" || normalized == "0" ||
               normalized == "false") {
      state = false;
    } else if (normalized == "toggle") {
      state = !state;
    } else {
      Serial.printf("[HardwareIO] Invalid relay command for %s\n",
                    channels_[i].id.c_str());
      return true;
    }
    applyRelay(i, state, true);
    Serial.printf("[HardwareIO] Relay '%s' -> %s\n",
                  channels_[i].id.c_str(), state ? "ON" : "OFF");
    return true;
  }
  return false;
}

bool HardwareIoManager::serviceTemperature(uint8_t index, uint32_t now_ms) {
  if (index >= channel_count_ ||
      channels_[index].type != HardwareIoType::Temperature) return false;
  RuntimeChannel& runtime = runtime_[index];
  const gpio_num_t pin = static_cast<gpio_num_t>(channels_[index].gpio);

  // Read one scratchpad byte per step. Reading all nine bytes blocks the
  // render/touch loop for several milliseconds; separate steps let LVGL run
  // between bytes.
  if (runtime.scratchpad_reading) {
    runtime.scratchpad[runtime.scratchpad_index++] = one_wire_read_byte(pin);
    if (runtime.scratchpad_index < sizeof(runtime.scratchpad)) return true;

    runtime.scratchpad_reading = false;
    runtime.next_action_ms = millis() + kTemperatureSampleMs;
    const bool crc_ok =
        one_wire_crc8(runtime.scratchpad, 8) == runtime.scratchpad[8];
    bool any_nonzero = false;
    for (uint8_t byte : runtime.scratchpad) {
      if (byte != 0) {
        any_nonzero = true;
        break;
      }
    }
    const int16_t raw = static_cast<int16_t>(
        (static_cast<uint16_t>(runtime.scratchpad[1]) << 8) |
        runtime.scratchpad[0]);
    const float value = static_cast<float>(raw) / 16.0f;
    runtime.temperature_valid =
        crc_ok && any_nonzero && value >= -55.0f && value <= 125.0f;
    if (runtime.temperature_valid) runtime.temperature_c = value;
    if (!runtime.temperature_valid) {
      if (runtime.failures < 255) ++runtime.failures;
      publishChannelState(index, runtime.failures == 1);
    } else {
      runtime.failures = 0;
      publishChannelState(index, false);
    }
    return true;
  }

  if (!runtime.conversion_pending &&
      static_cast<int32_t>(now_ms - runtime.next_action_ms) < 0) {
    return false;
  }
  if (!runtime.conversion_pending) {
    if (!one_wire_reset(pin)) {
      runtime.temperature_valid = false;
      runtime.next_action_ms = now_ms + kTemperatureRetryMs;
      if (runtime.failures < 255) ++runtime.failures;
      publishChannelState(index, runtime.failures == 1);
      return true;
    }
    // Each configured channel intentionally represents one bus/sensor.
    // Skip-ROM avoids a long device search on the UI thread.
    one_wire_write_byte(pin, 0xCC);
    one_wire_write_byte(pin, 0x44);
    runtime.conversion_started_ms = now_ms;
    runtime.conversion_pending = true;
    return true;
  }
  if (static_cast<int32_t>(now_ms - runtime.conversion_started_ms) <
      static_cast<int32_t>(kTemperatureConversionMs)) {
    return false;
  }

  runtime.conversion_pending = false;
  if (!one_wire_reset(pin)) {
    runtime.temperature_valid = false;
    runtime.next_action_ms = now_ms + kTemperatureRetryMs;
    if (runtime.failures < 255) ++runtime.failures;
    publishChannelState(index, runtime.failures == 1);
  } else {
    one_wire_write_byte(pin, 0xCC);
    one_wire_write_byte(pin, 0xBE);
    runtime.scratchpad_index = 0;
    memset(runtime.scratchpad, 0, sizeof(runtime.scratchpad));
    runtime.scratchpad_reading = true;
  }
  return true;
}

void HardwareIoManager::service() {
  const uint32_t now = millis();
  if (stale_state_topic_count_ &&
      (stale_state_retry_ms_ == 0 ||
       static_cast<int32_t>(now - stale_state_retry_ms_) >= 0)) {
    flushStaleStateTopics();
  }
  if (!begun_ || channel_count_ == 0) return;
  for (uint8_t n = 0; n < channel_count_; ++n) {
    const uint8_t index = static_cast<uint8_t>(
        (service_cursor_ + n) % channel_count_);
    if (serviceTemperature(index, now)) {
      service_cursor_ = static_cast<uint8_t>((index + 1) % channel_count_);
      return;  // Perform at most one 1-Wire transaction per UI loop pass.
    }
  }
}

bool HardwareIoManager::hasTemperatureChannels() const {
  for (uint8_t i = 0; i < channel_count_; ++i) {
    if (channels_[i].type == HardwareIoType::Temperature) return true;
  }
  return false;
}
