# Embedded wake word models

Flash data for the on-panel wake word; compiled only on profiles that define
`HOMETILES_WAKE_WORD` (`src/audio/speaker_select.h`).

## okay_nabu (microWakeWord v2)

- Source: https://github.com/esphome/micro-wake-word-models, commit
  `40ff33f57f8fc6ad71a75ef085abbf742495b225`, files
  `models/v2/okay_nabu.tflite` and `models/v2/okay_nabu.json` (the manifest,
  copied unchanged as `okay_nabu.json`).
- Model author: Kevin Ahrendt. License: Apache-2.0, see
  `LICENSE-APACHE-2.0.txt` (the repository's `LICENSE`).
- `okay_nabu.tflite`: 60,264 bytes, SHA256
  `0689abe1912a95a3318a0d8cb2e67bad0cbcfe3e24dd6e050c75debddfb6f891`.
- `okay_nabu_model.{h,cpp}` are generated from both files by
  `node tools/generate-wake-word-model.mjs <okay_nabu.tflite> <okay_nabu.json> okay_nabu`;
  the bytes are unchanged, stored as a 16-byte aligned const array.
- Manifest parameters: probability_cutoff 0.97, sliding_window_size 5,
  tensor_arena_size 26080, feature_step_size 10 ms.
- Read from the flatbuffer: input int8 [1, 3, 40] (scale 0.10196079, zero
  point -128), output uint8 [1, 1] (scale 1/256); operators CALL_ONCE,
  VAR_HANDLE, RESHAPE, READ_VARIABLE, CONCATENATION, STRIDED_SLICE,
  ASSIGN_VARIABLE, CONV_2D, DEPTHWISE_CONV_2D, SPLIT_V, FULLY_CONNECTED,
  LOGISTIC, QUANTIZE; 6 resource variables (3,472 int8 values of streaming
  state).

## hey_jarvis and alexa (microWakeWord v2)

- Source: the same repository and commit as okay_nabu, files
  `models/v2/hey_jarvis.{tflite,json}` and `models/v2/alexa.{tflite,json}`
  (manifests copied unchanged). Model author and license as above.
- `hey_jarvis.tflite`: 52,272 bytes, SHA256
  `21a7976add39ee24ec96c63d96b7aaa18e24d1d9824b963e451da8feb4b78b77`;
  probability_cutoff 0.97, sliding_window_size 5, tensor_arena_size 22860,
  feature_step_size 10 ms; 6 resource variables (2,840 int8 values).
- `alexa.tflite`: 55,856 bytes, SHA256
  `9011a8155b04de858c48038529235cbc0e42e9fca05a55bf588cb80a653a723b`;
  probability_cutoff 0.9, sliding_window_size 5, tensor_arena_size 22348,
  feature_step_size 10 ms; 6 resource variables (3,024 int8 values).
- Both have the okay_nabu input and output tensors and its operators except
  SPLIT_V. Generated the same way, e.g.
  `node tools/generate-wake-word-model.mjs <alexa.tflite> <alexa.json> alexa`.

## Feature and inference conventions

From https://github.com/kahrendt/microWakeWord (Apache-2.0), commit
`4665173cd35f1cff9a61e06fc427f124766c488e`: `microwakeword/audio/audio_utils.py`
(microfrontend settings: 16 kHz, 30 ms window, 40 channels, 125..7500 Hz,
PCAN on, min_signal_remaining 0.05) and `microwakeword/inference.py` (uint16
features times 0.0390625, quantized with the input scale and zero point;
`stride` feature rows per invocation; uint8 output as probability / 255).
No code was copied; `src/audio/wakeword/wake_word_detector.h` and
`wake_word_engine.cpp` implement these conventions.
