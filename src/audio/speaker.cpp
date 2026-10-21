#include "src/audio/speaker.h"

#include "src/audio/speaker_select.h"

#include <atomic>

#include "src/audio/assist_contract.h"
#include "src/audio/speaker_contract.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/network_manager.h"

#if defined(HOMETILES_SPEAKER)
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <mbedtls/platform.h>

#include <cerrno>

#include "src/audio/helix_mp3/mp3dec.h"
#include "src/audio/assist_request.h"
#include "src/audio/audio_stream_contract.h"
#include "src/audio/audio_board.h"
#include "src/audio/codecs/audio_chips.h"
#include "src/audio/mic_level.h"
#include "src/audio/speaker_request.h"
#include "src/audio/wakeword/wake_word_detector.h"
#include "src/audio/wakeword/wake_word_engine.h"
#include "src/core/config/batched_nvs_write.h"
#include "src/devices/device.h"
#include "src/video/camera_stream.h"
#include "src/video/tcp_ack_socket.h"
#include HOMETILES_SPEAKER_BOARD
#endif

namespace speaker {
namespace {

using namespace speaker_contract;
// Assist names stay qualified: both contracts define ErrorCode and friends.
namespace assist = assist_contract;

#if defined(HOMETILES_SPEAKER)
constexpr bool kSupported = true;
#else
constexpr bool kSupported = false;
#endif

std::atomic<bool> g_codec_ready{false};
std::atomic<uint8_t> g_public_state{static_cast<uint8_t>(PublicState::Idle)};
std::atomic<uint8_t> g_error{static_cast<uint8_t>(ErrorCode::None)};
std::atomic<float> g_volume{kDefaultVolume};
std::atomic<bool> g_muted{false};
// Bumped by every change of the retained status; service() publishes it.
std::atomic<uint32_t> g_state_generation{0};
// The clip's playback position and length (0 = not known) for the status.
// Set by the worker; published only with a status change, the Bridge moves
// the position on itself while a clip plays.
std::atomic<uint32_t> g_position_ms{0};
std::atomic<uint32_t> g_duration_ms{0};
// Home Assistant's pause: set by the loop task, followed by the worker.
std::atomic<bool> g_pause_requested{false};
uint32_t g_published_generation = 0;
// Bridge capability bits: 1 = audio_output, 2 = audio_input.
uint8_t g_last_capability = 0;

// Assist microphone capture (assist_contract.h).
std::atomic<bool> g_mic_ready{false};
// Privacy switch (stored, off on a new panel): while false nothing reads the
// microphone.
std::atomic<bool> g_mic_enabled{false};
// ES7210 mic gain step (stored), applied whenever the input opens.
std::atomic<uint8_t> g_mic_gain{kDefaultMicGainStep};
std::atomic<uint8_t> g_assist_state{static_cast<uint8_t>(assist::PublicState::Idle)};
std::atomic<uint8_t> g_assist_error{static_cast<uint8_t>(assist::ErrorCode::None)};
std::atomic<uint32_t> g_assist_state_generation{0};
uint32_t g_assist_published_generation = 0;

// On-panel wake word (assist_contract.h addendum): the chosen words (catalog
// mask) and the listen switch; set on the loop task, read by the worker.
std::atomic<uint32_t> g_wake_chosen{assist::kDefaultWakeWordMask};
std::atomic<bool> g_wake_listen{false};

assist::WakeSetting wakeSetting() {
  assist::WakeSetting setting;
  setting.chosen = g_wake_chosen.load();
  setting.listen = g_wake_listen.load();
  return setting;
}
// The model could not run; the status then offers no wake words.
std::atomic<bool> g_wake_failed{false};

#if defined(HOMETILES_WAKE_WORD)
constexpr uint32_t kCompiledWakeWords = assist::kAllWakeWordsMask;
#else
constexpr uint32_t kCompiledWakeWords = 0;
#endif

uint32_t availableWakeWords() { return g_wake_failed.load() ? 0 : kCompiledWakeWords; }

#if defined(HOMETILES_SPEAKER)

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
// https uses the same unverified TLS client as the screensaver image URL,
// which the 7B downloads reliably; Home Assistant often has only an https URL.
constexpr bool kHttpsAllowed = true;

constexpr const char* kLogPrefix = "[Speaker]";
constexpr const char* kPrefsNamespace = "speaker";
constexpr const char* kPrefsVolumeKey = "volume";
constexpr const char* kPrefsMutedKey = "muted";
// Comma-separated chosen wake word ids (names, not catalog bits) and the
// listen switch.
constexpr const char* kPrefsWakeWordsKey = "wake_words";
constexpr const char* kPrefsWakeListenKey = "wake_listen";
constexpr const char* kPrefsMicKey = "mic_on";
constexpr const char* kPrefsMicGainKey = "mic_gain";
// Volume sliders send many steps; store once they settle and nothing plays
// (flash writes stall both cores and would click in the output).
constexpr uint32_t kPersistDelayMs = 3000;

// TensorFlow Lite Micro inference runs on this stack too (PSRAM).
constexpr uint32_t kWorkerStackBytes = 12288;
// Above the loop task so LVGL rendering cannot starve the I2S DMA.
constexpr UBaseType_t kWorkerPriority = 3;
constexpr uint32_t kNotifyPlay = 1u << 0;
constexpr uint32_t kNotifyVolume = 1u << 1;
constexpr uint32_t kNotifyShutdown = 1u << 2;
constexpr uint32_t kNotifyAssist = 1u << 3;
constexpr uint32_t kNotifyWake = 1u << 4;
constexpr uint32_t kShutdownWaitMs = 1500;

constexpr uint32_t kHttpTimeoutMs = 5000;
// A stream that sends nothing for this long is gone.
constexpr uint32_t kStreamIdleMs = 10000;
constexpr size_t kReadChunkBytes = 4096;
// Network bytes taken per decoder step, so a fast download (a TTS clip on
// the LAN) cannot starve the I2S output.
constexpr size_t kPumpBudgetBytes = 16u * 1024u;

// 8 x 480 mono 16-bit frames = 7.5 KiB internal DMA RAM, about 170 ms at
// 22.05 kHz, enough to ride out a Web Admin flash write.
constexpr uint32_t kDmaDescriptors = 8;
constexpr uint32_t kDmaFrames = 480;
constexpr uint32_t kI2sWriteTimeoutMs = 1000;
constexpr uint32_t kInitialSampleRate = 44100;
// Silence around the amplifier switching avoids a pop.
constexpr uint32_t kSilenceMs = 30;
constexpr uint32_t kAmpSettleMs = 10;
constexpr int kMaxConsecutiveDecodeErrors = 40;
// Bytes searched for the first MP3 frame header before giving up.
constexpr size_t kMaxSyncHuntBytes = 64u * 1024u;
constexpr size_t kWavChunkFrames = 1024;
// How often a playing clip samples the free internal RAM for its log line.
constexpr uint32_t kClipMemorySampleMs = 200;
// A paused clip checks for resume or stop this often.
constexpr uint32_t kPausePollMs = 20;
// A Bridge stream's FFmpeg may take this long to open a remote clip (the
// Bridge waits 15 s, audio_stream.py) before the format arrives.
constexpr uint32_t kBridgeFormatWaitMs = 20000;
// Like the camera stream: the window, not this hint, bounds what waits.
constexpr int kBridgeReceiveBufferBytes = 4 * 1024;

struct PlayRequest {
  char id[kMaxRequestIdLength + 1];
  char url[kMaxUrlLength + 1];
};

// Assist capture: 16 kHz 16-bit stereo (one mic per slot). 6 x 320 frames x
// 4 bytes = 7.5 KiB internal DMA RAM (the same budget as the closed speaker
// output), 120 ms of audio to ride out a flash write or a busy core.
constexpr uint32_t kRxDmaDescriptors = 6;
constexpr uint32_t kRxDmaFrames = 320;
// One 20 ms read; five reads fill a 100 ms frame.
constexpr size_t kCaptureChunkFrames = 320;
constexpr size_t kCaptureChunkBytes = kCaptureChunkFrames * 2 * sizeof(int16_t);
constexpr uint32_t kI2sReadTimeoutMs = 200;
// Samples dropped after the ADC was (re)configured: power-up settling.
constexpr uint32_t kCaptureWarmupMs = 40;
// Mono PSRAM ring between the microphone and the published frames: the 3 s
// wake word handoff window plus 1 s for that backlog to drain once the
// session runs (125 KiB).
constexpr size_t kRingSamples = assist::kSampleRate * 4;
// Backlog frames published per 20 ms read (5x real time).
constexpr size_t kBacklogFramesPerRead = 3;
// Wake word listening waits this long after an Assist session, whose answer
// usually follows, and retries this long after an error. After playback it
// resumes at once: the engine ignores the first second of audio anyway
// (wake_word::kWarmupFeatureRows), which covers the answer's end.
constexpr uint32_t kWakeAfterSessionMs = 1000;
constexpr uint32_t kWakeRetryMs = 5000;

struct AssistRequest {
  char id[assist::kMaxRequestIdLength + 1];
  uint32_t max_ms;
};

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
TaskHandle_t g_worker = nullptr;
SemaphoreHandle_t g_request_mutex = nullptr;
// Latest play request (newest wins), handed from the loop task to the worker.
PlayRequest* g_request = nullptr;
// Loop-task parse buffer for the play URL.
char* g_parse_url = nullptr;
std::atomic<bool> g_play_pending{false};
// Bumped by stop, a newer play and shutdown; a clip ends when it changed.
std::atomic<uint32_t> g_stop_generation{0};
std::atomic<bool> g_shutdown{false};
std::atomic<uint32_t> g_shutdown_ack{0};

portMUX_TYPE g_id_mux = portMUX_INITIALIZER_UNLOCKED;
char g_status_id[kMaxRequestIdLength + 1] = {};
char g_assist_status_id[assist::kMaxRequestIdLength + 1] = {};

// Latest Assist start (newest wins), guarded by g_request_mutex. Only one of
// g_play_pending and g_assist_pending is set: each command clears the other.
AssistRequest g_assist_request = {};
std::atomic<bool> g_assist_pending{false};
// Bumped by an Assist stop, a newer start, a play command and shutdown; a
// capture session ends when it changed.
std::atomic<uint32_t> g_capture_generation{0};

// Loop task only.
bool g_persist_pending = false;
uint32_t g_persist_due_ms = 0;
uint32_t g_command_log_ms = 0;
uint32_t g_assist_log_ms = 0;
// Id of the newest accepted start; a stop must name it.
char g_assist_session_id[assist::kMaxRequestIdLength + 1] = {};

// Worker only.
i2s_chan_handle_t g_rx = nullptr;
i2s_chan_handle_t g_tx = nullptr;
uint32_t g_tx_rate = 0;
bool g_amp_on = false;
uint8_t g_applied_volume_reg = 0xFF;
int g_applied_mute = -1;
int16_t* g_chunk = nullptr;
int16_t* g_mono = nullptr;
wake_word::PcmRing g_ring;
// Wake word listening waits until this time (millis).
uint32_t g_wake_resume_ms = 0;

bool logDue(uint32_t* last_ms, uint32_t interval_ms) {
  const uint32_t now_ms = millis();
  if (*last_ms != 0 && static_cast<uint32_t>(now_ms - *last_ms) < interval_ms) return false;
  *last_ms = now_ms == 0 ? 1 : now_ms;
  return true;
}

void setStatusId(const char* id) {
  portENTER_CRITICAL(&g_id_mux);
  if (id) {
    strncpy(g_status_id, id, kMaxRequestIdLength);
    g_status_id[kMaxRequestIdLength] = '\0';
  } else {
    g_status_id[0] = '\0';
  }
  portEXIT_CRITICAL(&g_id_mux);
}

void setPublic(PublicState state, ErrorCode error) {
  g_public_state.store(static_cast<uint8_t>(state));
  g_error.store(static_cast<uint8_t>(error));
  g_state_generation.fetch_add(1);
}

void setAssistStatusId(const char* id) {
  portENTER_CRITICAL(&g_id_mux);
  if (id) {
    strncpy(g_assist_status_id, id, assist::kMaxRequestIdLength);
    g_assist_status_id[assist::kMaxRequestIdLength] = '\0';
  } else {
    g_assist_status_id[0] = '\0';
  }
  portEXIT_CRITICAL(&g_id_mux);
}

void setAssistPublic(assist::PublicState state, assist::ErrorCode error) {
  g_assist_state.store(static_cast<uint8_t>(state));
  g_assist_error.store(static_cast<uint8_t>(error));
  g_assist_state_generation.fetch_add(1);
}

void notifyWorker(uint32_t bits) {
  if (g_worker) xTaskNotify(g_worker, bits, eSetBits);
}

bool stopRequested(uint32_t generation) {
  return g_shutdown.load() || g_stop_generation.load() != generation;
}

// ---------------------------------------------------------------------------
// Worker: amplifier, codec and I2S output
// ---------------------------------------------------------------------------
void setAmp(bool on) {
  audio_board::setAmp(on);
  g_amp_on = on;
}

void applyVolumeIfChanged() {
  if (!g_codec_ready.load()) return;
  const uint8_t reg = limitedVolumeRegister(g_volume.load(), speaker_board::kVolumeLimit);
  if (reg != g_applied_volume_reg && audio_board::setOutputVolumeRegister(reg) == ESP_OK) {
    g_applied_volume_reg = reg;
  }
  const int mute = g_muted.load() ? 1 : 0;
  if (mute != g_applied_mute && audio_board::setOutputMute(mute != 0) == ESP_OK) {
    g_applied_mute = mute;
  }
}

// The board's chips (audio_board.cpp): output codec first, then the
// microphone, which may be the output codec's own ADC.
void probeCodec() {
  audio_board::initAmp();
  g_amp_on = false;
  if (audio_board::probeOutput(kInitialSampleRate)) {
    g_codec_ready.store(true);
    applyVolumeIfChanged();
    g_state_generation.fetch_add(1);
  }
  if (audio_board::probeInput(assist::kSampleRate, g_mic_gain.load())) {
    g_mic_ready.store(true);
    g_assist_state_generation.fetch_add(1);
  }
}

void writeSilence(uint32_t ms) {
  if (!g_tx || g_tx_rate == 0) return;
  int16_t zeros[256] = {};
  size_t frames = static_cast<size_t>(g_tx_rate) * ms / 1000;
  while (frames > 0) {
    const size_t now = frames < 256 ? frames : 256;
    size_t written = 0;
    if (i2s_channel_write(g_tx, zeros, now * sizeof(int16_t), &written,
                          kI2sWriteTimeoutMs) != ESP_OK) {
      return;
    }
    frames -= now;
  }
}

void closeOutput() {
  if (!g_tx) return;
  writeSilence(kSilenceMs);
  setAmp(false);
  i2s_channel_disable(g_tx);
  i2s_del_channel(g_tx);
  g_tx = nullptr;
  g_tx_rate = 0;
}

i2s_std_clk_config_t clockConfig(uint32_t rate) {
  i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  return clk;
}

// Opens the mono 16-bit output at rate, or switches a running one to it.
ErrorCode openOutput(uint32_t rate) {
  if (!audio_board::outputSupportsRate(rate)) {
    Serial.printf("%s Unsupported sample rate %lu Hz\n", kLogPrefix,
                  static_cast<unsigned long>(rate));
    return ErrorCode::UnsupportedFormat;
  }
  if (g_tx && g_tx_rate == rate) return ErrorCode::None;
  if (audio_board::setOutputRate(rate) != ESP_OK) return ErrorCode::CodecUnavailable;
  if (g_tx) {
    // Rate change inside one clip (rare): reprogram while muted by the amp.
    setAmp(false);
    i2s_channel_disable(g_tx);
    const i2s_std_clk_config_t clk = clockConfig(rate);
    if (i2s_channel_reconfig_std_clock(g_tx, &clk) != ESP_OK ||
        i2s_channel_enable(g_tx) != ESP_OK) {
      i2s_del_channel(g_tx);
      g_tx = nullptr;
      g_tx_rate = 0;
      return ErrorCode::CodecUnavailable;
    }
    g_tx_rate = rate;
  } else {
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan.dma_desc_num = kDmaDescriptors;
    chan.dma_frame_num = kDmaFrames;
    // An underrun (flash write, busy core) plays silence, not an old buffer.
    chan.auto_clear = true;
    if (i2s_new_channel(&chan, &g_tx, nullptr) != ESP_OK) {
      g_tx = nullptr;
      return ErrorCode::OutOfMemory;
    }
    i2s_std_config_t config = {};
    config.clk_cfg = clockConfig(rate);
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_MONO);
    // The same sample in both slots, whichever one the codec reads.
    config.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    config.gpio_cfg.mclk = speaker_board::kI2sMclk;
    config.gpio_cfg.bclk = speaker_board::kI2sBclk;
    config.gpio_cfg.ws = speaker_board::kI2sLrck;
    config.gpio_cfg.dout = speaker_board::kI2sDout;
    config.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(g_tx, &config) != ESP_OK ||
        i2s_channel_enable(g_tx) != ESP_OK) {
      i2s_del_channel(g_tx);
      g_tx = nullptr;
      return ErrorCode::CodecUnavailable;
    }
    g_tx_rate = rate;
    const esp_err_t started = audio_board::startOutput();
    if (started != ESP_OK) {
      Serial.printf("%s %s start failed (%s)\n", kLogPrefix, audio_board::outputName(),
                    esp_err_to_name(started));
    }
  }
  applyVolumeIfChanged();
  writeSilence(kSilenceMs);
  setAmp(true);
  vTaskDelay(pdMS_TO_TICKS(kAmpSettleMs));
  return ErrorCode::None;
}

bool writeFrames(const int16_t* samples, size_t frames) {
  size_t written = 0;
  return i2s_channel_write(g_tx, samples, frames * sizeof(int16_t), &written,
                           kI2sWriteTimeoutMs) == ESP_OK;
}

// Worker only: audio of the current clip handed to the output.
uint64_t g_played_us = 0;
// Worker only: the current clip was paused at least once.
bool g_clip_paused = false;

// The status position follows the frames written (the DMA buffer plays
// them within about 170 ms).
void notePlayed(size_t frames) {
  if (g_tx_rate == 0) return;
  g_played_us += static_cast<uint64_t>(frames) * 1000000u / g_tx_rate;
  g_position_ms.store(static_cast<uint32_t>(g_played_us / 1000u));
}

// One log line per clip: how long it ran and how low the free internal RAM
// went meanwhile.
struct ClipStats {
  uint32_t started_ms = 0;
  uint32_t sampled_ms = 0;
  size_t internal_before = 0;
  size_t internal_lowest = 0;
};
ClipStats g_clip_stats;  // worker only

void sampleClipMemory() {
  const uint32_t now_ms = millis();
  if (static_cast<uint32_t>(now_ms - g_clip_stats.sampled_ms) < kClipMemorySampleMs) return;
  g_clip_stats.sampled_ms = now_ms;
  const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (internal < g_clip_stats.internal_lowest) g_clip_stats.internal_lowest = internal;
}

// HTTPS downloads keep their TLS buffers in PSRAM, as the P4 update download
// does (github_update.cpp). In internal RAM a playing clip took the free
// internal heap from 155 to 44 KB and the DMA heap to 2 KB on a V2: the
// link waited, the UI slowed and the next clip's TLS setup failed (esp-aes
// could not allocate). mbedTLS has one allocator for all connections; the
// internal one (the core's setting) returns when the download ends.
void* tlsPsramCalloc(size_t count, size_t size) {
  void* ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return ptr ? ptr : heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void* tlsInternalCalloc(size_t count, size_t size) {
  return heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void tlsFree(void* ptr) {
  heap_caps_free(ptr);
}

class ScopedPsramTls {
 public:
  explicit ScopedPsramTls(bool active)
      : active_(active && mbedtls_platform_set_calloc_free(tlsPsramCalloc, tlsFree) == 0) {}
  ~ScopedPsramTls() {
    if (active_) mbedtls_platform_set_calloc_free(tlsInternalCalloc, tlsFree);
  }

 private:
  bool active_;
};

// ---------------------------------------------------------------------------
// Worker: streaming download
// ---------------------------------------------------------------------------
// The HTTP objects live in the worker's playRequest() frame; Source points at
// them. pump() never blocks on the network, so the decoder can keep the I2S
// DMA fed between reads.
struct Source {
  HTTPClient* http = nullptr;
  NetworkClient* stream = nullptr;
  // A clip the Bridge decodes (audio_stream_contract.h) instead of a
  // download: its socket and message reader.
  int fd = -1;
  audio_stream::Receiver* receiver = nullptr;
  StreamBuffer buffer;
  // Bytes still expected from a response with Content-Length, -1 otherwise.
  int64_t remaining = -1;
  // The announced Content-Length, -1 for a live stream.
  int64_t total_bytes = -1;
  bool ended = false;
  bool failed = false;
  uint32_t last_data_ms = 0;
};

// The Bridge stream's pump: reads what arrived without waiting, audio
// straight into the PSRAM buffer, and acknowledges each message once it lies
// there. While the buffer is full nothing is read, so the Bridge holds back
// with at most two blocks in flight.
void pumpBridge(Source* src) {
  audio_stream::Receiver& rx = *src->receiver;
  size_t budget = kPumpBudgetBytes;
  for (;;) {
    uint32_t sequence = 0;
    uint32_t length = 0;
    if (rx.takeAck(&sequence, &length)) {
      uint8_t ack[tcp_ack::kAckBytes];
      tcp_ack::encodeAck(ack, sequence, length);
      if (!tcp_ack::sendAll(src->fd, ack, sizeof(ack), nullptr, nullptr)) {
        Serial.printf("%s Bridge stream acknowledgement failed: errno=%d\n", kLogPrefix, errno);
        src->ended = true;
        src->failed = true;
        return;
      }
    }
    const audio_stream::Receiver::Need need = rx.need();
    if (need == audio_stream::Receiver::Need::Ended) {
      src->ended = true;
      return;
    }
    if (need == audio_stream::Receiver::Need::Failed) {
      Serial.printf("%s Bridge stream invalid: %s\n", kLogPrefix, rx.failure());
      src->ended = true;
      src->failed = true;
      return;
    }
    uint8_t* destination = nullptr;
    size_t want = 0;
    if (need == audio_stream::Receiver::Need::Audio) {
      want = src->buffer.writable();
      if (want == 0) {
        // Full: the output sets the pace, the Bridge is not idle.
        src->last_data_ms = millis();
        return;
      }
      if (want > rx.audioMissing()) want = rx.audioMissing();
      if (want > kReadChunkBytes) want = kReadChunkBytes;
      if (want > budget) want = budget;
      destination = src->buffer.write();
    } else {
      destination = rx.scratch();
      want = rx.scratchMissing();
    }
    if (want == 0) return;
    const int received = recv(src->fd, destination, want, MSG_DONTWAIT);
    if (received == 0) {
      Serial.printf("%s Bridge stream closed before its end\n", kLogPrefix);
      src->ended = true;
      src->failed = true;
      return;
    }
    if (received < 0) {
      if (errno == EINTR) continue;
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        Serial.printf("%s Bridge stream receive error: errno=%d\n", kLogPrefix, errno);
        src->ended = true;
        src->failed = true;
        return;
      }
      break;
    }
    src->last_data_ms = millis();
    if (need == audio_stream::Receiver::Need::Audio) {
      src->buffer.commit(static_cast<size_t>(received));
      rx.audioReceived(static_cast<size_t>(received));
      budget -= static_cast<size_t>(received);
      if (budget == 0) return;
    } else {
      rx.scratchReceived(static_cast<size_t>(received));
    }
  }
  // The format arrives within kBridgeFormatWaitMs (playBridgeStream); after
  // it a Bridge that sends nothing is gone.
  if (rx.formatReady() &&
      static_cast<uint32_t>(millis() - src->last_data_ms) > kStreamIdleMs) {
    Serial.printf("%s No data for %lu ms, stream ended\n", kLogPrefix,
                  static_cast<unsigned long>(kStreamIdleMs));
    src->ended = true;
    src->failed = true;
  }
}

void pump(Source* src) {
  sampleClipMemory();
  if (src->ended) return;
  if (src->receiver) {
    pumpBridge(src);
    return;
  }
  size_t budget = kPumpBudgetBytes;
  while (budget > 0) {
    const int available = src->stream->available();
    if (available <= 0) break;
    size_t want = src->buffer.writable();
    if (want == 0) {
      // Full: the decoder sets the pace, the network is not idle.
      src->last_data_ms = millis();
      return;
    }
    if (want > kReadChunkBytes) want = kReadChunkBytes;
    if (want > budget) want = budget;
    if (want > static_cast<size_t>(available)) want = static_cast<size_t>(available);
    if (src->remaining >= 0 && want > static_cast<uint64_t>(src->remaining)) {
      want = static_cast<size_t>(src->remaining);
    }
    const int read = src->stream->read(src->buffer.write(), want);
    if (read <= 0) break;
    src->buffer.commit(static_cast<size_t>(read));
    budget -= static_cast<size_t>(read);
    src->last_data_ms = millis();
    if (src->remaining >= 0) {
      src->remaining -= read;
      if (src->remaining == 0) {
        src->ended = true;
        return;
      }
    }
  }
  if (!src->http->connected() && src->stream->available() <= 0) {
    src->ended = true;
    // Fewer bytes than announced: the server went away mid-clip.
    if (src->remaining > 0) src->failed = true;
    return;
  }
  if (static_cast<uint32_t>(millis() - src->last_data_ms) > kStreamIdleMs) {
    Serial.printf("%s No data for %lu ms, stream ended\n", kLogPrefix,
                  static_cast<unsigned long>(kStreamIdleMs));
    src->ended = true;
    src->failed = true;
  }
}

// Waits until at least min_bytes are buffered, the download ended or a stop
// arrived. Returns false on stop.
bool fillTo(Source* src, size_t min_bytes, uint32_t generation) {
  for (;;) {
    if (stopRequested(generation)) return false;
    pump(src);
    if (src->ended || src->buffer.buffered() >= min_bytes) return true;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// Buffers a little before playback starts or resumes after an underrun.
// The I2S channel plays silence meanwhile (auto_clear).
bool prebuffer(Source* src, uint32_t generation) {
  const uint32_t started_ms = millis();
  for (;;) {
    if (stopRequested(generation)) return false;
    pump(src);
    if (prebufferDone(src->buffer.buffered(), millis() - started_ms, src->ended)) return true;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// Drops bytes as they arrive, for an ID3 tag with embedded cover art.
bool skipBytes(Source* src, size_t bytes, uint32_t generation) {
  while (bytes > 0) {
    if (!fillTo(src, 1, generation)) return false;
    const size_t now = src->buffer.buffered() < bytes ? src->buffer.buffered() : bytes;
    if (now == 0) return true;  // Ended inside the tag.
    src->buffer.consume(now);
    bytes -= now;
  }
  return true;
}

// Home Assistant's pause: the output goes quiet while the download keeps
// filling the buffer; once it is full TCP holds the server back. Returns
// false when a stop or a newer clip ended this one meanwhile.
bool holdWhilePaused(Source* src, uint32_t generation) {
  if (!g_pause_requested.load()) return true;
  if (g_tx) setAmp(false);
  g_clip_paused = true;
  setPublic(PublicState::Paused, ErrorCode::None);
  while (g_pause_requested.load() && !stopRequested(generation)) {
    pump(src);
    vTaskDelay(pdMS_TO_TICKS(kPausePollMs));
  }
  if (stopRequested(generation)) return false;
  setPublic(PublicState::Playing, ErrorCode::None);
  if (g_tx) {
    setAmp(true);
    vTaskDelay(pdMS_TO_TICKS(kAmpSettleMs));
  }
  return true;
}

// ---------------------------------------------------------------------------
// Worker: decoding
// ---------------------------------------------------------------------------
// #119: a 6 KB "audio/mpeg" clip from Home Assistant decoded no frame on a
// Guition V1, and Helix overwrote its own memory on the way, so the panel
// restarted in MP3FreeDecoder. The file itself may not be shared, so its
// format is recorded here: on this task's stack, where the core dump of such
// a crash keeps it, and in one log line when a clip decodes nothing. No audio
// data is kept, only the frame headers.
struct Mp3Diagnostics {
  char marker[8] = {'M', 'P', '3', 'D', 'I', 'A', 'G', '\0'};
  uint32_t id3_bytes = 0;
  uint32_t candidates = 0;     // sync words handed to the decoder
  uint32_t decoded = 0;        // frames that decoded
  uint32_t errors[16] = {};    // per Helix error code, index = -code
  uint8_t headers[8][4] = {};  // the first candidates' header bytes
};

// noinline: the record stays in memory for the core dump when freeing the
// decoder crashes right after this line.
__attribute__((noinline)) void logUndecodedMp3(const Mp3Diagnostics& diag) {
  char headers[8 * 9 + 1] = "";
  char errors[16 * 8 + 1] = "";
  size_t used = 0;
  const uint32_t shown = diag.candidates < 8 ? diag.candidates : 8;
  for (uint32_t i = 0; i < shown && used < sizeof(headers); ++i) {
    const uint8_t* h = diag.headers[i];
    used += snprintf(headers + used, sizeof(headers) - used, "%s%02X%02X%02X%02X",
                     i ? " " : "", h[0], h[1], h[2], h[3]);
  }
  used = 0;
  for (int code = 1; code < 16 && used < sizeof(errors); ++code) {
    if (diag.errors[code] == 0) continue;
    used += snprintf(errors + used, sizeof(errors) - used, "%s-%d:%u", used ? " " : "", code,
                     static_cast<unsigned>(diag.errors[code]));
  }
  Serial.printf("%s MP3 decoded no frame: id3=%u bytes, %u sync words, errors %s, headers %s\n",
                kLogPrefix, static_cast<unsigned>(diag.id3_bytes),
                static_cast<unsigned>(diag.candidates), errors[0] ? errors : "none",
                headers[0] ? headers : "none");
}

// The clip's length for the status, from its first decoded frame.
void noteMp3Duration(HMP3Decoder decoder, const uint8_t* frame, int left, const Source* src,
                     uint32_t id3_bytes) {
  MP3FrameInfo info;
  MP3GetLastFrameInfo(decoder, &info);
  const int64_t audio_bytes = src->total_bytes > 0 ? src->total_bytes - id3_bytes : -1;
  const uint32_t duration = mp3DurationMs(frame, static_cast<size_t>(left),
                                          static_cast<uint32_t>(info.samprate),
                                          static_cast<uint32_t>(info.bitrate), audio_bytes);
  if (duration == 0) return;
  g_duration_ms.store(duration);
  g_state_generation.fetch_add(1);
}

ErrorCode playMp3(Source* src, uint32_t generation) {
  if (!fillTo(src, 10, generation)) return ErrorCode::None;
  const size_t tag = id3v2Length(src->buffer.read(), src->buffer.buffered());
  if (tag > 0 && !skipBytes(src, tag, generation)) return ErrorCode::None;
  Mp3Diagnostics diag;
  diag.id3_bytes = static_cast<uint32_t>(tag);

  HMP3Decoder decoder = MP3InitDecoder();
  if (!decoder) return ErrorCode::OutOfMemory;
  int16_t* pcm = static_cast<int16_t*>(
      heap_caps_malloc(MAX_NCHAN * MAX_NGRAN * MAX_NSAMP * sizeof(int16_t), MALLOC_CAP_INTERNAL));
  if (!pcm) {
    MP3FreeDecoder(decoder);
    return ErrorCode::OutOfMemory;
  }
  int consecutive_errors = 0;
  size_t unsynced_bytes = 0;
  bool played_any = false;
  bool synced = false;
  uint32_t underruns = 0;
  ErrorCode result = ErrorCode::None;
  const bool buffered_ok = prebuffer(src, generation);
  while (buffered_ok && !stopRequested(generation)) {
    if (!holdWhilePaused(src, generation)) break;
    pump(src);
    if (src->buffer.buffered() < kMinDecodeBytes && !src->ended) {
      // The network fell behind: refill before decoding a cut frame.
      if (played_any && ++underruns <= 3) {
        Serial.printf("%s Stream underrun, buffering\n", kLogPrefix);
      }
      if (!prebuffer(src, generation)) break;
      continue;
    }
    const size_t buffered = src->buffer.buffered();
    if (buffered == 0) break;
    unsigned char* read = const_cast<unsigned char*>(src->buffer.read());
    const int offset = MP3FindSyncWord(read, static_cast<int>(buffered));
    if (offset < 0) {
      // Keep the last byte, it may start a sync word.
      src->buffer.consume(buffered - 1);
      unsynced_bytes += buffered - 1;
      if (src->ended) break;
      // A long run without a frame header is not MP3 at all.
      if (!synced && unsynced_bytes > kMaxSyncHuntBytes) break;
      if (buffered == 1 && !fillTo(src, 2, generation)) break;
      continue;
    }
    src->buffer.consume(static_cast<size_t>(offset));
    unsynced_bytes += static_cast<size_t>(offset);
    if (!synced && unsynced_bytes > kMaxSyncHuntBytes) break;
    read = const_cast<unsigned char*>(src->buffer.read());
    int left = static_cast<int>(src->buffer.buffered());
    // Helix advances the pointer past the header even when the frame body
    // is missing, so a frame is committed only once it decoded or failed.
    unsigned char* frame = read;
    int frame_left = left;
    if (diag.candidates < 8) {
      memcpy(diag.headers[diag.candidates], read, left < 4 ? static_cast<size_t>(left) : 4);
    }
    ++diag.candidates;
    // Only headers Helix can decode reach it (#119, isDecodableMp3Header);
    // anything else is a false sync word: one byte further.
    if (left >= 4 && !isDecodableMp3Header(read)) {
      src->buffer.consume(1);
      ++unsynced_bytes;
      if (!synced && unsynced_bytes > kMaxSyncHuntBytes) break;
      continue;
    }
    const int err = MP3Decode(decoder, &frame, &frame_left, pcm, 0);
    if (err == ERR_MP3_NONE) {
      if (++diag.decoded == 1) noteMp3Duration(decoder, read, left, src, diag.id3_bytes);
    } else if (err < 0 && err > -16) {
      ++diag.errors[-err];
    }
    // No real frame is longer than kMinDecodeBytes; a longer "underflow" is
    // a false header (or free format, which is not supported).
    if (err == ERR_MP3_INDATA_UNDERFLOW && left < static_cast<int>(2 * kMinDecodeBytes)) {
      if (src->ended) break;
      if (!fillTo(src, static_cast<size_t>(left) + kMinDecodeBytes, generation)) break;
      continue;
    }
    if (err == ERR_MP3_INDATA_UNDERFLOW) frame_left = left;
    src->buffer.consume(static_cast<size_t>(left - frame_left));
    if (err == ERR_MP3_MAINDATA_UNDERFLOW) {
      synced = true;  // Bit reservoir filling after a sync.
      continue;
    }
    if (err != ERR_MP3_NONE) {
      // Skip a false sync word or a damaged frame.
      if (++consecutive_errors > kMaxConsecutiveDecodeErrors) {
        result = played_any ? ErrorCode::None : ErrorCode::DecodeFailed;
        break;
      }
      if (left == frame_left) src->buffer.consume(1);
      continue;
    }
    consecutive_errors = 0;
    synced = true;
    MP3FrameInfo info;
    MP3GetLastFrameInfo(decoder, &info);
    if (info.nChans < 1 || info.nChans > 2 || info.outputSamps <= 0) continue;
    const ErrorCode output = openOutput(static_cast<uint32_t>(info.samprate));
    if (output != ErrorCode::None) {
      result = output;
      break;
    }
    applyVolumeIfChanged();
    size_t frames = static_cast<size_t>(info.outputSamps / info.nChans);
    if (info.nChans == 2) downmixToMono(pcm, frames);
    if (!writeFrames(pcm, frames)) {
      result = ErrorCode::CodecUnavailable;
      break;
    }
    notePlayed(frames);
    played_any = true;
  }
  if (result == ErrorCode::None && !played_any && !stopRequested(generation)) {
    result = synced ? ErrorCode::DecodeFailed : ErrorCode::UnsupportedFormat;
  }
  if (diag.decoded == 0) logUndecodedMp3(diag);
  heap_caps_free(pcm);
  MP3FreeDecoder(decoder);
  return result;
}

// Plays 16-bit PCM from the buffer: frames_left frames, or until the source
// ends (UINT64_MAX). A WAV clip and a Bridge stream share it.
ErrorCode playPcm(Source* src, uint32_t generation, uint32_t rate, uint16_t channels,
                  uint64_t frames_left) {
  const size_t frame_bytes = 2u * channels;
  int16_t* chunk = static_cast<int16_t*>(heap_caps_malloc(
      kWavChunkFrames * channels * sizeof(int16_t), MALLOC_CAP_INTERNAL));
  if (!chunk) return ErrorCode::OutOfMemory;
  ErrorCode result = ErrorCode::None;
  bool started = false;
  if (!prebuffer(src, generation)) frames_left = 0;
  while (frames_left > 0 && !stopRequested(generation)) {
    if (!holdWhilePaused(src, generation)) break;
    pump(src);
    size_t available = src->buffer.buffered() / frame_bytes;
    if (available == 0) {
      if (src->ended) break;
      if (!prebuffer(src, generation)) break;
      continue;
    }
    size_t frames = available < kWavChunkFrames ? available : kWavChunkFrames;
    if (frames > frames_left) frames = static_cast<size_t>(frames_left);
    if (!started) {
      const ErrorCode output = openOutput(rate);
      if (output != ErrorCode::None) {
        result = output;
        break;
      }
      started = true;
    }
    // The buffer may sit at an odd offset; copy before reading int16.
    memcpy(chunk, src->buffer.read(), frames * frame_bytes);
    src->buffer.consume(frames * frame_bytes);
    if (channels == 2) downmixToMono(chunk, frames);
    applyVolumeIfChanged();
    if (!writeFrames(chunk, frames)) {
      result = ErrorCode::CodecUnavailable;
      break;
    }
    notePlayed(frames);
    if (frames_left != UINT64_MAX) frames_left -= frames;
  }
  heap_caps_free(chunk);
  return result;
}

ErrorCode playWav(Source* src, uint32_t generation) {
  WavInfo info;
  for (;;) {
    const WavHeaderResult header =
        parseWavHeader(src->buffer.read(), src->buffer.buffered(), &info);
    if (header == WavHeaderResult::Ok) break;
    if (header == WavHeaderResult::Invalid || src->ended) return ErrorCode::UnsupportedFormat;
    if (!fillTo(src, src->buffer.buffered() + 1, generation)) return ErrorCode::None;
  }
  src->buffer.consume(info.data_offset);
  const bool bounded = info.data_length != 0 && info.data_length != 0xFFFFFFFFu;
  const size_t frame_bytes = 2u * info.channels;
  const uint32_t duration = wavDurationMs(info.data_length, info.sample_rate,
                                          static_cast<uint32_t>(frame_bytes));
  if (duration > 0) {
    g_duration_ms.store(duration);
    g_state_generation.fetch_add(1);
  }
  return playPcm(src, generation, info.sample_rate, info.channels,
                 bounded ? info.data_length / frame_bytes : UINT64_MAX);
}

bool clipStopRequested(void* context) {
  return stopRequested(*static_cast<const uint32_t*>(context));
}

// A clip the Bridge decodes (audio_stream_contract.h): no TLS, no decoder,
// and at most the Bridge's window of two blocks waits in internal RAM. The
// clip reports "playing" with its id once the format named its position.
ErrorCode playBridgeStream(const char* id, const char* url, uint32_t generation,
                           uint8_t* memory) {
  audio_stream::Endpoint endpoint;
  if (!audio_stream::parseStreamUrl(url, strlen(url), &endpoint)) return ErrorCode::InvalidUrl;
  tcp_ack::ConnectOptions options;
  options.log_prefix = kLogPrefix;
  options.receive_buffer_bytes = kBridgeReceiveBufferBytes;
  options.connect_timeout_ms = kHttpTimeoutMs;
  options.stop = clipStopRequested;
  options.stop_context = &generation;
  const int fd = tcp_ack::connectTo(endpoint.host, endpoint.port, options);
  if (fd < 0) {
    if (stopRequested(generation)) return ErrorCode::None;
    Serial.printf("%s Bridge stream connection failed: errno=%d\n", kLogPrefix, errno);
    return ErrorCode::DownloadFailed;
  }
  char request[sizeof("HTAUD/1 ") + audio_stream::kMaxTokenLength + 2];
  const int request_bytes =
      snprintf(request, sizeof(request), "%s%s\n", audio_stream::kRequestPrefix, endpoint.token);
  uint8_t hello[tcp_ack::kHelloBytes] = {};
  uint16_t hello_status = 0;
  uint16_t chunk = 0;
  if (request_bytes <= 0 || static_cast<size_t>(request_bytes) >= sizeof(request) ||
      !tcp_ack::sendAll(fd, request, static_cast<size_t>(request_bytes), clipStopRequested,
                        &generation) ||
      tcp_ack::receiveExact(fd, hello, sizeof(hello), clipStopRequested, &generation,
                            millis() + kHttpTimeoutMs) != tcp_ack::SocketReadResult::Ok ||
      !tcp_ack::decodeHello(hello, &hello_status, &chunk) ||
      hello_status != tcp_ack::kHelloAccepted) {
    tcp_ack::closeSocket(fd);
    if (stopRequested(generation)) return ErrorCode::None;
    Serial.printf("%s Bridge stream refused (status %u)\n", kLogPrefix,
                  static_cast<unsigned>(hello_status));
    return ErrorCode::DownloadFailed;
  }
  audio_stream::Receiver receiver;
  Source src;
  src.fd = fd;
  src.receiver = &receiver;
  src.buffer.reset(memory, kStreamBufferBytes);
  src.last_data_ms = millis();
  const uint32_t wait_started_ms = millis();
  while (!receiver.formatReady() && !src.ended && !stopRequested(generation)) {
    if (static_cast<uint32_t>(millis() - wait_started_ms) > kBridgeFormatWaitMs) {
      Serial.printf("%s Bridge stream sent no format within %lu ms\n", kLogPrefix,
                    static_cast<unsigned long>(kBridgeFormatWaitMs));
      src.failed = true;
      break;
    }
    pump(&src);
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  ErrorCode result = ErrorCode::None;
  if (receiver.formatReady() && !src.failed && !stopRequested(generation)) {
    const audio_stream::Format& format = receiver.format();
    // The position counts from where the Bridge started the clip (a seek).
    g_played_us = static_cast<uint64_t>(format.position_ms) * 1000u;
    g_position_ms.store(format.position_ms);
    g_duration_ms.store(format.duration_ms);
    setStatusId(id);
    setPublic(PublicState::Playing, ErrorCode::None);
    Serial.printf("%s Bridge stream: %lu Hz from %.1f s, length %.1f s\n", kLogPrefix,
                  static_cast<unsigned long>(format.rate), format.position_ms / 1000.0,
                  format.duration_ms / 1000.0);
    result = playPcm(&src, generation, format.rate, 1, UINT64_MAX);
  } else if (strcmp(receiver.failure(), "unsupported_format") == 0) {
    result = ErrorCode::UnsupportedFormat;
  }
  if (result == ErrorCode::None && src.failed && !stopRequested(generation)) {
    result = ErrorCode::DownloadFailed;
  }
  tcp_ack::closeSocket(fd);
  return result;
}

ErrorCode playUrl(const char* id, const char* url, uint32_t generation, uint8_t* memory) {
  if (strncmp(url, "tcp://", 6) == 0) return playBridgeStream(id, url, generation, memory);
  const bool https = strncmp(url, "https://", 8) == 0;
  // First, so the TLS buffers of the whole download land in PSRAM.
  ScopedPsramTls tls_memory(https);
  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  http.setConnectTimeout(kHttpTimeoutMs);
  http.setReuse(false);
  // HTTP/1.0: no chunked transfer encoding to undo, and Icecast sends no
  // inline metadata unless asked for it.
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  NetworkClient plain_client;
  NetworkClientSecure secure_client;
  bool begun = false;
  if (https) {
    secure_client.setInsecure();
    begun = http.begin(secure_client, url);
  } else {
    begun = http.begin(plain_client, url);
  }
  if (!begun) return ErrorCode::InvalidUrl;
  http.addHeader("Connection", "close");
  const char* headers[] = {"Content-Type"};
  http.collectHeaders(headers, 1);

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("%s Download failed: HTTP %d\n", kLogPrefix, code);
    http.end();
    return ErrorCode::DownloadFailed;
  }
  const String type = http.header("Content-Type");
  if (isUnsupportedContentType(type.c_str())) {
    Serial.printf("%s Unsupported content type: %.48s\n", kLogPrefix, type.c_str());
    http.end();
    return ErrorCode::UnsupportedFormat;
  }
  Source src;
  src.http = &http;
  src.stream = http.getStreamPtr();
  src.buffer.reset(memory, kStreamBufferBytes);
  const int announced = http.getSize();
  src.remaining = announced > 0 ? announced : -1;
  src.total_bytes = src.remaining;
  src.last_data_ms = millis();
  ErrorCode result = ErrorCode::None;
  if (!src.stream) {
    result = ErrorCode::DownloadFailed;
  } else if (fillTo(&src, 12, generation)) {
    if (src.buffer.buffered() == 0) {
      result = ErrorCode::DownloadFailed;
    } else if (detectFormat(src.buffer.read(), src.buffer.buffered()) == ClipFormat::Wav) {
      result = playWav(&src, generation);
    } else {
      // A radio stream may start in the middle of a frame, so anything that
      // is not WAV goes to the MP3 decoder, which hunts for a frame header.
      result = playMp3(&src, generation);
    }
    // A connection that broke mid-clip is an error even if part of it played.
    if (result == ErrorCode::None && src.failed && !stopRequested(generation)) {
      result = ErrorCode::DownloadFailed;
    }
  }
  http.end();
  return result;
}

void playRequest(const PlayRequest& request, uint8_t* stream_memory) {
  const uint32_t generation = g_stop_generation.load();
  // A Bridge stream reports its clip once the format names the position
  // (playBridgeStream). A seek starts a new stream; b502 reported "playing"
  // from 0 without a length in between, and Home Assistant's player bar
  // vanished or jumped back before it showed the new position.
  const bool bridge_stream = strncmp(request.url, "tcp://", 6) == 0;
  if (!bridge_stream) setStatusId(request.id);
  if (!g_codec_ready.load()) {
    setStatusId(request.id);
    setPublic(PublicState::Idle, ErrorCode::CodecUnavailable);
    return;
  }
  if (!stream_memory) {
    setStatusId(request.id);
    setPublic(PublicState::Idle, ErrorCode::OutOfMemory);
    return;
  }
  if (!bridge_stream) {
    g_position_ms.store(0);
    g_duration_ms.store(0);
  }
  g_played_us = 0;
  g_clip_paused = false;
  g_clip_stats = ClipStats{};
  g_clip_stats.started_ms = millis();
  g_clip_stats.internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  g_clip_stats.internal_lowest = g_clip_stats.internal_before;
  if (!bridge_stream) setPublic(PublicState::Playing, ErrorCode::None);
  Serial.printf("%s Playing %s\n", kLogPrefix, request.id);
  ErrorCode result = playUrl(request.id, request.url, generation, stream_memory);
  // A long pause can outlast the server's patience: the connection is gone
  // when playback resumes, so the clip starts again once. A Bridge stream's
  // token works once; the Bridge keeps a paused clip for 10 minutes.
  if (result == ErrorCode::DownloadFailed && g_clip_paused && !stopRequested(generation) &&
      !bridge_stream) {
    Serial.printf("%s Clip %s lost its connection during a pause, starting again\n",
                  kLogPrefix, request.id);
    g_position_ms.store(0);
    g_played_us = 0;
    g_clip_paused = false;
    setPublic(PublicState::Playing, ErrorCode::None);
    result = playUrl(request.id, request.url, generation, stream_memory);
  }
  closeOutput();
  g_pause_requested.store(false);
  Serial.printf("%s Clip %s %s after %.1f s: internal RAM %u KB before, lowest %u KB\n",
                kLogPrefix, request.id,
                result != ErrorCode::None ? errorCodeName(result)
                : stopRequested(generation) ? "stopped" : "done",
                (millis() - g_clip_stats.started_ms) / 1000.0,
                static_cast<unsigned>(g_clip_stats.internal_before / 1024),
                static_cast<unsigned>(g_clip_stats.internal_lowest / 1024));
  // A newer request keeps the state "playing" instead of flashing idle.
  if (!(g_play_pending.load() && result == ErrorCode::None)) {
    setStatusId(request.id);
    setPublic(PublicState::Idle, result);
  }
}

// ---------------------------------------------------------------------------
// Worker: Assist microphone capture
// ---------------------------------------------------------------------------
// The ring holds mono samples in native order and is copied byte-wise into
// the little-endian frames.
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "assist frames are little-endian");

void closeInput() {
  if (!g_rx) return;
  i2s_channel_disable(g_rx);
  i2s_del_channel(g_rx);
  g_rx = nullptr;
}

// Capture buffers, allocated once by the worker: one stereo I2S chunk, its
// mono conversion and the PSRAM ring that queues mono audio for publishing.
bool allocateCaptureBuffers() {
  g_chunk = static_cast<int16_t*>(heap_caps_malloc(kCaptureChunkBytes, MALLOC_CAP_INTERNAL));
  g_mono = static_cast<int16_t*>(
      heap_caps_malloc(kCaptureChunkFrames * sizeof(int16_t), MALLOC_CAP_INTERNAL));
  int16_t* ring = static_cast<int16_t*>(
      heap_caps_malloc(kRingSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_chunk || !g_mono || !ring) {
    heap_caps_free(g_chunk);
    heap_caps_free(g_mono);
    heap_caps_free(ring);
    g_chunk = nullptr;
    g_mono = nullptr;
    Serial.printf("%s Capture buffers unavailable, Assist microphone disabled\n", kLogPrefix);
    return false;
  }
  g_ring.reset(ring, kRingSamples);
  return true;
}

// RX-only master channel on the shared I2S lines: the P4 drives MCLK, BCLK
// and LRCK; an ES7210 sends MIC1/MIC2 as the left/right slot on din, an
// ES8311 its one ADC on the left slot.
assist::ErrorCode openInput() {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chan.dma_desc_num = kRxDmaDescriptors;
  chan.dma_frame_num = kRxDmaFrames;
  const esp_err_t created = i2s_new_channel(&chan, nullptr, &g_rx);
  if (created != ESP_OK) {
    g_rx = nullptr;
    Serial.printf("%s Microphone channel unavailable (%s)\n", kLogPrefix,
                  esp_err_to_name(created));
    return created == ESP_ERR_NO_MEM ? assist::ErrorCode::OutOfMemory : assist::ErrorCode::Busy;
  }
  i2s_std_config_t config = {};
  config.clk_cfg = clockConfig(assist::kSampleRate);
  config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_STEREO);
  config.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
  config.gpio_cfg.mclk = speaker_board::kI2sMclk;
  config.gpio_cfg.bclk = speaker_board::kI2sBclk;
  config.gpio_cfg.ws = speaker_board::kI2sLrck;
  config.gpio_cfg.dout = I2S_GPIO_UNUSED;
  config.gpio_cfg.din = speaker_board::kI2sDin;
  if (i2s_channel_init_std_mode(g_rx, &config) != ESP_OK) {
    i2s_del_channel(g_rx);
    g_rx = nullptr;
    return assist::ErrorCode::MicUnavailable;
  }
  if (i2s_channel_enable(g_rx) != ESP_OK) {
    i2s_del_channel(g_rx);
    g_rx = nullptr;
    return assist::ErrorCode::MicUnavailable;
  }
  // Configure the microphone ADC now that its clocks run.
  const esp_err_t configured = audio_board::startInput(assist::kSampleRate, g_mic_gain.load());
  if (configured != ESP_OK) {
    Serial.printf("%s %s setup failed (%s)\n", kLogPrefix, audio_board::inputName(),
                  esp_err_to_name(configured));
    closeInput();
    return assist::ErrorCode::MicUnavailable;
  }
  // Drop the samples of the ADC's power-up settling.
  size_t warmup_bytes = static_cast<size_t>(assist::kSampleRate) * kCaptureWarmupMs / 1000 *
                        2 * sizeof(int16_t);
  while (warmup_bytes > 0) {
    size_t bytes_read = 0;
    const size_t want = warmup_bytes < kCaptureChunkBytes ? warmup_bytes : kCaptureChunkBytes;
    if (i2s_channel_read(g_rx, g_chunk, want, &bytes_read, kI2sReadTimeoutMs) != ESP_OK ||
        bytes_read == 0) {
      break;
    }
    warmup_bytes -= bytes_read < warmup_bytes ? bytes_read : warmup_bytes;
  }
  return assist::ErrorCode::None;
}

// Reads one 20 ms stereo chunk into g_mono. Returns the mono sample count,
// or -1 when the microphone stopped answering.
int readMono() {
  size_t bytes_read = 0;
  const esp_err_t read =
      i2s_channel_read(g_rx, g_chunk, kCaptureChunkBytes, &bytes_read, kI2sReadTimeoutMs);
  if (read != ESP_OK && bytes_read == 0) {
    Serial.printf("%s Microphone read failed (%s)\n", kLogPrefix, esp_err_to_name(read));
    return -1;
  }
  const size_t frames = bytes_read / (2 * sizeof(int16_t));
  if (speaker_board::kInput == audio_chips::Input::Es8311) {
    assist::stereoLeftToMono(g_chunk, frames, g_mono);
  } else {
    assist::stereoToMono(g_chunk, frames, g_mono);
  }
  return static_cast<int>(frames);
}

// Writes the header in front of the PCM already at frame + header length and
// queues the frame. The MQTT layer copies it, so the buffer is free again.
bool publishAudioFrame(const char* topic, uint8_t* frame, const char* id, size_t id_length,
                       uint32_t sequence, size_t pcm_bytes, bool last) {
  const size_t header_length = assist::frameHeaderLength(id_length);
  if (assist::buildFrameHeader(frame, header_length, id, id_length, last, sequence) !=
      header_length) {
    return false;
  }
  return networkManager.mqttEnqueuePublish(topic, frame, header_length + pcm_bytes, false);
}

// Publishes up to max_frames full 100 ms frames from the ring. A frame the
// publish queue refuses stays in the ring for the next attempt; returns
// false then.
bool sendRingFrames(const char* topic, uint8_t* frame, const char* id, size_t id_length,
                    uint32_t* sequence, size_t max_frames) {
  uint8_t* pcm = frame + assist::frameHeaderLength(id_length);
  for (size_t sent = 0; sent < max_frames && g_ring.size() >= assist::kFrameSamples; ++sent) {
    g_ring.peekBytes(pcm, assist::kFrameSamples);
    if (!publishAudioFrame(topic, frame, id, id_length, *sequence, assist::kFrameBytes, false)) {
      return false;
    }
    g_ring.discard(assist::kFrameSamples);
    ++*sequence;
  }
  return true;
}

// handoff: a wake word detection left the input open and the audio since the
// detection in the ring; it is sent first, then live audio.
void captureSession(const AssistRequest& request, uint32_t generation, bool handoff) {
  setAssistStatusId(request.id);
  if (!g_mic_ready.load()) {
    setAssistPublic(assist::PublicState::Idle, assist::ErrorCode::MicUnavailable);
    return;
  }
  // The capture uses the speaker's I2S lines and clocks.
  closeOutput();
  if (!handoff) g_ring.clear();
  const char* audio_topic = mqttTopics.topic(TopicKey::ASSIST_AUDIO_STAT);
  // Copied once: the registry may rebuild its strings on the loop task.
  const String topic = audio_topic ? audio_topic : "";
  const size_t id_length = strlen(request.id);
  const size_t header_length = assist::frameHeaderLength(id_length);
  uint8_t* frame = static_cast<uint8_t*>(heap_caps_malloc(
      header_length + assist::kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  assist::ErrorCode result = assist::ErrorCode::None;
  if (!frame || !g_chunk || g_ring.capacity() == 0) result = assist::ErrorCode::OutOfMemory;
  if (result == assist::ErrorCode::None && topic.isEmpty()) result = assist::ErrorCode::Invalid;
  if (result == assist::ErrorCode::None && !handoff) result = openInput();
  if (result != assist::ErrorCode::None) {
    Serial.printf("%s Assist capture %s refused: %s\n", kLogPrefix, request.id,
                  assist::errorCodeName(result));
    closeInput();
    g_ring.clear();
    heap_caps_free(frame);
    // Still answer the Bridge's stream with an empty last frame.
    if (!topic.isEmpty() && networkManager.isMqttConnected()) {
      uint8_t header[assist::kMaxFrameHeaderBytes];
      publishAudioFrame(topic.c_str(), header, request.id, id_length, 0, 0, true);
    }
    setAssistPublic(assist::PublicState::Idle, result);
    return;
  }

  setAssistPublic(assist::PublicState::Listening, assist::ErrorCode::None);
  Serial.printf("%s Assist capture %s started (max %lu ms%s)\n", kLogPrefix, request.id,
                static_cast<unsigned long>(request.max_ms),
                handoff ? ", after the wake word" : "");
  uint32_t sequence = 0;
  const char* end_reason = "stopped";
  const uint32_t started_ms = millis();
  for (;;) {
    if (g_shutdown.load()) {
      end_reason = "shutdown";
      break;
    }
    if (g_capture_generation.load() != generation) break;
    if (static_cast<uint32_t>(millis() - started_ms) >= request.max_ms) {
      result = assist::ErrorCode::Timeout;
      break;
    }
    // Frames queued while offline would only pile up; the Bridge ends the
    // pipeline after 2 s without frames.
    if (!networkManager.isMqttConnected()) {
      end_reason = "MQTT offline";
      break;
    }
    const int frames = readMono();
    if (frames < 0) {
      result = assist::ErrorCode::MicUnavailable;
      break;
    }
    // A ring that overflows means MQTT has not kept up for seconds; stop
    // instead of dropping audio in the middle of a sentence.
    if (!g_ring.write(g_mono, static_cast<size_t>(frames))) {
      result = assist::ErrorCode::Busy;
      break;
    }
    // A few frames per read drain the wake word backlog without flooding
    // the publish queue; a refused frame is retried after the next read.
    sendRingFrames(topic.c_str(), frame, request.id, id_length, &sequence,
                   kBacklogFramesPerRead);
  }
  closeInput();
  // Whatever is still buffered goes out as far as the queue takes it. Every
  // session ends with a frame carrying the last flag, also after an error;
  // it may hold the remaining samples or none.
  sendRingFrames(topic.c_str(), frame, request.id, id_length, &sequence, SIZE_MAX);
  size_t rest = g_ring.size() < assist::kFrameSamples ? g_ring.size() : assist::kFrameSamples;
  g_ring.peekBytes(frame + header_length, rest);
  const bool last_sent = publishAudioFrame(topic.c_str(), frame, request.id, id_length, sequence,
                                           rest * sizeof(int16_t), true);
  g_ring.clear();
  heap_caps_free(frame);
  Serial.printf("%s Assist capture %s ended (%s), %lu frames%s\n", kLogPrefix, request.id,
                result != assist::ErrorCode::None ? assist::errorCodeName(result) : end_reason,
                static_cast<unsigned long>(sequence + 1),
                last_sent ? "" : ", last frame not queued");
  // A newer start keeps the state "listening" instead of flashing idle.
  if (!(g_assist_pending.load() && result == assist::ErrorCode::None)) {
    setAssistPublic(assist::PublicState::Idle, result);
  }
}

// Takes the newest Assist start. The generation is read under the mutex: a
// stop after this point changes it.
bool takeAssistRequest(AssistRequest* request, uint32_t* generation) {
  bool have = false;
  if (xSemaphoreTake(g_request_mutex, portMAX_DELAY) == pdTRUE) {
    have = g_assist_pending.exchange(false);
    if (have) {
      *request = g_assist_request;
      *generation = g_capture_generation.load();
    }
    xSemaphoreGive(g_request_mutex);
  }
  return have;
}

// Non-blocking: the pending notification bits, with volume changes already
// applied (they never interrupt listening).
uint32_t takeNotifications() {
  uint32_t bits = 0;
  xTaskNotifyWait(0, UINT32_MAX, &bits, 0);
  if (bits & kNotifyVolume) applyVolumeIfChanged();
  return bits & ~kNotifyVolume;
}

// ---------------------------------------------------------------------------
// Worker: on-panel wake word
// ---------------------------------------------------------------------------
uint32_t activeWakeWords() {
  return assist::activeWakeWordMask(wakeSetting()) & availableWakeWords();
}

bool wakeListenConfigured() {
  return activeWakeWords() != 0 && g_mic_ready.load() && g_mic_enabled.load() &&
         g_ring.capacity() != 0 &&
         !g_shutdown.load() && !g_play_pending.load() && !g_assist_pending.load();
}

// A camera stream pauses wake word listening. On core 0 the listener
// (priority 3) delayed the stream task's acknowledgements: the V2 full screen
// fell from 30 to 21-23 FPS with a wake word on and reached 30 again with it
// off (2026-10-21). Listening resumes kWakeCameraHoldMs after the last
// stream, so a switch between the popup and the full screen does not reopen
// the microphone in between. Worker task only.
constexpr uint32_t kWakeCameraHoldMs = 2000;
constexpr uint32_t kWakeCameraPollMs = 250;
uint32_t g_camera_seen_ms = 0;
bool g_wake_camera_paused = false;

bool cameraHoldsMicrophone() {
  const uint32_t now = millis();
  if (camera_stream_is_active()) {
    g_camera_seen_ms = now ? now : 1;
    return true;
  }
  return g_camera_seen_ms != 0 &&
         static_cast<uint32_t>(now - g_camera_seen_ms) < kWakeCameraHoldMs;
}

bool wakeListenWanted() {
  if (!wakeListenConfigured()) {
    g_wake_camera_paused = false;
    return false;
  }
  const bool paused = cameraHoldsMicrophone();
  if (paused != g_wake_camera_paused) {
    g_wake_camera_paused = paused;
    Serial.printf("%s Wake word %s\n", kLogPrefix,
                  paused ? "paused while a camera stream runs"
                         : "listening again after the camera stream");
  }
  return !paused;
}

#if defined(HOMETILES_WAKE_WORD)
wake_word::Engine g_wake_engine;
uint32_t g_wake_sequence = 0;
uint32_t g_wake_log_ms = 0;

// Microphone level while listening: first after 10 s of audio (a quick board
// check), then every 10 minutes of audio.
constexpr uint64_t kFirstLevelSamples = 10ull * assist::kSampleRate;
constexpr uint64_t kLevelSamples = 600ull * assist::kSampleRate;
mic_level::Meter g_mic_meter;
uint64_t g_mic_meter_due = kFirstLevelSamples;

void meterMicLevel(const int16_t* samples, size_t count) {
  g_mic_meter.add(samples, count);
  if (g_mic_meter.samples() < g_mic_meter_due) return;
  const uint16_t gain = micGainTenthsDb(g_mic_gain.load());
  Serial.printf("%s Mic level (%s, gain setting %u.%u dB): peak %.0f dBFS, average %.0f dBFS over "
                "%lu s\n",
                kLogPrefix, audio_board::inputName(), static_cast<unsigned>(gain / 10),
                static_cast<unsigned>(gain % 10), static_cast<double>(g_mic_meter.peakDbfs()),
                static_cast<double>(g_mic_meter.rmsDbfs()),
                static_cast<unsigned long>(g_mic_meter.samples() / assist::kSampleRate));
  g_mic_meter.reset();
  g_mic_meter_due = kLevelSamples;
}

void disableWakeWords(const char* reason) {
  Serial.printf("%s Wake word disabled: %s\n", kLogPrefix, reason);
  g_wake_failed.store(true);
  g_assist_state_generation.fetch_add(1);
}

// After a detection: tells the Bridge, then buffers audio for kWakeHandoffMs
// and hands it to the first Assist start. Returns true to keep listening with
// the input still open; false when the input was closed, with the
// notifications the main loop still has to handle in *bits.
bool handleWakeDetection(const char* word, const String& topic, uint32_t mask, uint32_t* bits) {
  *bits = 0;
  char payload[96];
  const size_t length = assist::buildWakeJson(payload, sizeof(payload), word, g_wake_sequence);
  if (topic.isEmpty() || length == 0 || !networkManager.isMqttConnected() ||
      !networkManager.mqttEnqueuePublish(topic.c_str(), reinterpret_cast<const uint8_t*>(payload),
                                         length, false)) {
    if (logDue(&g_wake_log_ms, 10000)) {
      Serial.printf("%s Wake word %s detected but not sent (not connected)\n", kLogPrefix, word);
    }
    return true;
  }
  Serial.printf("%s Wake word %s sent (seq %lu), waiting for Assist\n", kLogPrefix, word,
                static_cast<unsigned long>(g_wake_sequence));
  ++g_wake_sequence;
  setAssistStatusId(nullptr);
  setAssistPublic(assist::PublicState::Wake, assist::ErrorCode::None);
  g_ring.clear();
  const uint32_t detected_ms = millis();
  for (;;) {
    const uint32_t pending = takeNotifications();
    if (pending & kNotifyAssist) {
      AssistRequest request = {};
      uint32_t generation = 0;
      if (takeAssistRequest(&request, &generation)) {
        captureSession(request, generation, true);
        *bits = pending & ~(kNotifyAssist | kNotifyWake);
        return false;
      }
    }
    if ((pending & (kNotifyPlay | kNotifyShutdown)) || g_shutdown.load() ||
        activeWakeWords() != mask) {
      Serial.printf("%s Wake word handoff abandoned\n", kLogPrefix);
      closeInput();
      g_ring.clear();
      setAssistPublic(assist::PublicState::Idle, assist::ErrorCode::None);
      *bits = pending & ~kNotifyWake;
      return false;
    }
    if (static_cast<uint32_t>(millis() - detected_ms) >= assist::kWakeHandoffMs) {
      Serial.printf("%s No Assist start within %lu ms, listening again\n", kLogPrefix,
                    static_cast<unsigned long>(assist::kWakeHandoffMs));
      g_ring.clear();
      setAssistPublic(assist::PublicState::Idle, assist::ErrorCode::None);
      return true;
    }
    const int frames = readMono();
    if (frames < 0) {
      closeInput();
      g_ring.clear();
      setAssistPublic(assist::PublicState::Idle, assist::ErrorCode::None);
      g_wake_resume_ms = millis() + kWakeRetryMs;
      return false;
    }
    // The ring holds more than the handoff window, so this never drops.
    g_ring.write(g_mono, static_cast<size_t>(frames));
  }
}

// Listens until a notification, a wake word change, an error or a camera
// stream. Returns the notifications the main loop still has to handle (0:
// re-evaluate).
uint32_t wakeListen() {
  const uint32_t mask = activeWakeWords();
  size_t index = 0;
  while (index < assist::kWakeWordCount && !(mask & (1u << index))) ++index;
  const wake_word::ModelSpec* spec = wake_word::modelForCatalogIndex(index);
  if (!spec) {
    disableWakeWords("no model for the active word");
    return 0;
  }
  if (g_wake_engine.spec() != spec && !g_wake_engine.begin(*spec)) {
    disableWakeWords("model setup failed");
    return 0;
  }
  const char* wake_topic = mqttTopics.topic(TopicKey::ASSIST_WAKE_STAT);
  // Copied once: the registry may rebuild its strings on the loop task.
  const String topic = wake_topic ? wake_topic : "";
  // The input uses the speaker's I2S lines and clocks.
  closeOutput();
  if (openInput() != assist::ErrorCode::None) {
    g_wake_resume_ms = millis() + kWakeRetryMs;
    return 0;
  }
  g_wake_engine.reset();
  if (logDue(&g_wake_log_ms, 60000)) {
    Serial.printf("%s Listening for wake word %s\n", kLogPrefix, spec->id);
  }
  uint32_t bits = 0;
  for (;;) {
    bits = takeNotifications();
    if (bits || g_shutdown.load() || activeWakeWords() != mask || camera_stream_is_active()) {
      break;
    }
    const int frames = readMono();
    if (frames < 0) {
      g_wake_resume_ms = millis() + kWakeRetryMs;
      break;
    }
    meterMicLevel(g_mono, static_cast<size_t>(frames));
    if (frames == 0 || !g_wake_engine.process(g_mono, static_cast<size_t>(frames))) continue;
    uint32_t handoff_bits = 0;
    if (handleWakeDetection(spec->id, topic, mask, &handoff_bits)) continue;
    g_wake_resume_ms = millis() + kWakeAfterSessionMs;
    return handoff_bits;
  }
  closeInput();
  return bits & ~kNotifyWake;
}
#else
uint32_t wakeListen() { return 0; }
#endif  // defined(HOMETILES_WAKE_WORD)

void workerMain(void*) {
  probeCodec();
  if (g_mic_ready.load() && !allocateCaptureBuffers()) {
    g_mic_ready.store(false);
    g_assist_state_generation.fetch_add(1);
  }
  PlayRequest* request = static_cast<PlayRequest*>(
      heap_caps_malloc(sizeof(PlayRequest), MALLOC_CAP_SPIRAM));
  // Allocated once: a fragmented PSRAM heap later cannot refuse a stream.
  uint8_t* stream_memory =
      static_cast<uint8_t*>(heap_caps_malloc(kStreamBufferBytes, MALLOC_CAP_SPIRAM));
  if (!stream_memory) Serial.printf("%s Stream buffer unavailable\n", kLogPrefix);
  for (;;) {
    uint32_t bits = 0;
    const bool wake_wanted = wakeListenWanted();
    const int32_t refractory_ms = static_cast<int32_t>(g_wake_resume_ms - millis());
    if (wake_wanted && refractory_ms <= 0) {
      // Listening runs until something needs the worker or the I2S lines.
      bits = wakeListen();
      if (bits == 0) continue;
    } else {
      TickType_t wait = portMAX_DELAY;
      if (wake_wanted) {
        wait = pdMS_TO_TICKS(static_cast<uint32_t>(refractory_ms)) + 1;
      } else if (g_wake_camera_paused) {
        wait = pdMS_TO_TICKS(kWakeCameraPollMs);  // until the camera stream ended
      }
      if (xTaskNotifyWait(0, UINT32_MAX, &bits, wait) != pdTRUE) continue;
    }
    if ((bits & kNotifyShutdown) || g_shutdown.load()) {
      closeInput();
      closeOutput();
      g_shutdown_ack.fetch_add(1);
      continue;
    }
    if (bits & kNotifyVolume) applyVolumeIfChanged();
    if ((bits & kNotifyPlay) && request) {
      bool have = false;
      if (xSemaphoreTake(g_request_mutex, portMAX_DELAY) == pdTRUE) {
        have = g_play_pending.exchange(false);
        if (have) memcpy(request, g_request, sizeof(PlayRequest));
        xSemaphoreGive(g_request_mutex);
      }
      if (have) {
        playRequest(*request, stream_memory);
        // Listening resumes now; the engine's warm-up covers the answer's end.
        g_wake_resume_ms = millis();
      }
    }
    if (bits & kNotifyAssist) {
      AssistRequest assist_request = {};
      uint32_t generation = 0;
      if (takeAssistRequest(&assist_request, &generation)) {
        captureSession(assist_request, generation, false);
        g_wake_resume_ms = millis() + kWakeAfterSessionMs;
      }
    }
  }
}

void storeSettings() {
  Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    Serial.printf("%s Could not open preferences\n", kLogPrefix);
    return;
  }
  prefs.putFloat(kPrefsVolumeKey, g_volume.load());
  prefs.putBool(kPrefsMutedKey, g_muted.load());
  char wake_words[assist::kMaxWakeWordListChars];
  assist::formatWakeWordList(g_wake_chosen.load(), wake_words, sizeof(wake_words));
  prefs.putString(kPrefsWakeWordsKey, wake_words);
  prefs.putBool(kPrefsWakeListenKey, g_wake_listen.load());
  prefs.putBool(kPrefsMicKey, g_mic_enabled.load());
  prefs.putUChar(kPrefsMicGainKey, g_mic_gain.load());
  if (!BatchedNvsWrite::finish(prefs)) {
    Serial.printf("%s Could not save the speaker settings\n", kLogPrefix);
  }
}

#endif  // defined(HOMETILES_SPEAKER)

// Only once the codec answered: a board without one never announces audio.
void publishStatus() {
  if (!kSupported || !g_codec_ready.load() || !networkManager.isMqttConnected()) return;
  const char* topic = mqttTopics.topic(TopicKey::AUDIO_STAT);
  if (!topic || !*topic) return;
  StatusFields fields;
  fields.state = static_cast<PublicState>(g_public_state.load());
  fields.volume = g_volume.load();
  fields.muted = g_muted.load();
  fields.error = static_cast<ErrorCode>(g_error.load());
  fields.position_ms = g_position_ms.load();
  fields.duration_ms = g_duration_ms.load();
  fields.bridge_stream = kSupported;
  char id[kMaxRequestIdLength + 1] = {};
#if defined(HOMETILES_SPEAKER)
  portENTER_CRITICAL(&g_id_mux);
  memcpy(id, g_status_id, sizeof(id));
  portEXIT_CRITICAL(&g_id_mux);
#endif
  if (id[0]) fields.id = id;
  char payload[320];
  if (!buildStatusJson(payload, sizeof(payload), fields)) return;
  networkManager.mqttEnqueuePublish(topic, payload, true);
}

// Only once the microphone answered: a board without one never announces
// Assist.
void publishAssistStatus() {
  if (!kSupported || !g_mic_ready.load() || !networkManager.isMqttConnected()) return;
  const char* topic = mqttTopics.topic(TopicKey::ASSIST_STAT);
  if (!topic || !*topic) return;
  assist::StatusFields fields;
  fields.state = static_cast<assist::PublicState>(g_assist_state.load());
  fields.error = static_cast<assist::ErrorCode>(g_assist_error.load());
  fields.wake_words = true;
  fields.available_wake_words = availableWakeWords();
  fields.active_wake_words = assist::activeWakeWordMask(wakeSetting());
  fields.wake_choice = true;
  fields.chosen_wake_words = g_wake_chosen.load();
  fields.wake_listen = g_wake_listen.load();
  fields.mic_state = true;
  fields.mic_enabled = g_mic_enabled.load();
  char id[assist::kMaxRequestIdLength + 1] = {};
#if defined(HOMETILES_SPEAKER)
  portENTER_CRITICAL(&g_id_mux);
  memcpy(id, g_assist_status_id, sizeof(id));
  portEXIT_CRITICAL(&g_id_mux);
#endif
  if (id[0]) fields.id = id;
  char payload[384];
  if (!assist::buildStatusJson(payload, sizeof(payload), fields)) return;
  networkManager.mqttEnqueuePublish(topic, payload, true);
}

uint8_t capabilityBits() {
  return static_cast<uint8_t>((bridgeCapability() ? 1u : 0u) | (micCapability() ? 2u : 0u));
}

}  // namespace

bool supported() { return kSupported; }

void begin() {
#if defined(HOMETILES_SPEAKER)
  if (g_worker) return;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    g_volume.store(clampVolume(prefs.getFloat(kPrefsVolumeKey, kDefaultVolume)));
    g_muted.store(prefs.getBool(kPrefsMutedKey, false));
    // Unknown names (a word dropped from the catalog) are ignored.
    const String wake_words =
        prefs.isKey(kPrefsWakeWordsKey) ? prefs.getString(kPrefsWakeWordsKey) : String();
    const assist::WakeSetting wake = assist::loadWakeSetting(
        wake_words.c_str(), prefs.isKey(kPrefsWakeListenKey),
        prefs.getBool(kPrefsWakeListenKey, false));
    g_wake_chosen.store(wake.chosen);
    g_wake_listen.store(wake.listen);
    g_mic_enabled.store(prefs.getBool(kPrefsMicKey, false));
    const uint8_t gain = prefs.getUChar(kPrefsMicGainKey, kDefaultMicGainStep);
    g_mic_gain.store(gain > kMaxMicGainStep ? kDefaultMicGainStep : gain);
    prefs.end();
  }
  g_request_mutex = xSemaphoreCreateMutex();
  g_request = static_cast<PlayRequest*>(heap_caps_calloc(1, sizeof(PlayRequest), MALLOC_CAP_SPIRAM));
  g_parse_url = static_cast<char*>(heap_caps_calloc(1, kMaxUrlLength + 1, MALLOC_CAP_SPIRAM));
  if (!g_request_mutex || !g_request || !g_parse_url) {
    Serial.printf("%s Out of memory, speaker disabled\n", kLogPrefix);
    return;
  }
  const BaseType_t core = (ARDUINO_RUNNING_CORE == 0) ? 1 : 0;
  if (xTaskCreatePinnedToCoreWithCaps(workerMain, "speaker", kWorkerStackBytes, nullptr,
                                      kWorkerPriority, &g_worker, core,
                                      MALLOC_CAP_SPIRAM) != pdPASS) {
    g_worker = nullptr;
    Serial.printf("%s Worker task could not be created\n", kLogPrefix);
  }
#endif
}

void service() {
  if (!kSupported) return;
  const uint32_t generation = g_state_generation.load();
  if (generation != g_published_generation) {
    g_published_generation = generation;
    publishStatus();
  }
  const uint32_t assist_generation = g_assist_state_generation.load();
  if (assist_generation != g_assist_published_generation) {
    g_assist_published_generation = assist_generation;
    publishAssistStatus();
  }
  const uint8_t capability = capabilityBits();
  if (capability != g_last_capability) {
    g_last_capability = capability;
    // The post-connect path announces the current value after a reconnect.
    if (networkManager.isMqttConnected()) networkManager.publishBridgeConfig();
  }
#if defined(HOMETILES_SPEAKER)
  if (g_persist_pending && !g_shutdown.load() &&
      static_cast<int32_t>(millis() - g_persist_due_ms) >= 0 &&
      static_cast<PublicState>(g_public_state.load()) == PublicState::Idle &&
      !g_play_pending.load() &&
      static_cast<assist::PublicState>(g_assist_state.load()) == assist::PublicState::Idle &&
      !g_assist_pending.load()) {
    g_persist_pending = false;
    storeSettings();
  }
#endif
}

void onMqttConnected() {
  if (!kSupported) return;
  g_published_generation = g_state_generation.load();
  g_assist_published_generation = g_assist_state_generation.load();
  g_last_capability = capabilityBits();
  publishStatus();
  publishAssistStatus();
}

const char* commandTopic() {
  if (!kSupported) return nullptr;
  const char* topic = mqttTopics.topic(TopicKey::AUDIO_CMND);
  return topic && *topic ? topic : nullptr;
}

bool isCommandTopic(const char* topic) {
  const char* command = commandTopic();
  return command && topic && strcmp(topic, command) == 0;
}

const char* assistCommandTopic() {
  if (!kSupported) return nullptr;
  const char* topic = mqttTopics.topic(TopicKey::ASSIST_CMND);
  return topic && *topic ? topic : nullptr;
}

bool isAssistCommandTopic(const char* topic) {
  const char* command = assistCommandTopic();
  return command && topic && strcmp(topic, command) == 0;
}

bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length) {
  if (isCommandTopic(topic)) {
    handleCommandPayload(payload, length);
    return true;
  }
  if (isAssistCommandTopic(topic)) {
    handleAssistCommandPayload(payload, length);
    return true;
  }
  return false;
}

void handleCommandPayload(const uint8_t* payload, size_t length) {
#if defined(HOMETILES_SPEAKER)
  if (!g_worker || g_shutdown.load() || !g_parse_url) return;
  Command command;
  parseCommand(reinterpret_cast<const char*>(payload), length, kHttpsAllowed, &command,
               g_parse_url);
  if (command.status != CommandStatus::Ok) {
    if (logDue(&g_command_log_ms, 10000)) {
      Serial.printf("%s Ignored command: %s (%u bytes)\n", kLogPrefix,
                    commandStatusName(command.status), static_cast<unsigned>(length));
    }
    // A refused play still answers, so Home Assistant can say why.
    if (command.kind == CommandKind::Play && command.id[0] != '\0') {
      setStatusId(command.id);
      setPublic(static_cast<PublicState>(g_public_state.load()),
                command.status == CommandStatus::HttpsUnsupported ? ErrorCode::HttpsUnsupported
                                                                   : ErrorCode::InvalidUrl);
    }
    return;
  }
  switch (command.kind) {
    case CommandKind::Play: {
      if (xSemaphoreTake(g_request_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
      memcpy(g_request->id, command.id, sizeof(g_request->id));
      memcpy(g_request->url, g_parse_url, command.url_length + 1);
      g_play_pending.store(true);
      g_pause_requested.store(false);
      // Playback ends a capture session or a start that has not begun.
      g_assist_pending.store(false);
      g_capture_generation.fetch_add(1);
      xSemaphoreGive(g_request_mutex);
      // Ends the current clip; the worker then takes the new request.
      g_stop_generation.fetch_add(1);
      notifyWorker(kNotifyPlay);
      break;
    }
    case CommandKind::Stop:
      if (xSemaphoreTake(g_request_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_play_pending.store(false);
        xSemaphoreGive(g_request_mutex);
      }
      g_pause_requested.store(false);
      g_stop_generation.fetch_add(1);
      break;
    case CommandKind::Pause:
    case CommandKind::Resume:
      // The worker follows the flag inside the playing clip; idle ignores it.
      if (static_cast<PublicState>(g_public_state.load()) != PublicState::Idle) {
        g_pause_requested.store(command.kind == CommandKind::Pause);
      }
      break;
    case CommandKind::Volume:
    case CommandKind::Mute:
      if (command.kind == CommandKind::Volume) {
        g_volume.store(command.volume);
      } else {
        g_muted.store(command.muted);
      }
      g_state_generation.fetch_add(1);
      g_persist_pending = true;
      g_persist_due_ms = millis() + kPersistDelayMs;
      notifyWorker(kNotifyVolume);
      break;
    case CommandKind::Invalid:
      break;
  }
#else
  (void)payload;
  (void)length;
#endif
}

void handleAssistCommandPayload(const uint8_t* payload, size_t length, bool sealed) {
#if defined(HOMETILES_SPEAKER)
  if (!g_worker || g_shutdown.load() || !g_request_mutex) return;
  assist::Command command;
  assist::parseCommand(reinterpret_cast<const char*>(payload), length, &command);
  if (command.status != assist::CommandStatus::Ok) {
    if (logDue(&g_assist_log_ms, 10000)) {
      Serial.printf("%s Ignored Assist command: %s (%u bytes)\n", kLogPrefix,
                    assist::commandStatusName(command.status), static_cast<unsigned>(length));
    }
    // A refused start still answers, unless another session is listening.
    if (command.kind == assist::CommandKind::Start && command.id[0] != '\0' &&
        g_mic_ready.load() &&
        static_cast<assist::PublicState>(g_assist_state.load()) == assist::PublicState::Idle) {
      setAssistStatusId(command.id);
      setAssistPublic(assist::PublicState::Idle, assist::ErrorCode::Invalid);
    }
    return;
  }
  // Wake words apply at once (the worker starts or stops listening), are
  // stored for the next boot and show in the retained status right away.
  if (command.kind == assist::CommandKind::WakeWords) {
    const assist::WakeSetting wake = assist::applyWakeWordsCommand(
        wakeSetting(), command.wake_words, command.has_listen, command.listen);
    g_wake_chosen.store(wake.chosen);
    g_wake_listen.store(wake.listen);
    g_assist_state_generation.fetch_add(1);
    g_persist_pending = true;
    g_persist_due_ms = millis() + kPersistDelayMs;
    notifyWorker(kNotifyWake);
    char list[assist::kMaxWakeWordListChars];
    assist::formatWakeWordList(wake.chosen, list, sizeof(list));
    Serial.printf("%s Wake word: %s, listening %s\n", kLogPrefix, list,
                  wake.listen ? "on" : "off");
    return;
  }
  if (command.kind == assist::CommandKind::Mic) {
    // Off always applies; on needs the encrypted channel (see the contract).
    if (command.mic_enabled && !sealed) {
      if (logDue(&g_assist_log_ms, 10000)) {
        Serial.printf("%s Microphone on refused: not on the encrypted channel\n", kLogPrefix);
      }
      g_assist_state_generation.fetch_add(1);
      return;
    }
    setMicEnabled(command.mic_enabled, sealed ? "encrypted command" : "command");
    return;
  }
  if (!g_mic_ready.load()) {
    if (logDue(&g_assist_log_ms, 10000)) {
      Serial.printf("%s Assist command ignored: no microphone\n", kLogPrefix);
    }
    return;
  }
  // The privacy switch refuses every session and says why.
  if (command.kind == assist::CommandKind::Start && !g_mic_enabled.load()) {
    if (static_cast<assist::PublicState>(g_assist_state.load()) == assist::PublicState::Idle) {
      setAssistStatusId(command.id);
      setAssistPublic(assist::PublicState::Idle, assist::ErrorCode::MicOff);
    }
    return;
  }
  switch (command.kind) {
    case assist::CommandKind::Start: {
      if (xSemaphoreTake(g_request_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
      memcpy(g_assist_request.id, command.id, sizeof(g_assist_request.id));
      g_assist_request.max_ms = command.max_ms;
      g_assist_pending.store(true);
      // Ends a running session; the worker then takes the new one.
      g_capture_generation.fetch_add(1);
      // A start stops speaker playback first.
      g_play_pending.store(false);
      xSemaphoreGive(g_request_mutex);
      g_stop_generation.fetch_add(1);
      memcpy(g_assist_session_id, command.id, sizeof(g_assist_session_id));
      notifyWorker(kNotifyAssist);
      break;
    }
    case assist::CommandKind::Stop: {
      if (strcmp(command.id, g_assist_session_id) != 0) {
        if (logDue(&g_assist_log_ms, 10000)) {
          Serial.printf("%s Assist stop for another session ignored\n", kLogPrefix);
        }
        return;
      }
      bool withdrawn = false;
      if (xSemaphoreTake(g_request_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        withdrawn = g_assist_pending.exchange(false);
        xSemaphoreGive(g_request_mutex);
      }
      g_capture_generation.fetch_add(1);
      // Stopped before the worker began it: report the session as done.
      if (withdrawn) {
        setAssistStatusId(command.id);
        setAssistPublic(assist::PublicState::Idle, assist::ErrorCode::None);
      }
      break;
    }
    case assist::CommandKind::WakeWords:
    case assist::CommandKind::Mic:
    case assist::CommandKind::Invalid:
      break;
  }
#else
  (void)payload;
  (void)length;
  (void)sealed;
#endif
}

bool micEnabled() { return g_mic_enabled.load(); }

float volume() { return g_volume.load(); }

void setVolume(float volume) {
#if defined(HOMETILES_SPEAKER)
  const float clamped = clampVolume(volume);
  if (clamped == g_volume.load()) return;
  g_volume.store(clamped);
  // Same path as the Bridge's volume command: retained status, stored later.
  g_state_generation.fetch_add(1);
  g_persist_pending = true;
  g_persist_due_ms = millis() + kPersistDelayMs;
  if (g_worker) notifyWorker(kNotifyVolume);
#else
  (void)volume;
#endif
}

uint8_t micGainStep() { return g_mic_gain.load(); }

void setMicGainStep(uint8_t step) {
#if defined(HOMETILES_SPEAKER)
  if (step > kMaxMicGainStep) step = kMaxMicGainStep;
  if (g_mic_gain.exchange(step) == step) return;
  g_persist_pending = true;
  g_persist_due_ms = millis() + kPersistDelayMs;
  // A listening wake word reopens the input with the new gain; a running
  // Assist session keeps its gain until it ends.
  if (g_worker) notifyWorker(kNotifyWake);
#else
  (void)step;
#endif
}

void setMicEnabled(bool enabled, const char* source) {
#if defined(HOMETILES_SPEAKER)
  if (g_mic_enabled.exchange(enabled) == enabled) {
    g_assist_state_generation.fetch_add(1);
    return;
  }
  if (!enabled) {
    // Ends a running session and withdraws one that has not begun.
    if (g_request_mutex && xSemaphoreTake(g_request_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      g_assist_pending.store(false);
      xSemaphoreGive(g_request_mutex);
    } else {
      g_assist_pending.store(false);
    }
    g_capture_generation.fetch_add(1);
  }
  g_assist_state_generation.fetch_add(1);
  g_persist_pending = true;
  g_persist_due_ms = millis() + kPersistDelayMs;
  // The worker starts or stops wake word listening.
  if (g_worker) notifyWorker(kNotifyWake);
  Serial.printf("%s Microphone %s (%s)\n", kLogPrefix, enabled ? "on" : "off",
                source ? source : "panel");
#else
  (void)enabled;
  (void)source;
#endif
}

bool bridgeCapability() { return kSupported && g_codec_ready.load(); }

bool micCapability() { return kSupported && g_mic_ready.load(); }

void shutdown(const char* reason) {
#if defined(HOMETILES_SPEAKER)
  if (!g_worker || g_shutdown.exchange(true)) return;
  const uint32_t ack = g_shutdown_ack.load();
  g_stop_generation.fetch_add(1);
  g_capture_generation.fetch_add(1);
  notifyWorker(kNotifyShutdown);
  const uint32_t started_ms = millis();
  while (g_shutdown_ack.load() == ack &&
         static_cast<uint32_t>(millis() - started_ms) < kShutdownWaitMs) {
    delay(5);
  }
  Serial.printf("%s Stopped for %s\n", kLogPrefix, reason ? reason : "shutdown");
#else
  (void)reason;
#endif
}

}  // namespace speaker
