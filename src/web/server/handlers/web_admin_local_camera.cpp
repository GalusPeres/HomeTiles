#include "src/web/server/web_admin.h"

#include "src/devices/device.h"
#include "src/video/local_camera/local_camera.h"
#include "src/video/local_camera/local_camera_stream_contract.h"
#include "src/web/server/handlers/web_admin_handler_utils.h"

namespace {

using local_camera_contract::ImageSettings;

enum class ImageArgs : uint8_t { None, Valid, Invalid };

// One integer image argument. Absent keeps *value; anything but a strict
// integer inside [min_value, max_value] is invalid.
ImageArgs readImageArg(WebServer& server, const char* name, long min_value,
                       long max_value, long* value) {
  if (!server.hasArg(name)) return ImageArgs::None;
  String text = server.arg(name);
  text.trim();
  long parsed = 0;
  if (!local_camera_contract::parseImageInteger(text.c_str(), &parsed) ||
      parsed < min_value || parsed > max_value) {
    return ImageArgs::Invalid;
  }
  *value = parsed;
  return ImageArgs::Valid;
}

// A switch argument: 1/true/on or 0/false/off. Anything else is invalid.
bool readSwitchArg(WebServer& server, const char* name, bool* value) {
  String text = server.arg(name);
  text.trim();
  text.toLowerCase();
  if (text == "1" || text == "true" || text == "on") {
    *value = true;
    return true;
  }
  if (text == "0" || text == "false" || text == "off") {
    *value = false;
    return true;
  }
  return false;
}

// reset=1 starts from the defaults; brightness, contrast, saturation, red,
// blue and gain (Max. gain) then override single values. Every argument is
// validated before anything is applied.
ImageArgs readImageSettings(WebServer& server, ImageSettings* out) {
  using namespace local_camera_contract;
  bool any = false;
  ImageSettings base = local_camera::imageSettings();
  if (server.hasArg("reset")) {
    String value = server.arg("reset");
    value.trim();
    value.toLowerCase();
    if (value != "1" && value != "true" && value != "on") return ImageArgs::Invalid;
    base = ImageSettings{};
    any = true;
  }
  long brightness = base.brightness;
  long contrast = base.contrast;
  long saturation = base.saturation;
  long red = base.red;
  long blue = base.blue;
  long gain = base.gain;
  const ImageArgs results[] = {
      readImageArg(server, "brightness", kImageAdjustMin, kImageAdjustMax, &brightness),
      readImageArg(server, "contrast", kImageAdjustMin, kImageAdjustMax, &contrast),
      readImageArg(server, "saturation", kSaturationMin, kSaturationMax, &saturation),
      readImageArg(server, "red", kImageAdjustMin, kImageAdjustMax, &red),
      readImageArg(server, "blue", kImageAdjustMin, kImageAdjustMax, &blue),
      readImageArg(server, "gain", kGainLimitMin, kGainLimitMax, &gain),
  };
  for (const ImageArgs result : results) {
    if (result == ImageArgs::Invalid) return ImageArgs::Invalid;
    if (result == ImageArgs::Valid) any = true;
  }
  if (!any) return ImageArgs::None;
  *out = makeImageSettings(brightness, contrast, saturation, red, blue, gain);
  return ImageArgs::Valid;
}

}  // namespace

// GET returns the built-in camera status; POST enabled=0|1 saves the opt-in,
// POST mode=<id> the live-stream mode, POST custom_fps=<1-25> and
// custom_quality=<10-90> the Custom mode values, POST mirror=0|1 the mirror,
// POST indicator=0|1|2 the on-display indicator style and the image controls
// (reset, brightness, contrast, saturation, red, blue, gain) their values, each
// immediately. The status JSON is diagnostic data only; every
// user-visible text is rendered from the central translations.
void WebAdminServer::handleLocalCamera() {
  webAdminMarkActivity();
  if (!local_camera::supported() || !Device::kCapabilities.has_builtin_camera) {
    web_admin_handlers::sendJsonError(server, 404,
                                      "Built-in camera is not available on this device");
    return;
  }

  if (server.method() == HTTP_POST) {
    const bool has_enabled = server.hasArg("enabled");
    const bool has_mode = server.hasArg("mode");
    const bool has_mirror = server.hasArg("mirror");
    const bool has_indicator = server.hasArg("indicator");
    ImageSettings image;
    const ImageArgs image_args = readImageSettings(server, &image);
    if (image_args == ImageArgs::Invalid) {
      web_admin_handlers::sendJsonError(server, 400, "Invalid image value");
      return;
    }
    // Custom mode: custom_fps and custom_quality, each optional.
    const local_camera_stream::CustomMode custom_current = local_camera::customMode();
    long custom_fps = custom_current.fps;
    long custom_quality = custom_current.quality;
    const ImageArgs custom_results[] = {
        readImageArg(server, "custom_fps", local_camera_stream::kCustomMinFps,
                     local_camera_stream::kCustomMaxFps, &custom_fps),
        readImageArg(server, "custom_quality", local_camera_stream::kCustomMinQuality,
                     local_camera_stream::kMaxQuality, &custom_quality),
    };
    bool has_custom = false;
    for (const ImageArgs result : custom_results) {
      if (result == ImageArgs::Invalid) {
        web_admin_handlers::sendJsonError(server, 400, "Invalid custom stream value");
        return;
      }
      if (result == ImageArgs::Valid) has_custom = true;
    }
    if (!has_enabled && !has_mode && !has_mirror && !has_indicator && !has_custom &&
        image_args == ImageArgs::None) {
      web_admin_handlers::sendJsonError(server, 400, "Missing enabled value");
      return;
    }

    // Validate every argument before anything is saved, so a rejected request
    // never leaves a partially applied setting behind.
    long mode = -1;
    if (has_mode) {
      String mode_text = server.arg("mode");
      mode_text.trim();
      bool digits = mode_text.length() > 0 && mode_text.length() <= 3;
      for (size_t i = 0; digits && i < mode_text.length(); ++i) {
        digits = isDigit(mode_text[i]);
      }
      mode = digits ? mode_text.toInt() : -1;
      if (mode < 0 || mode > 255 ||
          !local_camera_stream::isKnownMode(static_cast<uint8_t>(mode))) {
        web_admin_handlers::sendJsonError(server, 400, "Invalid mode value");
        return;
      }
    }
    bool mirror = false;
    if (has_mirror && !readSwitchArg(server, "mirror", &mirror)) {
      web_admin_handlers::sendJsonError(server, 400, "Invalid mirror value");
      return;
    }
    bool enable = false;
    if (has_enabled && !readSwitchArg(server, "enabled", &enable)) {
      web_admin_handlers::sendJsonError(server, 400, "Invalid enabled value");
      return;
    }
    // Indicator style: 0 none, 1 line only, 2 line with the pill.
    local_camera::IndicatorStyle indicator = local_camera::IndicatorStyle::Pill;
    if (has_indicator) {
      String text = server.arg("indicator");
      text.trim();
      if (text == "0") {
        indicator = local_camera::IndicatorStyle::None;
      } else if (text == "1") {
        indicator = local_camera::IndicatorStyle::Line;
      } else if (text != "2") {
        web_admin_handlers::sendJsonError(server, 400, "Invalid indicator value");
        return;
      }
    }

    if (image_args == ImageArgs::Valid && !local_camera::setImageSettings(image)) {
      web_admin_handlers::sendJsonError(server, 500, "Could not save the camera setting");
      return;
    }
    // Custom values first, so switching to the Custom mode uses them.
    if (has_custom &&
        !local_camera::setCustomMode(local_camera_stream::makeCustomMode(custom_fps, custom_quality))) {
      web_admin_handlers::sendJsonError(server, 500, "Could not save the camera setting");
      return;
    }
    if (has_mode && !local_camera::setStreamMode(static_cast<uint8_t>(mode))) {
      web_admin_handlers::sendJsonError(server, 500, "Could not save the camera setting");
      return;
    }
    if (has_mirror && !local_camera::setMirror(mirror)) {
      web_admin_handlers::sendJsonError(server, 500, "Could not save the camera setting");
      return;
    }
    if (has_indicator && !local_camera::setIndicatorStyle(indicator)) {
      web_admin_handlers::sendJsonError(server, 500, "Could not save the camera setting");
      return;
    }
    if (has_enabled && !local_camera::setEnabled(enable)) {
      web_admin_handlers::sendJsonError(server, 500, "Could not save the camera setting");
      return;
    }
  }

  String json;
  json.reserve(320);
  local_camera::appendStatusJson(json);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}
