#pragma once

// On-panel wake word detection with a microWakeWord streaming model on
// TensorFlow Lite Micro (esp-tflite-micro of the Arduino core) and the
// vendored TFLM microfrontend (src/audio/wakeword/microfrontend/).
//
// Compiled only on profiles that define HOMETILES_WAKE_WORD
// (speaker_select.h). The engine is used by the speaker worker task only;
// it is not thread-safe. TFLM headers stay inside the .cpp.

#include <stddef.h>
#include <stdint.h>

namespace wake_word {

// One embedded model and the parameters from its microWakeWord manifest.
struct ModelSpec {
  // Wake word id of the Assist contract (assist_contract::kWakeWordIds).
  const char* id;
  const uint8_t* data;
  size_t size;
  float probability_cutoff;
  uint8_t sliding_window_size;
  size_t tensor_arena_size;
  uint8_t feature_step_ms;
};

// Model for a wake word catalog index; nullptr when this build has none.
const ModelSpec* modelForCatalogIndex(size_t index);

class Engine {
 public:
  // Builds the microfrontend and the interpreter for spec. Memory is kept
  // until end(). Returns false (and logs why) when the model cannot run.
  bool begin(const ModelSpec& spec);
  void end();
  bool ready() const { return impl_ != nullptr; }
  const ModelSpec* spec() const { return spec_; }

  // Clears the audio features, the model's streaming state and the
  // detection window; the next warm-up invocations are ignored.
  void reset();

  // Feeds 16 kHz mono samples. Returns true when the wake word was
  // detected; the engine has then already reset itself and the remaining
  // samples of this call were not processed.
  bool process(const int16_t* samples, size_t count);

 private:
  struct Impl;
  Impl* impl_ = nullptr;
  const ModelSpec* spec_ = nullptr;
};

}  // namespace wake_word
