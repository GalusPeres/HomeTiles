#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "src/tiles/icons/mdi_icons.h"

// A media player's icon in its state form like Home Assistant: speaker-play,
// speaker-pause, speaker-off, television-play ... where MDI has one, else the
// icon itself. The tile and the Media popup's head show the same one (user
// 2026-10-23: the tile kept the plain icon while the popup changed).
namespace media_state_icon {

enum class Form : uint8_t { Plain, Play, Pause, Off };

inline Form form_of(const String& raw_state) {
  String state = raw_state;
  state.trim();
  if (state.equalsIgnoreCase("playing")) return Form::Play;
  if (state.equalsIgnoreCase("paused")) return Form::Pause;
  if (state.equalsIgnoreCase("off") || state.equalsIgnoreCase("standby")) return Form::Off;
  return Form::Plain;
}

inline String name(const String& icon_name, Form form) {
  static const char* const kSuffix[] = {nullptr, "-play", "-pause", "-off"};
  const char* suffix = kSuffix[static_cast<uint8_t>(form)];
  if (!icon_name.length() || !suffix) return icon_name;
  const String variant = icon_name + suffix;
  return getMdiCodepoint(variant) != mdiUnknownIconCodepoint() ? variant : icon_name;
}

}  // namespace media_state_icon
