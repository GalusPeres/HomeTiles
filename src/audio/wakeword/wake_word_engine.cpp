#include "src/audio/wakeword/wake_word_engine.h"

#include "src/audio/speaker_select.h"

#if defined(HOMETILES_WAKE_WORD)

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <string.h>

#include <new>

#include "src/audio/assist_contract.h"
#include "src/audio/wakeword/microfrontend/frontend.h"
#include "src/audio/wakeword/microfrontend/frontend_util.h"
#include "src/audio/wakeword/models/okay_nabu_model.h"
#include "src/audio/wakeword/wake_word_detector.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace wake_word {
namespace {

constexpr const char* kLogPrefix = "[WakeWord]";

// Embedded models (src/audio/wakeword/models/PROVENANCE.md).
const ModelSpec kModels[] = {
    {"okay_nabu", wake_word_models::kOkayNabuModel, wake_word_models::kOkayNabuModelSize,
     wake_word_models::kOkayNabuProbabilityCutoff, wake_word_models::kOkayNabuSlidingWindowSize,
     wake_word_models::kOkayNabuTensorArenaSize, wake_word_models::kOkayNabuFeatureStepMs},
};

// Operators of the embedded models, read from the okay_nabu (v2) flatbuffer:
// CALL_ONCE, VAR_HANDLE, RESHAPE, READ_VARIABLE, CONCATENATION,
// STRIDED_SLICE, ASSIGN_VARIABLE, CONV_2D, DEPTHWISE_CONV_2D, SPLIT_V,
// FULLY_CONNECTED, LOGISTIC, QUANTIZE.
constexpr int kOpCount = 13;
using OpResolver = tflite::MicroMutableOpResolver<kOpCount>;

// okay_nabu keeps its streaming state in 6 resource variables (3,472 int8
// values); the variables are allocated from the same arena on the first
// invocation, so the arena gets this much on top of the manifest size.
constexpr int kMaxResourceVariables = 8;
constexpr size_t kVariableArenaBytes = 8 * 1024;
// The arena goes to internal RAM only when this much stays free beside it.
constexpr size_t kInternalReserveBytes = 96 * 1024;
// Invocation cost is logged once per start, at most this often.
constexpr uint32_t kTimingLogIntervalMs = 10u * 60u * 1000u;
constexpr uint32_t kErrorLogIntervalMs = 10000;

uint32_t g_timing_log_ms = 0;
uint32_t g_error_log_ms = 0;

bool logDue(uint32_t* last_ms, uint32_t interval_ms) {
  const uint32_t now_ms = millis();
  if (*last_ms != 0 && static_cast<uint32_t>(now_ms - *last_ms) < interval_ms) return false;
  *last_ms = now_ms == 0 ? 1 : now_ms;
  return true;
}

bool registerOps(OpResolver* resolver) {
  return resolver->AddCallOnce() == kTfLiteOk && resolver->AddVarHandle() == kTfLiteOk &&
         resolver->AddReshape() == kTfLiteOk && resolver->AddReadVariable() == kTfLiteOk &&
         resolver->AddConcatenation() == kTfLiteOk && resolver->AddStridedSlice() == kTfLiteOk &&
         resolver->AddAssignVariable() == kTfLiteOk && resolver->AddConv2D() == kTfLiteOk &&
         resolver->AddDepthwiseConv2D() == kTfLiteOk && resolver->AddSplitV() == kTfLiteOk &&
         resolver->AddFullyConnected() == kTfLiteOk && resolver->AddLogistic() == kTfLiteOk &&
         resolver->AddQuantize() == kTfLiteOk;
}

}  // namespace

const ModelSpec* modelForCatalogIndex(size_t index) {
  if (index >= assist_contract::kWakeWordCount) return nullptr;
  for (const ModelSpec& spec : kModels) {
    if (strcmp(spec.id, assist_contract::kWakeWordIds[index]) == 0) return &spec;
  }
  return nullptr;
}

struct Engine::Impl {
  FrontendState frontend = {};
  bool frontend_ready = false;
  OpResolver resolver;
  const tflite::Model* model = nullptr;
  uint8_t* arena = nullptr;
  size_t arena_size = 0;
  bool arena_internal = false;
  tflite::MicroInterpreter* interpreter = nullptr;
  alignas(tflite::MicroInterpreter) uint8_t interpreter_storage[sizeof(tflite::MicroInterpreter)];
  TfLiteTensor* input = nullptr;
  TfLiteTensor* output = nullptr;
  int stride = 0;
  int rows = 0;
  float input_scale = 0.0f;
  int32_t input_zero_point = 0;
  SlidingWindowDetector detector;
  // Invocation cost since the last reset.
  uint32_t timed_invocations = 0;
  uint64_t invoke_us_total = 0;
  uint32_t invoke_us_max = 0;
  bool timing_reported = false;

  void destroyInterpreter() {
    if (interpreter) interpreter->~MicroInterpreter();
    interpreter = nullptr;
    input = nullptr;
    output = nullptr;
  }

  // (Re)creates the interpreter on a fresh arena: the cheapest way to clear
  // the resource variables that carry the streaming state, since CALL_ONCE
  // initializes them only on the first invocation of an interpreter.
  bool buildInterpreter(const char* id) {
    destroyInterpreter();
    tflite::MicroAllocator* allocator = tflite::MicroAllocator::Create(arena, arena_size);
    if (!allocator) {
      Serial.printf("%s %s: allocator setup failed\n", kLogPrefix, id);
      return false;
    }
    tflite::MicroResourceVariables* variables =
        tflite::MicroResourceVariables::Create(allocator, kMaxResourceVariables);
    if (!variables) {
      Serial.printf("%s %s: resource variables unavailable\n", kLogPrefix, id);
      return false;
    }
    interpreter = new (interpreter_storage)
        tflite::MicroInterpreter(model, resolver, allocator, variables);
    if (interpreter->AllocateTensors() != kTfLiteOk) {
      Serial.printf("%s %s: AllocateTensors failed (arena %u bytes)\n", kLogPrefix, id,
                    static_cast<unsigned>(arena_size));
      destroyInterpreter();
      return false;
    }
    input = interpreter->input(0);
    output = interpreter->output(0);
    if (!input || !output || input->type != kTfLiteInt8 || !input->dims ||
        input->dims->size != 3 || input->dims->data[0] != 1 ||
        input->dims->data[2] != kFeatureChannels || input->dims->data[1] < 1 ||
        input->dims->data[1] > 16 || output->type != kTfLiteUInt8 || output->bytes < 1) {
      Serial.printf("%s %s: unexpected input or output tensor\n", kLogPrefix, id);
      destroyInterpreter();
      return false;
    }
    stride = input->dims->data[1];
    input_scale = input->params.scale;
    input_zero_point = input->params.zero_point;
    memset(input->data.int8, static_cast<int8_t>(input_zero_point),
           static_cast<size_t>(stride) * kFeatureChannels);
    return true;
  }
};

bool Engine::begin(const ModelSpec& spec) {
  end();
  const tflite::Model* model = tflite::GetModel(spec.data);
  if (!model || model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.printf("%s %s: unsupported model schema\n", kLogPrefix, spec.id);
    return false;
  }
  void* memory = heap_caps_calloc(1, sizeof(Impl), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) {
    Serial.printf("%s %s: out of memory\n", kLogPrefix, spec.id);
    return false;
  }
  Impl* impl = new (memory) Impl();
  impl->model = model;
  bool ok = registerOps(&impl->resolver);
  if (!ok) Serial.printf("%s %s: operator registration failed\n", kLogPrefix, spec.id);

  if (ok) {
    FrontendConfig config;
    FrontendFillConfigWithDefaults(&config);
    config.window.size_ms = kWindowMs;
    config.window.step_size_ms = spec.feature_step_ms;
    config.filterbank.num_channels = kFeatureChannels;
    config.filterbank.lower_band_limit = kLowerBandHz;
    config.filterbank.upper_band_limit = kUpperBandHz;
    config.noise_reduction.smoothing_bits = kNoiseSmoothingBits;
    config.noise_reduction.even_smoothing = kNoiseEvenSmoothing;
    config.noise_reduction.odd_smoothing = kNoiseOddSmoothing;
    config.noise_reduction.min_signal_remaining = kNoiseMinSignalRemaining;
    config.pcan_gain_control.enable_pcan = 1;
    config.pcan_gain_control.strength = kPcanStrength;
    config.pcan_gain_control.offset = kPcanOffset;
    config.pcan_gain_control.gain_bits = kPcanGainBits;
    config.log_scale.enable_log = 1;
    config.log_scale.scale_shift = kLogScaleShift;
    ok = FrontendPopulateState(&config, &impl->frontend, kSampleRate) != 0;
    impl->frontend_ready = ok;
    if (!ok) Serial.printf("%s %s: microfrontend setup failed\n", kLogPrefix, spec.id);
  }

  if (ok) {
    impl->arena_size = (spec.tensor_arena_size + kVariableArenaBytes + 15u) & ~static_cast<size_t>(15u);
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >=
        impl->arena_size + kInternalReserveBytes) {
      impl->arena = static_cast<uint8_t*>(
          heap_caps_aligned_alloc(16, impl->arena_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
      impl->arena_internal = impl->arena != nullptr;
    }
    if (!impl->arena) {
      impl->arena = static_cast<uint8_t*>(
          heap_caps_aligned_alloc(16, impl->arena_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    ok = impl->arena != nullptr;
    if (!ok) Serial.printf("%s %s: tensor arena unavailable\n", kLogPrefix, spec.id);
  }

  if (ok) ok = impl->buildInterpreter(spec.id);
  impl_ = impl;
  spec_ = &spec;
  if (!ok) {
    end();
    return false;
  }
  impl->detector.configure(spec.probability_cutoff, spec.sliding_window_size);
  impl->detector.reset(kWarmupInvocations);
  Serial.printf("%s %s ready: %d feature rows per invocation, arena %u bytes (%s), cutoff %u/255, "
                "window %u\n",
                kLogPrefix, spec.id, impl->stride, static_cast<unsigned>(impl->arena_size),
                impl->arena_internal ? "internal" : "PSRAM",
                static_cast<unsigned>(impl->detector.cutoff()),
                static_cast<unsigned>(impl->detector.window()));
  return true;
}

void Engine::end() {
  if (impl_) {
    impl_->destroyInterpreter();
    if (impl_->frontend_ready) FrontendFreeStateContents(&impl_->frontend);
    heap_caps_free(impl_->arena);
    impl_->~Impl();
    heap_caps_free(impl_);
  }
  impl_ = nullptr;
  spec_ = nullptr;
}

void Engine::reset() {
  if (!impl_ || !spec_) return;
  FrontendReset(&impl_->frontend);
  impl_->rows = 0;
  impl_->detector.reset(kWarmupInvocations);
  impl_->timed_invocations = 0;
  impl_->invoke_us_total = 0;
  impl_->invoke_us_max = 0;
  impl_->timing_reported = false;
  if (!impl_->buildInterpreter(spec_->id) && logDue(&g_error_log_ms, kErrorLogIntervalMs)) {
    Serial.printf("%s %s: reset failed, detection paused\n", kLogPrefix, spec_->id);
  }
}

bool Engine::process(const int16_t* samples, size_t count) {
  if (!impl_ || !impl_->interpreter || !samples) return false;
  Impl& e = *impl_;
  while (count > 0) {
    size_t read = 0;
    const FrontendOutput features = FrontendProcessSamples(&e.frontend, samples, count, &read);
    samples += read;
    count -= read;
    if (!features.values || features.size != static_cast<size_t>(kFeatureChannels)) {
      if (read == 0) break;
      continue;
    }
    int8_t* row = e.input->data.int8 + static_cast<size_t>(e.rows) * kFeatureChannels;
    for (int c = 0; c < kFeatureChannels; ++c) {
      row[c] = quantizeFeature(features.values[c], e.input_scale, e.input_zero_point);
    }
    if (++e.rows < e.stride) continue;
    e.rows = 0;

    const int64_t started_us = esp_timer_get_time();
    const TfLiteStatus status = e.interpreter->Invoke();
    const uint32_t elapsed_us = static_cast<uint32_t>(esp_timer_get_time() - started_us);
    if (status != kTfLiteOk) {
      if (logDue(&g_error_log_ms, kErrorLogIntervalMs)) {
        Serial.printf("%s %s: Invoke failed\n", kLogPrefix, spec_->id);
      }
      reset();
      return false;
    }
    if (!e.timing_reported) {
      ++e.timed_invocations;
      e.invoke_us_total += elapsed_us;
      if (elapsed_us > e.invoke_us_max) e.invoke_us_max = elapsed_us;
      if (e.timed_invocations >= kWarmupInvocations) {
        e.timing_reported = true;
        if (logDue(&g_timing_log_ms, kTimingLogIntervalMs)) {
          Serial.printf("%s %s: invoke avg %lu us, max %lu us over %lu runs (every %u ms of "
                        "audio), arena used %u of %u bytes\n",
                        kLogPrefix, spec_->id,
                        static_cast<unsigned long>(e.invoke_us_total / e.timed_invocations),
                        static_cast<unsigned long>(e.invoke_us_max),
                        static_cast<unsigned long>(e.timed_invocations),
                        static_cast<unsigned>(e.stride * spec_->feature_step_ms),
                        static_cast<unsigned>(e.interpreter->arena_used_bytes()),
                        static_cast<unsigned>(e.arena_size));
        }
      }
    }
    if (e.detector.push(e.output->data.uint8[0])) {
      Serial.printf("%s %s detected (mean %u/255)\n", kLogPrefix, spec_->id,
                    static_cast<unsigned>(e.detector.mean()));
      reset();
      return true;
    }
  }
  return false;
}

}  // namespace wake_word

#endif  // defined(HOMETILES_WAKE_WORD)
