# TFLM audio microfrontend for the on-panel wake word

Feature extraction for the microWakeWord models (`src/audio/wakeword/`):
30 ms window, 10 ms step, 40 mel filterbank channels 125..7500 Hz, noise
reduction, PCAN gain control and log scale, producing one uint16 frame of 40
features per step. The Arduino core's esp-tflite-micro does not ship it.

Arduino compiles every `.c`/`.cpp` file under `src/` on every profile; the
code is plain portable C/C++ and the linker drops it where nothing calls it
(only `HOMETILES_WAKE_WORD` profiles, `src/audio/speaker_select.h`).

## TensorFlow Lite Micro microfrontend

- Source: https://github.com/tensorflow/tflite-micro, commit
  `18811372beeed5d8a05f9b356f079a369c099540`, directory
  `tensorflow/lite/experimental/microfrontend/lib/`.
- License: Apache-2.0, see `LICENSE-APACHE-2.0.txt` (the repository's
  `LICENSE`). Each file keeps its original copyright notice.
- Files: `bits.h`, `fft.{h,cc}`, `fft_util.{h,cc}`, `filterbank.{c,h}`,
  `filterbank_util.{c,h}`, `frontend.{c,h}`, `frontend_util.{c,h}`,
  `kiss_fft_common.h`, `kiss_fft_int16.{h,cc}`, `log_lut.{c,h}`,
  `log_scale.{c,h}`, `log_scale_util.{c,h}`, `noise_reduction.{c,h}`,
  `noise_reduction_util.{c,h}`, `pcan_gain_control.{c,h}`,
  `pcan_gain_control_util.{c,h}`, `window.{c,h}`, `window_util.{c,h}`.
  The `_io`, `_main`, `_memmap` and `_test` files were left out.

Changes:

- `#include "tensorflow/lite/experimental/microfrontend/lib/<file>"`
  became `#include "<file>"` (the files sit side by side here).
- `fft.cc`, `fft_util.cc` and `kiss_fft_int16.cc` were renamed to `.cpp`:
  the Arduino build and `tools/fast-build.mjs` compile only `.c`, `.cpp`
  and `.S` sketch sources.
- The 16-bit kissfft namespace `kissfft_fixed16` became
  `hometiles_kissfft_fixed16`, and `kiss_fft_int16.{h,cpp}` include the
  vendored kissfft below, so neither clashes with a kissfft that the
  Arduino core links.

## kissfft (fixed-point FFT used by `fft.cpp`)

- Source: https://github.com/mborgerding/kissfft, tag `v130`, commit
  `918336beac80868ad3011cb283063bb224e3e5da` (the version TFLM pins in
  `tensorflow/lite/micro/tools/make/third_party_downloads.inc`), with TFLM's
  `third_party/kissfft/kissfft.patch` (same TFLM commit) applied: include
  guard in `_kiss_fft_guts.h`, `extern "C++"` linkage, no heap allocation in
  `kiss_fft_alloc`, no `fprintf`/`exit` in `kiss_fftr`.
- License: BSD-3-Clause, see `kissfft/COPYING-BSD-3-Clause.txt` (the
  repository's `COPYING`).
- Files in `kissfft/`: `kiss_fft.h`, `_kiss_fft_guts.h`, `kiss_fftr.h`
  (from `tools/`), `kiss_fft_impl.inc.h` (was `kiss_fft.c`) and
  `kiss_fftr_impl.inc.h` (was `tools/kiss_fftr.c`). The two sources were
  renamed so Arduino does not compile them as standalone (float, global
  symbol) translation units; `kiss_fft_int16.cpp` includes them inside the
  16-bit fixed-point namespace, exactly as TFLM's `kiss_fft_int16.cc` does
  with the originals.
- Trailing whitespace was removed from the kissfft files (`git diff --check`).
