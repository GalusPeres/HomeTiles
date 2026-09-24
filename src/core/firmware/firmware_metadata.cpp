#include "src/core/firmware/firmware_metadata.h"
#include "src/devices/device_select.h"

#include <sdkconfig.h>
#include <string.h>

#if defined(DEVICE_M5STACKS_TAB5)
#define FW_META_TARGET_DEVICE_KEY "m5stacks_tab5"
#define FW_META_TARGET_DISPLAY_NAME "M5Stacks Tab5"
#elif defined(DEVICE_GUITION_JC8012P4A1)
#define FW_META_TARGET_DEVICE_KEY "guition_jc8012p4a1"
#define FW_META_TARGET_DISPLAY_NAME "Guition JC8012P4A1"
#elif defined(DEVICE_GUITION_JC8012P4A1_V2)
#define FW_META_TARGET_DEVICE_KEY "guition_jc8012p4a1_v2"
#define FW_META_TARGET_DISPLAY_NAME "Guition JC8012P4A1 V2"
#elif defined(DEVICE_GUITION_JC1060P470C)
#define FW_META_TARGET_DEVICE_KEY "guition_jc1060p470c"
#define FW_META_TARGET_DISPLAY_NAME "Guition JC1060P470C"
#elif defined(DEVICE_GUITION_JC1060P470C_V2)
#define FW_META_TARGET_DEVICE_KEY "guition_jc1060p470c_v2"
#define FW_META_TARGET_DISPLAY_NAME "Guition JC1060P470C V2"
#elif defined(DEVICE_GUITION_JC4880P443_PORTRAIT)
#define FW_META_TARGET_DEVICE_KEY "guition_jc4880p443_portrait"
#define FW_META_TARGET_DISPLAY_NAME "Guition JC4880P443 Portrait"
#elif defined(DEVICE_GUITION_ESP32_4848S040)
#define FW_META_TARGET_DEVICE_KEY "guition_esp32_4848s040"
#define FW_META_TARGET_DISPLAY_NAME "GUITION ESP32-4848S040"
#elif defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4)
#define FW_META_TARGET_DEVICE_KEY "waveshare_s3_touch_lcd_4"
#define FW_META_TARGET_DISPLAY_NAME "Waveshare ESP32-S3 Touch LCD 4"
#elif defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4B)
#define FW_META_TARGET_DEVICE_KEY "waveshare_s3_touch_lcd_4b"
#define FW_META_TARGET_DISPLAY_NAME "Waveshare ESP32-S3 Touch LCD 4B"
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_4_3)
#define FW_META_TARGET_DEVICE_KEY "waveshare_touch_lcd_4_3"
#define FW_META_TARGET_DISPLAY_NAME "Waveshare Touch LCD 4.3"
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_7)
#define FW_META_TARGET_DEVICE_KEY "waveshare_touch_lcd_7"
#define FW_META_TARGET_DISPLAY_NAME "Waveshare Touch LCD 7"
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)
#define FW_META_TARGET_DEVICE_KEY "waveshare_touch_lcd_7b"
#define FW_META_TARGET_DISPLAY_NAME "Waveshare Touch LCD 7B"
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_8)
#define FW_META_TARGET_DEVICE_KEY "waveshare_touch_lcd_8"
#define FW_META_TARGET_DISPLAY_NAME "Waveshare Touch LCD 8"
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
#define FW_META_TARGET_DEVICE_KEY "waveshare_touch_lcd_10_1"
#define FW_META_TARGET_DISPLAY_NAME "Waveshare Touch LCD 10.1"
#elif defined(DEVICE_LAYOUT_TEST_1024X600)
#define FW_META_TARGET_DEVICE_KEY "layout_test_1024x600"
#define FW_META_TARGET_DISPLAY_NAME "Layout Test 1024x600"
#elif defined(DEVICE_LAYOUT_TEST_480X480)
#define FW_META_TARGET_DEVICE_KEY "layout_test_480x480"
#define FW_META_TARGET_DISPLAY_NAME "Layout Test 480x480"
#elif defined(DEVICE_WAVESHARE_4B)
#define FW_META_TARGET_DEVICE_KEY "waveshare_4b"
#define FW_META_TARGET_DISPLAY_NAME "Waveshare B4"
#else
#define FW_META_TARGET_DEVICE_KEY "unknown"
#define FW_META_TARGET_DISPLAY_NAME "Unknown"
#endif

// v0.3.3 wrote "unknown" into its descriptor because this source file did not
// include device_select.h. The v0.3.5 release needs the same descriptor once so
// those already-installed panels can bootstrap themselves through OTA.
#if defined(HOMETILES_OTA_BOOTSTRAP_METADATA)
#define FW_META_DEVICE_KEY "unknown"
#define FW_META_DISPLAY_NAME "Unknown"
#else
#define FW_META_DEVICE_KEY FW_META_TARGET_DEVICE_KEY
#define FW_META_DISPLAY_NAME FW_META_TARGET_DISPLAY_NAME
#endif

#define FW_META_PROJECT_KEY "esp32_p4_homeassistant_display"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#if CONFIG_ESP_REV_MAX_FULL < 300
#define FW_META_SILICON_VARIANT "pre_v3"
#define FW_META_SILICON_MIN_REV CONFIG_ESP_REV_MIN_FULL
#define FW_META_SILICON_MAX_REV CONFIG_ESP_REV_MAX_FULL
#elif CONFIG_ESP_REV_MIN_FULL >= 300 && defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)
#if CONFIG_ESP_REV_MIN_FULL > 301 || CONFIG_ESP_REV_MAX_FULL < 301
#error "The experimental Waveshare 7B build must include ESP32-P4 revision 3.1"
#endif
#define FW_META_SILICON_VARIANT "rev3_1"
#define FW_META_SILICON_MIN_REV 301
#define FW_META_SILICON_MAX_REV 301
#elif CONFIG_ESP_REV_MIN_FULL >= 300 && defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
// Arduino-ESP32 3.3.7 "v3.00 or newer" builds boot ESP32-P4 v3.1 to v3.99.
// Keep the HomeTiles contract identical to that image header so an installed
// v3 panel can still accept a later image through the subset range check.
#if CONFIG_ESP_REV_MIN_FULL != 301 || CONFIG_ESP_REV_MAX_FULL != 399
#error "The Waveshare 10.1 v3 build must target ESP32-P4 revisions 3.1 to 3.99"
#endif
#define FW_META_SILICON_VARIANT "post_v3"
#define FW_META_SILICON_MIN_REV 301
#define FW_META_SILICON_MAX_REV 399
#else
#error "Every ESP32-P4 build must target one unambiguous silicon generation"
#endif
#else
#define FW_META_SILICON_VARIANT "default"
#define FW_META_SILICON_MIN_REV 0
#define FW_META_SILICON_MAX_REV UINT16_MAX
#endif

namespace firmware_meta {
namespace {

constexpr uint8_t kEspImageHeaderMagic = 0xE9;
constexpr uint32_t kEspAppDescMagicWord = 0xABCD5432u;
constexpr size_t kEspImageHeaderSize = 24;
constexpr size_t kEspImageSegmentHeaderSize = 8;
constexpr size_t kEspAppDescOffset = kEspImageHeaderSize + kEspImageSegmentHeaderSize;
constexpr size_t kEspAppDescSize = 256;

struct EmbeddedFirmwareDescriptor {
  DeviceDescriptor device;
  SiliconRevisionDescriptor silicon;
} __attribute__((packed));

inline const EmbeddedFirmwareDescriptor kCurrentFirmwareDescriptor
    __attribute__((used, section(".rodata_custom_desc"))) = {
        {
            kDeviceDescriptorMagic,
            FW_META_PROJECT_KEY,
            FW_META_DEVICE_KEY,
            FW_META_DISPLAY_NAME,
        },
        {
            kSiliconRevisionDescriptorMagic,
            FW_META_SILICON_MIN_REV,
            FW_META_SILICON_MAX_REV,
            FW_META_SILICON_VARIANT,
        },
};

static_assert(sizeof(EmbeddedFirmwareDescriptor) ==
                  sizeof(DeviceDescriptor) + sizeof(SiliconRevisionDescriptor),
              "Firmware descriptors must stay tightly packed");

uint32_t readU32LE(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

}  // namespace

const DeviceDescriptor& currentDeviceDescriptor() {
  return kCurrentFirmwareDescriptor.device;
}

const SiliconRevisionDescriptor& currentSiliconRevisionDescriptor() {
  return kCurrentFirmwareDescriptor.silicon;
}

const char* currentProjectKey() {
  return kCurrentFirmwareDescriptor.device.project_key;
}

const char* currentDeviceKey() {
  return kCurrentFirmwareDescriptor.device.device_key;
}

const char* currentDisplayName() {
  return kCurrentFirmwareDescriptor.device.display_name;
}

const char* currentSiliconVariant() {
  return kCurrentFirmwareDescriptor.silicon.variant;
}

bool matchesCurrentDeviceKey(const char* incoming_device_key) {
  if (!incoming_device_key || !*incoming_device_key) return false;

  if (strcmp(kCurrentFirmwareDescriptor.device.device_key, "unknown") == 0) {
    // A bootstrap image accepts its own legacy descriptor and the exact target
    // selected at build time. It never accepts another device's image.
    return strcmp(incoming_device_key, "unknown") == 0 ||
           strcmp(incoming_device_key, FW_META_TARGET_DEVICE_KEY) == 0;
  }

  return strcmp(incoming_device_key,
                kCurrentFirmwareDescriptor.device.device_key) == 0;
}

bool matchesCurrentSiliconVariant(const char* incoming_variant) {
  if (strcmp(kCurrentFirmwareDescriptor.silicon.variant, "default") == 0) {
    return true;
  }
  return incoming_variant &&
         strcmp(incoming_variant,
                kCurrentFirmwareDescriptor.silicon.variant) == 0;
}

bool matchesCurrentSiliconRevisionRange(
    uint16_t incoming_minimum_revision,
    uint16_t incoming_maximum_revision) {
  return incoming_minimum_revision <= incoming_maximum_revision &&
         incoming_minimum_revision >=
             kCurrentFirmwareDescriptor.silicon.minimum_revision &&
         incoming_maximum_revision <=
             kCurrentFirmwareDescriptor.silicon.maximum_revision;
}

const char* expectedDeviceDisplayName() {
  if (strcmp(kCurrentFirmwareDescriptor.device.device_key, "unknown") == 0) {
    return FW_META_TARGET_DISPLAY_NAME;
  }
  return kCurrentFirmwareDescriptor.device.display_name;
}

bool parseDeviceDescriptorFromImage(const uint8_t* image_data, size_t image_len, DeviceDescriptor& out) {
  if (!image_data || image_len < kDeviceDescriptorImageBytes) {
    return false;
  }
  if (image_data[0] != kEspImageHeaderMagic) {
    return false;
  }

  const uint8_t* app_desc = image_data + kEspAppDescOffset;
  if (readU32LE(app_desc) != kEspAppDescMagicWord) {
    return false;
  }

  memcpy(&out, image_data + kDeviceDescriptorImageOffset, sizeof(DeviceDescriptor));
  if (out.magic_word != kDeviceDescriptorMagic) {
    return false;
  }

  out.device_key[kDeviceKeyMaxLen - 1] = '\0';
  out.display_name[kDisplayNameMaxLen - 1] = '\0';
  return true;
}

bool parseSiliconRevisionDescriptorFromImage(
    const uint8_t* image_data,
    size_t image_len,
    SiliconRevisionDescriptor& out) {
  if (!image_data || image_len < kDeviceDescriptorImageBytes) {
    return false;
  }
  memcpy(&out,
         image_data + kSiliconRevisionDescriptorImageOffset,
         sizeof(SiliconRevisionDescriptor));
  if (out.magic_word != kSiliconRevisionDescriptorMagic) {
    return false;
  }
  out.variant[kSiliconVariantMaxLen - 1] = '\0';
  return out.minimum_revision <= out.maximum_revision;
}

bool imageMatchesCurrentSiliconVariant(
    const uint8_t* image_data,
    size_t image_len,
    bool* accepted_legacy_descriptor) {
  if (accepted_legacy_descriptor) {
    *accepted_legacy_descriptor = false;
  }

  const uint16_t chip_revision = ESP.getChipRevision();
  if (chip_revision < kCurrentFirmwareDescriptor.silicon.minimum_revision ||
      chip_revision > kCurrentFirmwareDescriptor.silicon.maximum_revision) {
    return false;
  }

  SiliconRevisionDescriptor incoming{};
  if (parseSiliconRevisionDescriptorFromImage(image_data, image_len, incoming)) {
    return matchesCurrentSiliconVariant(incoming.variant) &&
           matchesCurrentSiliconRevisionRange(incoming.minimum_revision,
                                              incoming.maximum_revision) &&
           chip_revision >= incoming.minimum_revision &&
           chip_revision <= incoming.maximum_revision;
  }

  const char* current_variant = currentSiliconVariant();
  const bool legacy_is_safe = strcmp(current_variant, "default") == 0 ||
                              strcmp(current_variant, "pre_v3") == 0;
  if (legacy_is_safe && accepted_legacy_descriptor) {
    *accepted_legacy_descriptor = true;
  }
  return legacy_is_safe;
}

}  // namespace firmware_meta
