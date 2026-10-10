#include "src/audio/speaker.h"

#include "src/audio/speaker_select.h"

#include <atomic>

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

#include "mp3dec.h"
#include "src/audio/codecs/es8311/es8311_codec.h"
#include "src/audio/speaker_request.h"
#include "src/core/config/batched_nvs_write.h"
#include "src/devices/device.h"
#include HOMETILES_SPEAKER_BOARD
#endif

namespace speaker {
namespace {

using namespace speaker_contract;

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
uint32_t g_published_generation = 0;
bool g_last_capability = false;

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
// Volume sliders send many steps; store once they settle and nothing plays
// (flash writes stall both cores and would click in the output).
constexpr uint32_t kPersistDelayMs = 3000;

constexpr uint32_t kWorkerStackBytes = 8192;
// Above the loop task so LVGL rendering cannot starve the I2S DMA.
constexpr UBaseType_t kWorkerPriority = 3;
constexpr uint32_t kNotifyPlay = 1u << 0;
constexpr uint32_t kNotifyVolume = 1u << 1;
constexpr uint32_t kNotifyShutdown = 1u << 2;
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

struct PlayRequest {
  char id[kMaxRequestIdLength + 1];
  char url[kMaxUrlLength + 1];
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

// Loop task only.
bool g_persist_pending = false;
uint32_t g_persist_due_ms = 0;
uint32_t g_command_log_ms = 0;

// Worker only.
es8311::Codec g_codec;
i2s_chan_handle_t g_tx = nullptr;
uint32_t g_tx_rate = 0;
bool g_amp_on = false;
uint8_t g_applied_volume_reg = 0xFF;
int g_applied_mute = -1;

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
  const bool level = speaker_board::kAmpActiveHigh ? on : !on;
  gpio_set_level(speaker_board::kAmpEnable, level ? 1 : 0);
  g_amp_on = on;
}

void initAmpPin() {
  gpio_config_t config = {};
  config.pin_bit_mask = 1ULL << speaker_board::kAmpEnable;
  config.mode = GPIO_MODE_OUTPUT;
  gpio_config(&config);
  setAmp(false);
}

void applyVolumeIfChanged() {
  if (!g_codec.dev) return;
  const uint8_t reg = volumeRegister(g_volume.load());
  if (reg != g_applied_volume_reg &&
      es8311::setVolumeRegister(&g_codec, reg) == ESP_OK) {
    g_applied_volume_reg = reg;
  }
  const int mute = g_muted.load() ? 1 : 0;
  if (mute != g_applied_mute && es8311::setMute(&g_codec, mute != 0) == ESP_OK) {
    g_applied_mute = mute;
  }
}

void probeCodec() {
  initAmpPin();
  i2c_master_bus_handle_t bus = speaker_board::bus();
  if (!bus) {
    Serial.printf("%s I2C bus unavailable, speaker disabled\n", kLogPrefix);
    return;
  }
  esp_err_t err = es8311::probe(bus, speaker_board::kCodecAddress, &g_codec);
  if (err == ESP_OK) err = es8311::init(&g_codec, kInitialSampleRate);
  if (err != ESP_OK) {
    Serial.printf("%s ES8311 not ready (%s), speaker disabled\n", kLogPrefix,
                  esp_err_to_name(err));
    es8311::release(&g_codec);
    return;
  }
  applyVolumeIfChanged();
  g_codec_ready.store(true);
  g_state_generation.fetch_add(1);
  Serial.printf("%s ES8311 ready at 0x%02X\n", kLogPrefix, speaker_board::kCodecAddress);
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
  if (!es8311::supportsSampleRate(rate)) {
    Serial.printf("%s Unsupported sample rate %lu Hz\n", kLogPrefix,
                  static_cast<unsigned long>(rate));
    return ErrorCode::UnsupportedFormat;
  }
  if (g_tx && g_tx_rate == rate) return ErrorCode::None;
  if (es8311::setSampleRate(&g_codec, rate) != ESP_OK) return ErrorCode::CodecUnavailable;
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

// ---------------------------------------------------------------------------
// Worker: streaming download
// ---------------------------------------------------------------------------
// The HTTP objects live in the worker's playRequest() frame; Source points at
// them. pump() never blocks on the network, so the decoder can keep the I2S
// DMA fed between reads.
struct Source {
  HTTPClient* http = nullptr;
  NetworkClient* stream = nullptr;
  StreamBuffer buffer;
  // Bytes still expected from a response with Content-Length, -1 otherwise.
  int64_t remaining = -1;
  bool ended = false;
  bool failed = false;
  uint32_t last_data_ms = 0;
};

void pump(Source* src) {
  if (src->ended) return;
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

// ---------------------------------------------------------------------------
// Worker: decoding
// ---------------------------------------------------------------------------
ErrorCode playMp3(Source* src, uint32_t generation) {
  if (!fillTo(src, 10, generation)) return ErrorCode::None;
  const size_t tag = id3v2Length(src->buffer.read(), src->buffer.buffered());
  if (tag > 0 && !skipBytes(src, tag, generation)) return ErrorCode::None;

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
    const int err = MP3Decode(decoder, &frame, &frame_left, pcm, 0);
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
    played_any = true;
  }
  if (result == ErrorCode::None && !played_any && !stopRequested(generation)) {
    result = synced ? ErrorCode::DecodeFailed : ErrorCode::UnsupportedFormat;
  }
  heap_caps_free(pcm);
  MP3FreeDecoder(decoder);
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
  uint64_t frames_left = bounded ? info.data_length / frame_bytes : UINT64_MAX;
  int16_t* chunk = static_cast<int16_t*>(heap_caps_malloc(
      kWavChunkFrames * info.channels * sizeof(int16_t), MALLOC_CAP_INTERNAL));
  if (!chunk) return ErrorCode::OutOfMemory;
  ErrorCode result = ErrorCode::None;
  bool started = false;
  if (!prebuffer(src, generation)) frames_left = 0;
  while (frames_left > 0 && !stopRequested(generation)) {
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
      const ErrorCode output = openOutput(info.sample_rate);
      if (output != ErrorCode::None) {
        result = output;
        break;
      }
      started = true;
    }
    // The buffer may sit at an odd offset; copy before reading int16.
    memcpy(chunk, src->buffer.read(), frames * frame_bytes);
    src->buffer.consume(frames * frame_bytes);
    if (info.channels == 2) downmixToMono(chunk, frames);
    applyVolumeIfChanged();
    if (!writeFrames(chunk, frames)) {
      result = ErrorCode::CodecUnavailable;
      break;
    }
    frames_left -= frames;
  }
  heap_caps_free(chunk);
  return result;
}

ErrorCode playUrl(const char* url, uint32_t generation, uint8_t* memory) {
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
  const bool https = strncmp(url, "https://", 8) == 0;
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
  setStatusId(request.id);
  if (!g_codec_ready.load()) {
    setPublic(PublicState::Idle, ErrorCode::CodecUnavailable);
    return;
  }
  if (!stream_memory) {
    setPublic(PublicState::Idle, ErrorCode::OutOfMemory);
    return;
  }
  setPublic(PublicState::Playing, ErrorCode::None);
  Serial.printf("%s Playing %s\n", kLogPrefix, request.id);
  const ErrorCode result = playUrl(request.url, generation, stream_memory);
  closeOutput();
  if (result != ErrorCode::None) {
    Serial.printf("%s Clip %s failed: %s\n", kLogPrefix, request.id, errorCodeName(result));
  }
  // A newer request keeps the state "playing" instead of flashing idle.
  if (!(g_play_pending.load() && result == ErrorCode::None)) {
    setPublic(PublicState::Idle, result);
  }
}

void workerMain(void*) {
  probeCodec();
  PlayRequest* request = static_cast<PlayRequest*>(
      heap_caps_malloc(sizeof(PlayRequest), MALLOC_CAP_SPIRAM));
  // Allocated once: a fragmented PSRAM heap later cannot refuse a stream.
  uint8_t* stream_memory =
      static_cast<uint8_t*>(heap_caps_malloc(kStreamBufferBytes, MALLOC_CAP_SPIRAM));
  if (!stream_memory) Serial.printf("%s Stream buffer unavailable\n", kLogPrefix);
  for (;;) {
    uint32_t bits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
    if ((bits & kNotifyShutdown) || g_shutdown.load()) {
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
      if (have) playRequest(*request, stream_memory);
    }
  }
}

void storeVolume() {
  Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    Serial.printf("%s Could not open preferences\n", kLogPrefix);
    return;
  }
  prefs.putFloat(kPrefsVolumeKey, g_volume.load());
  prefs.putBool(kPrefsMutedKey, g_muted.load());
  if (!BatchedNvsWrite::finish(prefs)) {
    Serial.printf("%s Could not save the volume\n", kLogPrefix);
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
  char id[kMaxRequestIdLength + 1] = {};
#if defined(HOMETILES_SPEAKER)
  portENTER_CRITICAL(&g_id_mux);
  memcpy(id, g_status_id, sizeof(id));
  portEXIT_CRITICAL(&g_id_mux);
#endif
  if (id[0]) fields.id = id;
  char payload[224];
  if (!buildStatusJson(payload, sizeof(payload), fields)) return;
  networkManager.mqttEnqueuePublish(topic, payload, true);
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
  const bool capability = bridgeCapability();
  if (capability != g_last_capability) {
    g_last_capability = capability;
    // The post-connect path announces the current value after a reconnect.
    if (networkManager.isMqttConnected()) networkManager.publishBridgeConfig();
  }
#if defined(HOMETILES_SPEAKER)
  if (g_persist_pending && !g_shutdown.load() &&
      static_cast<int32_t>(millis() - g_persist_due_ms) >= 0 &&
      static_cast<PublicState>(g_public_state.load()) == PublicState::Idle &&
      !g_play_pending.load()) {
    g_persist_pending = false;
    storeVolume();
  }
#endif
}

void onMqttConnected() {
  if (!kSupported) return;
  g_published_generation = g_state_generation.load();
  g_last_capability = bridgeCapability();
  publishStatus();
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

bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length) {
  if (!isCommandTopic(topic)) return false;
  handleCommandPayload(payload, length);
  return true;
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
      g_stop_generation.fetch_add(1);
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

bool bridgeCapability() { return kSupported && g_codec_ready.load(); }

void shutdown(const char* reason) {
#if defined(HOMETILES_SPEAKER)
  if (!g_worker || g_shutdown.exchange(true)) return;
  const uint32_t ack = g_shutdown_ack.load();
  g_stop_generation.fetch_add(1);
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
