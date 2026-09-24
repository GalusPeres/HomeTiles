#include "src/video/local_camera/local_camera.h"

#include "src/video/local_camera/camera_select.h"

#include <atomic>

#include "src/core/config/batched_nvs_write.h"
#include "src/core/power/power_manager.h"
#include "src/devices/device.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/network_manager.h"
#include "src/video/local_camera/local_camera_contract.h"
#include "src/video/local_camera/local_camera_request.h"
#include "src/video/local_camera/local_camera_stream_contract.h"

#if defined(HOMETILES_LOCAL_CAMERA)
#include <math.h>

#include <algorithm>

#include <driver/isp.h>
#include <driver/jpeg_encode.h>
#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_cam_ctlr.h>
#include <esp_cam_ctlr_csi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "src/core/display/display_manager.h"
#include "src/core/display/dma2d_arbiter.h"
#include "src/video/camera_stream.h"
#include "src/video/local_camera/camera_driver.h"
#include "src/video/local_camera/local_camera_upload.h"
#include HOMETILES_LOCAL_CAMERA_BOARD
#endif

namespace local_camera {
namespace {

using namespace local_camera_contract;
using local_camera_stream::StopReason;
using local_camera_stream::StreamHints;
using local_camera_stream::StreamSettings;
using local_camera_stream::StreamStatus;
using local_camera_stream::StreamWindow;

// Persisted opt-in. A separate key keeps the Settings record layout unchanged.
constexpr char kPrefsNamespace[] = "tab5_config";
constexpr char kPrefsEnabledKey[] = "local_cam_en";
// Live-stream mode id (local_camera_stream::kModes, 0 = Auto), u8.
constexpr char kPrefsStreamModeKey[] = "lcam_mode";
constexpr char kPrefsMirrorKey[] = "lcam_mirror";
// On-display indicator style (IndicatorStyle as uint8_t).
constexpr char kPrefsIndicatorKey[] = "lcam_ind";
// Custom stream mode (local_camera_stream::kModeCustom): frames per second and
// JPEG quality.
constexpr char kPrefsCustomFpsKey[] = "lcam_cfps";
constexpr char kPrefsCustomQualityKey[] = "lcam_cq";
// Persisted pause of v0.6.12b17; removed on boot (the pause is RAM-only now).
constexpr char kPrefsLegacyPausedKey[] = "lcam_pause";
// User image controls (local_camera_contract::ImageSettings), one key each:
// i8 for the signed controls, u8 for saturation. Missing keys keep defaults.
constexpr char kPrefsBrightnessKey[] = "lcam_bright";
constexpr char kPrefsContrastKey[] = "lcam_contrast";
constexpr char kPrefsSaturationKey[] = "lcam_sat";
constexpr char kPrefsRedKey[] = "lcam_red";
constexpr char kPrefsBlueKey[] = "lcam_blue";
constexpr char kPrefsGainKey[] = "lcam_gain";

// Web Admin status values. MQTT uses the reduced ready/disabled/error set.
enum class ServiceState : uint8_t { Disabled, Probing, Ready, NotFound, Error };

enum class Detail : uint8_t {
  None,
  BusUnavailable,
  PhySupplyFailed,
  SensorNotFound,
  SensorInitFailed,
  WorkerUnavailable,
  NoFrames,
  PipelineFailed,
  EncoderBusy,
  TooLarge,
  PublishFailed,
};

const char* serviceStateName(ServiceState state) {
  switch (state) {
    case ServiceState::Disabled: return "disabled";
    case ServiceState::Probing: return "probing";
    case ServiceState::Ready: return "ready";
    case ServiceState::NotFound: return "not_found";
    case ServiceState::Error: return "error";
  }
  return "error";
}

const char* detailName(Detail detail) {
  switch (detail) {
    case Detail::None: return "";
    case Detail::BusUnavailable: return "i2c_bus_unavailable";
    case Detail::PhySupplyFailed: return "phy_supply_failed";
    case Detail::SensorNotFound: return "sensor_not_found";
    case Detail::SensorInitFailed: return "sensor_init_failed";
    case Detail::WorkerUnavailable: return "worker_unavailable";
    case Detail::NoFrames: return "no_frames";
    case Detail::PipelineFailed: return "pipeline_failed";
    case Detail::EncoderBusy: return "encoder_busy";
    case Detail::TooLarge: return "too_large";
    case Detail::PublishFailed: return "publish_failed";
  }
  return "";
}

#if defined(HOMETILES_LOCAL_CAMERA)
constexpr bool kSupported = true;
#else
constexpr bool kSupported = false;
#endif

// Sensor name and JPEG size of the active board for the status payloads.
#if defined(HOMETILES_LOCAL_CAMERA)
constexpr const char* kSensorName = local_camera_board::kMode.name;
constexpr uint16_t kStatusWidth = local_camera_board::kMode.image_width;
constexpr uint16_t kStatusHeight = local_camera_board::kMode.image_height;
// Clockwise turn the Bridge applies to every JPEG (quarter-turn mounting).
constexpr uint16_t kStatusRotate = local_camera_board::kMode.quarter_turn ? 90 : 0;
#else
constexpr const char* kSensorName = "";
constexpr uint16_t kStatusWidth = 0;
constexpr uint16_t kStatusHeight = 0;
constexpr uint16_t kStatusRotate = 0;
#endif

std::atomic<bool> g_enabled{false};
std::atomic<uint8_t> g_state{static_cast<uint8_t>(ServiceState::Disabled)};
std::atomic<uint8_t> g_detail{static_cast<uint8_t>(Detail::None)};
std::atomic<uint16_t> g_chip_id{0};
std::atomic<uint32_t> g_state_generation{0};
std::atomic<bool> g_capture_busy{false};
std::atomic<uint32_t> g_capture_ok{0};
std::atomic<uint32_t> g_capture_failed{0};
std::atomic<uint32_t> g_last_jpeg_bytes{0};
std::atomic<uint32_t> g_last_capture_ms{0};

// Loop-task only.
[[maybe_unused]] RequestRateLimiter g_rate_limiter;
uint32_t g_published_generation = UINT32_MAX;
bool g_last_capability = false;
[[maybe_unused]] uint32_t g_last_request_log_ms = 0;

// --- Live stream ----------------------------------------------------------
// Stored mode and a generation the capture worker compares per frame.
std::atomic<uint8_t> g_stream_mode{local_camera_stream::kModeAuto};
// Values of the Custom mode (loop task writes, worker reads per stream start
// and mode generation).
std::atomic<uint8_t> g_custom_fps{local_camera_stream::CustomMode{}.fps};
std::atomic<uint8_t> g_custom_quality{local_camera_stream::CustomMode{}.quality};

[[maybe_unused]] local_camera_stream::CustomMode currentCustomMode() {
  local_camera_stream::CustomMode mode;
  mode.fps = g_custom_fps.load();
  mode.quality = g_custom_quality.load();
  return mode;
}
// Horizontal mirror of the delivered image (Web Admin setting).
std::atomic<bool> g_mirror{false};
// Pause from Home Assistant (Bridge switch): no capture at all, but the
// camera stays announced to the Bridge. RAM-only.
std::atomic<bool> g_paused{false};
// Web Admin storage work in progress (beginStorageHold()): no stream and no
// snapshot meanwhile. Nesting count, loop task only; other tasks only read it.
[[maybe_unused]] std::atomic<uint8_t> g_storage_holds{0};
// The hold lingers after the last storage work (millis(), 0 = none): the
// layout reload and folder preloads that follow a save read the flash too.
[[maybe_unused]] std::atomic<uint32_t> g_storage_linger_until_ms{0};

inline bool storageHoldActive() {
  if (g_storage_holds.load() != 0) return true;
  const uint32_t until = g_storage_linger_until_ms.load();
  return until != 0 && static_cast<int32_t>(until - millis()) > 0;
}
// Stored on-display indicator style (Web Admin, experimental).
std::atomic<uint8_t> g_indicator_style{static_cast<uint8_t>(IndicatorStyle::Pill)};
// Privacy indicator (indicatorActive()): set right before the sensor starts
// streaming, cleared by the standby that every stop path runs.
std::atomic<bool> g_sensor_capturing{false};
std::atomic<uint32_t> g_indicator_hold_until_ms{0};
constexpr uint32_t kIndicatorHoldMs = 2000;
// Set by endStream(): the next standby skips the hold, so the pill disappears
// right away as the confirmation of the tap.
std::atomic<bool> g_indicator_skip_hold{false};
// User image controls: written by the loop task, read by the capture worker.
// The worker compares the generation per frame and re-applies CCM and gamma.
portMUX_TYPE g_image_mux = portMUX_INITIALIZER_UNLOCKED;
ImageSettings g_image;
[[maybe_unused]] std::atomic<uint32_t> g_image_generation{0};

ImageSettings currentImageSettings() {
  portENTER_CRITICAL(&g_image_mux);
  const ImageSettings image = g_image;
  portEXIT_CRITICAL(&g_image_mux);
  return image;
}
[[maybe_unused]] std::atomic<uint32_t> g_stream_mode_generation{0};
// Loop task -> worker: keep streaming. The worker clears it when it stops by
// itself (popup, sleep, error) so the next keepalive can start it again.
[[maybe_unused]] std::atomic<bool> g_stream_wanted{false};
// Worker: a stream run is in progress (set before the pipeline starts).
[[maybe_unused]] std::atomic<bool> g_stream_running{false};
[[maybe_unused]] std::atomic<uint8_t> g_stream_stop_reason{static_cast<uint8_t>(StopReason::None)};
// millis() of the last stream end caused by an error; restarts wait a while.
[[maybe_unused]] std::atomic<uint32_t> g_stream_error_ms{0};
// OTA/restart release: no new stream until this millis() value.
[[maybe_unused]] std::atomic<uint32_t> g_stream_blocked_until_ms{0};

// Loop-task only: the Bridge session that is currently allowed to stream.
struct LoopSession {
  bool valid = false;
  char session[local_camera_stream::kMaxSessionLength + 1] = {};
  uint32_t deadline_ms = 0;
};
[[maybe_unused]] LoopSession g_session;
// Loop-task only: the session the display ended (endStream()); its keepalives
// are ignored so an open viewer cannot restart it.
[[maybe_unused]] char g_ended_session[local_camera_stream::kMaxSessionLength + 1] = {};
[[maybe_unused]] uint32_t g_stream_log_ms = 0;

// Request hints for the Auto mode (loop writes, worker reads) and the
// /api/status snapshot (worker writes, web handler reads).
[[maybe_unused]] portMUX_TYPE g_stream_mux = portMUX_INITIALIZER_UNLOCKED;
[[maybe_unused]] StreamHints g_stream_hints;
[[maybe_unused]] StreamStatus g_stream_status;

[[maybe_unused]] StreamHints currentStreamHints() {
  portENTER_CRITICAL(&g_stream_mux);
  const StreamHints hints = g_stream_hints;
  portEXIT_CRITICAL(&g_stream_mux);
  return hints;
}

[[maybe_unused]] void updateStreamStatus(const StreamStatus& status) {
  portENTER_CRITICAL(&g_stream_mux);
  g_stream_status = status;
  portEXIT_CRITICAL(&g_stream_mux);
}

ServiceState currentState() {
  return static_cast<ServiceState>(g_state.load());
}

[[maybe_unused]] void setState(ServiceState state, Detail detail) {
  g_detail.store(static_cast<uint8_t>(detail));
  g_state.store(static_cast<uint8_t>(state));
  g_state_generation.fetch_add(1);
}

[[maybe_unused]] bool logDue(uint32_t* last_ms, uint32_t interval_ms) {
  const uint32_t now_ms = millis();
  if (*last_ms != 0 && static_cast<uint32_t>(now_ms - *last_ms) < interval_ms) {
    return false;
  }
  *last_ms = now_ms == 0 ? 1 : now_ms;
  return true;
}

String deviceTopic(const char* leaf, const char* id = nullptr) {
  String topic = mqttTopics.deviceBase();
  topic += leaf;
  if (id) topic += id;
  return topic;
}

[[maybe_unused]] void publishErrorReply(const char* id, ErrorCode code) {
  if (!networkManager.isMqttConnected()) return;
  char payload[64];
  if (!buildErrorJson(payload, sizeof(payload), code)) return;
  const String topic = deviceTopic(kErrorLeaf, id);
  networkManager.mqttEnqueuePublish(topic.c_str(), payload, false);
}

StatusFields currentStatusFields() {
  StatusFields fields;
  fields.width = kStatusWidth;
  fields.height = kStatusHeight;
  fields.rotate = kStatusRotate;
  if (g_ended_session[0] != '\0') fields.ended_session = g_ended_session;
  if (!g_enabled.load()) {
    fields.state = PublicState::Disabled;
    return fields;
  }
  if (g_paused.load()) {
    fields.state = PublicState::Disabled;
    fields.paused = true;
    return fields;
  }
  switch (currentState()) {
    case ServiceState::Ready:
      fields.state = PublicState::Ready;
      fields.sensor = kSensorName;
      break;
    case ServiceState::Disabled:
      fields.state = PublicState::Disabled;
      break;
    default:
      fields.state = PublicState::Error;
      fields.error = errorCodeName(ErrorCode::SensorUnavailable);
      break;
  }
  return fields;
}

// Retained state, skipped while the first probe after enabling is running.
void publishStatus() {
  if (!kSupported || !networkManager.isMqttConnected()) return;
  if (g_enabled.load() && currentState() == ServiceState::Probing) return;
  char payload[288];
  if (!buildStatusJson(payload, sizeof(payload), currentStatusFields())) return;
  const char* topic = mqttTopics.topic(TopicKey::LOCAL_CAMERA_STAT);
  if (!topic || !*topic) return;
  networkManager.mqttEnqueuePublish(topic, payload, true);
}

// ===========================================================================
// Hardware path: devices with a camera board file (camera_select.h).
// ===========================================================================
#if defined(HOMETILES_LOCAL_CAMERA)

namespace board = local_camera_board;
constexpr const SensorMode& kMode = board::kMode;

constexpr uint32_t kNotifyProbe = 1u << 0;
constexpr uint32_t kNotifyCapture = 1u << 1;
constexpr uint32_t kNotifyRelease = 1u << 2;
constexpr uint32_t kNotifyDisable = 1u << 3;
constexpr uint32_t kNotifyShutdown = 1u << 4;
constexpr uint32_t kNotifyStream = 1u << 5;
constexpr uint32_t kNotifyStreamStop = 1u << 6;

constexpr uint32_t kWorkerStackBytes = 8192;
// Same core and idle priority as the MQTT worker and the HA camera decoder:
// never outrank the worker that has to upload the snapshot.
constexpr UBaseType_t kWorkerPriority = tskIDLE_PRIORITY;
constexpr uint32_t kPipelineIdleReleaseMs = 30000;

constexpr uint32_t kIspClockHz = 80 * 1000 * 1000;
constexpr uint32_t kFrameBytes =
    uint32_t{kMode.frame_width} * kMode.frame_height * 2;  // RGB565
constexpr uint32_t kImageWidth = kMode.image_width;
constexpr uint32_t kImageHeight = kMode.image_height;
// This silicon has no ISP crop and the JPEG encoder has no stride: the sensor
// window must be the JPEG size.
static_assert(kMode.frame_width == kMode.image_width &&
                  kMode.frame_height == kMode.image_height,
              "the sensor must deliver the JPEG size");
// A sensor mounted a quarter turn from the landscape image: the JPEG leaves
// the panel portrait and the receiver (the Bridge) turns it 90 degrees
// clockwise, announced as "rotate" in the retained status. A PPA turn on the
// panel held the 2D-DMA for ~29 ms per frame and made the display sluggish.
constexpr bool kQuarterTurn = kMode.quarter_turn;
// RAW8 or RAW10 from the sensor; the ISP output (RGB565) is the same.
constexpr bool kRaw8 = kMode.raw_bits == 8;
static_assert(kMode.raw_bits == 8 || kMode.raw_bits == 10, "RAW8 or RAW10 only");
// The Bridge turns the JPEG losslessly (whole MCUs only). 4:2:2 (16x8 MCUs)
// would turn into the uncommon 4:4:0; 4:2:0 (16x16 MCUs) stays 4:2:0.
constexpr jpeg_down_sampling_type_t kJpegSubsampling =
    kQuarterTurn ? JPEG_DOWN_SAMPLING_YUV420 : JPEG_DOWN_SAMPLING_YUV422;
static_assert(!kQuarterTurn || (kMode.image_width % 16 == 0 && kMode.image_height % 16 == 0),
              "a lossless quarter turn needs whole 16x16 MCUs");
constexpr size_t kFrameBufferAlign = 128;
constexpr uint32_t kJpegInputBytes = kImageWidth * kImageHeight * 2;
// ISP statistics see the frame as the sensor delivers it: 5x5 blocks over
// the largest centred window whose sides are multiples of 5.
constexpr uint32_t kStatsWidth = kMode.frame_width / 5 * 5;
constexpr uint32_t kStatsHeight = kMode.frame_height / 5 * 5;
constexpr uint32_t kStatsLeft = (kMode.frame_width - kStatsWidth) / 2;
constexpr uint32_t kStatsTop = (kMode.frame_height - kStatsHeight) / 2;
// Generously sized: an undersized hardware JPEG output buffer is unsafe.
constexpr size_t kJpegOutputCapacity = kJpegInputBytes / 2;
constexpr uint32_t kFirstFrameTimeoutMs = 600;
constexpr uint32_t kAutoExposureBudgetMs = 1800;
constexpr uint8_t kAutoExposureMaxIterations = 10;
constexpr int kStatisticsTimeoutMs = 200;
constexpr uint32_t kFreezeTimeoutMs = 300;
constexpr uint32_t kMinAwbSamples = 1000;
// The last steps keep noisy low-light stills under max_bytes.
constexpr uint8_t kJpegQualities[] = {80, 65, 50, 38, 25, 15};
// Upload wait slices: a disable, shutdown or the request deadline can still
// withdraw a stream that the MQTT worker has not started.
constexpr uint32_t kStreamPollMs = 100;
constexpr uint32_t kShutdownWaitMs = 1500;
// Bus-lock or SCL timeouts on the SCCB bus shared with touch are retried.
constexpr uint8_t kProbeAttempts = 3;
constexpr uint32_t kProbeRetryDelayMs = 30;
constexpr uint32_t kSensorRetryDelayMs = 5;
constexpr uint32_t kErrorLogIntervalMs = 10000;

struct FrameEvent {
  int8_t index;
  uint32_t received;
};

// Shared with the CSI interrupt callbacks.
struct IsrState {
  uint8_t* buffers[2] = {nullptr, nullptr};
  size_t buffer_bytes = 0;
  volatile int8_t inflight = -1;
  volatile int8_t frozen = -1;
  volatile bool armed = false;
  QueueHandle_t frames = nullptr;
};

// Worker-owned resources.
struct Pipeline {
  esp_cam_ctlr_handle_t csi = nullptr;
  bool csi_enabled = false;
  bool csi_running = false;
  isp_proc_handle_t isp = nullptr;
  bool isp_enabled = false;
  isp_ae_ctlr_t ae = nullptr;
  isp_awb_ctlr_t awb = nullptr;
  jpeg_encoder_handle_t jpeg = nullptr;
  uint8_t* jpeg_out = nullptr;
  size_t jpeg_capacity = 0;
  // Base auto-exposure target of the statistics sample point; the brightness
  // setting scales it (aeTarget()).
  uint32_t ae_target = 115;
  // User image settings last pushed to the ISP (g_image_generation) and the
  // contrast of the loaded gamma curve.
  uint32_t image_generation = 0;
  int8_t gamma_contrast = 0;
  // Digital gain step baked into the loaded gamma curve, and whether the AE
  // statistics see it (sample point after gamma).
  uint8_t gamma_digital_step = 0;
  bool ae_after_gamma = false;
  // Stream frame preparation on the PPA (crop, turn and scale in hardware).
  ppa_client_handle_t ppa = nullptr;
  SemaphoreHandle_t ppa_done = nullptr;
  // A PPA transform that never finished may still own its buffers: they are
  // kept (leaked) instead of reused until the next restart.
  bool ppa_wedged = false;
  bool ready = false;
  // The CSI receiver ran since the pipeline was built (see
  // releaseUsedPipeline()).
  bool started = false;
};

TaskHandle_t g_worker = nullptr;
std::atomic<bool> g_abort{false};
std::atomic<uint32_t> g_shutdown_ack{0};
SnapshotRequest g_pending;  // Written by the loop task before g_capture_busy.

IsrState g_isr;
Pipeline g_pipe;
board::SccbBus g_sccb_bus{};
bool g_hw_acquired = false;
board::Sensor g_sensor;
// True only after the chip ID matched; register writes go to no other device.
bool g_sensor_identified = false;
bool g_sensor_ready = false;
uint32_t g_last_used_ms = 0;
ExposureSetting g_exposure;

// Frame time for the current exposure. Exposures beyond the default stretch
// the frame (the sensor lengthens VTS), so waits scale with them.
// The camera is fixed to the board: when the display is turned by 180 degrees
// (rotation setting, including Auto), the image turns with it. Quarter turns
// keep the landscape image.
bool imageRotated180() {
  const uint8_t turns = static_cast<uint8_t>(
      (displayManager.getRotation() + 4u - Device::kRotationDefault) & 3u);
  return kMode.rotate_180 != (turns == 2);
}

// Sensor readout orientation last written (orientationCode), or unknown.
// Worker task only.
constexpr uint8_t kOrientationUnknown = 0xff;
uint8_t g_applied_orientation = kOrientationUnknown;
uint32_t g_orientation_log_ms = 0;
// Frames still in flight after a live change carry the previous readout.
constexpr uint8_t kOrientationSettleFrames = 2;

uint32_t currentFrameMs() {
  const uint32_t base = kMode.default_exposure_lines;
  const uint32_t lines = g_exposure.lines > base ? g_exposure.lines : base;
  return (uint32_t{kMode.frame_ms} * lines + base - 1) / base;
}
WhiteBalanceGains g_gains;
uint32_t g_capture_log_ms = 0;
// Pipeline setup/release repeats after every idle release; log it sparsely.
uint32_t g_pipeline_log_ms = 0;
uint32_t g_error_log_ms = 0;
// A failed pipeline setup keeps the state Ready and is retried by every
// accepted request; its failure lines are throttled separately.
uint32_t g_pipeline_error_log_ms = 0;
uint32_t g_pipeline_errors_suppressed = 0;
uint32_t g_sensor_log_ms = 0;
uint32_t g_upload_log_ms = 0;

// Contrast used by gammaCurve(); esp_isp_gamma_fill_curve_points() takes a
// plain function pointer. Worker task only.
int g_gamma_curve_contrast = 0;
int8_t g_gamma_curve_brightness = 0;
float g_gamma_curve_gain = 1.0f;
// Digital gain step kept across pipelines, so the next stream or snapshot
// starts where the last one ended. Worker task only.
uint8_t g_digital_step = 0;
// Highest digital gain step the Max. gain setting allows (applyGainLimit()).
uint8_t g_max_digital_step = kMaxDigitalGainStep;
uint32_t g_digital_log_ms = 0;
constexpr uint32_t kDigitalGainLogIntervalMs = 10000;
// Last exposure for /api/local-camera, written by the worker.
std::atomic<uint16_t> g_report_lines{0};
std::atomic<uint16_t> g_report_gain_x16{0};
std::atomic<uint16_t> g_report_digital_x100{100};
std::atomic<uint16_t> g_report_luma{0};

// Gamma with the sensor pedestal removed: the ISP has no black level step in
// this pipeline, so input values up to the pedestal map to black. The user
// contrast is an S-curve on top (identity at 0).
uint32_t gammaCurve(uint32_t x) {
  return gammaLutValue(x, kMode.black_level, kGammaExponent, g_gamma_curve_contrast,
                       g_gamma_curve_gain);
}

// Effective auto-exposure target: the base target scaled by the brightness.
uint32_t aeTarget() {
  return adjustedAeTarget(g_pipe.ae_target, currentImageSettings().brightness);
}

bool onGetNewTransaction(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t* trans,
                         void*) {
  int8_t next;
  if (g_isr.frozen >= 0) {
    next = static_cast<int8_t>(1 - g_isr.frozen);
  } else {
    next = g_isr.inflight < 0 ? 0 : static_cast<int8_t>(1 - g_isr.inflight);
  }
  g_isr.inflight = next;
  trans->buffer = g_isr.buffers[next];
  trans->buflen = g_isr.buffer_bytes;
  return false;
}

bool onTransactionFinished(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t* trans,
                           void*) {
  int8_t index = -1;
  if (trans->buffer == g_isr.buffers[0]) index = 0;
  else if (trans->buffer == g_isr.buffers[1]) index = 1;
  if (index < 0) return false;
  const bool complete = trans->received_size == 0 ||
                        trans->received_size >= g_isr.buffer_bytes;
  if (g_isr.armed && complete) {
    g_isr.frozen = index;
    g_isr.armed = false;
  }
  FrameEvent event{index, static_cast<uint32_t>(trans->received_size)};
  BaseType_t woken = pdFALSE;
  if (g_isr.frames) xQueueOverwriteFromISR(g_isr.frames, &event, &woken);
  return woken == pdTRUE;
}

// Last failure (step and error code) for /api/status. Not every board exposes
// the application log over USB, so remote diagnosis needs it.
portMUX_TYPE g_last_error_mux = portMUX_INITIALIZER_UNLOCKED;
char g_last_error[80] = "";

void rememberError(const char* what, esp_err_t err) {
  char text[sizeof(g_last_error)];
  snprintf(text, sizeof(text), "%s: %s (0x%x)", what, esp_err_to_name(err),
           static_cast<unsigned>(err));
  portENTER_CRITICAL(&g_last_error_mux);
  memcpy(g_last_error, text, sizeof(text));
  portEXIT_CRITICAL(&g_last_error_mux);
}

// Outcome of the last auto exposure / white balance run for /api/status, so
// image tuning can be judged from real numbers without a serial log.
struct AutoTuneReport {
  uint32_t steps = 0;
  uint32_t ae_samples = 0;
  uint32_t awb_samples = 0;
  uint32_t white_patches = 0;
  uint32_t luma = 0;
  uint32_t target = 0;
  uint16_t lines = 0;
  uint16_t gain_x16 = 0;
  uint16_t digital_x100 = 100;
  uint16_t wb_red_x100 = 0;
  uint16_t wb_blue_x100 = 0;
  bool limited = false;
};
AutoTuneReport g_last_autotune;
bool g_has_autotune = false;

void rememberAutoTune(const AutoTuneReport& report) {
  portENTER_CRITICAL(&g_last_error_mux);
  g_last_autotune = report;
  g_has_autotune = true;
  portEXIT_CRITICAL(&g_last_error_mux);
}

void logCaptureError(const char* what, esp_err_t err) {
  rememberError(what, err);
  if (!logDue(&g_error_log_ms, kErrorLogIntervalMs)) return;
  Serial.printf("[LocalCam] %s: %s (0x%x)\n", what, esp_err_to_name(err),
                static_cast<unsigned>(err));
}

// True when a pipeline failure line may be printed now; reports how many
// were suppressed since the last one.
bool pipelineErrorLogDue() {
  if (!logDue(&g_pipeline_error_log_ms, kErrorLogIntervalMs)) {
    ++g_pipeline_errors_suppressed;
    return false;
  }
  if (g_pipeline_errors_suppressed != 0) {
    Serial.printf("[LocalCam] %u further pipeline failures suppressed\n",
                  static_cast<unsigned>(g_pipeline_errors_suppressed));
    g_pipeline_errors_suppressed = 0;
  }
  return true;
}

bool abortRequested() {
  return g_abort.load() || !g_enabled.load() || g_paused.load() || storageHoldActive();
}

ErrorCode abortCode() {
  return g_enabled.load() && !g_paused.load() ? ErrorCode::Busy : ErrorCode::Disabled;
}

// Software standby (0x0100 = 0 first); this board has no power-down or reset
// line. One retry covers a transient lock timeout on the shared touch bus.
esp_err_t sensorStandby() {
  if (g_sensor_capturing.exchange(false)) {
    const bool skip_hold = g_indicator_skip_hold.exchange(false);
    g_indicator_hold_until_ms.store(millis() + (skip_hold ? 0 : kIndicatorHoldMs));
  }
  esp_err_t err = g_sensor.setStream(false);
  if (err != ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(kSensorRetryDelayMs));
    err = g_sensor.setStream(false);
  }
  if (err != ESP_OK && logDue(&g_sensor_log_ms, kErrorLogIntervalMs)) {
    Serial.printf("[LocalCam] Sensor standby failed: %s (0x%x)\n",
                  esp_err_to_name(err), static_cast<unsigned>(err));
  }
  return err;
}

void applyColorCorrection() {
  if (!g_pipe.isp) return;
  esp_isp_ccm_config_t ccm = {};
  buildImageCcm(kBaseCcm, g_gains, currentImageSettings(), ccm.matrix);
  ccm.saturation = true;
  ccm.flags.update_once_configured = 1;
  esp_isp_ccm_configure(g_pipe.isp, &ccm);
}

// Loads the gamma curve for one contrast into all three channels. The IDF
// allows esp_isp_gamma_configure() while gamma is enabled; each call latches
// the new points through the gamma update bit, so no disable/enable (and no
// linear frame in between) is needed.
esp_err_t loadGammaCurve(int contrast, uint8_t digital_step) {
  g_gamma_curve_contrast = contrast;
  // Digital gain after the sensor limits and the user brightness, which
  // acts at once instead of waiting for the auto exposure.
  const int brightness = currentImageSettings().brightness;
  g_gamma_curve_gain =
      digitalGainForStep(digital_step) * brightnessLinearGain(brightness, kGammaExponent);
  g_gamma_curve_brightness = static_cast<int8_t>(brightness);
  isp_gamma_curve_points_t curve = {};
  esp_err_t err = esp_isp_gamma_fill_curve_points(gammaCurve, &curve);
  if (err == ESP_OK) err = esp_isp_gamma_configure(g_pipe.isp, COLOR_COMPONENT_R, &curve);
  if (err == ESP_OK) err = esp_isp_gamma_configure(g_pipe.isp, COLOR_COMPONENT_G, &curve);
  if (err == ESP_OK) err = esp_isp_gamma_configure(g_pipe.isp, COLOR_COMPONENT_B, &curve);
  if (err == ESP_OK) {
    g_pipe.gamma_contrast = static_cast<int8_t>(contrast);
    g_pipe.gamma_digital_step = digital_step;
  }
  return err;
}

// Worker: pushes changed user image settings to the running ISP before the
// next frame. Red, blue and saturation live in the CCM; contrast reloads the
// gamma curve only when it changed; brightness is read by every AE step.
void applyImageSettingsIfChanged() {
  if (!g_pipe.isp || !g_pipe.ready) return;
  const uint32_t generation = g_image_generation.load();
  if (generation == g_pipe.image_generation) return;
  // Recorded first: a failed gamma load is not retried on every frame; the
  // next change tries again.
  g_pipe.image_generation = generation;
  const ImageSettings image = currentImageSettings();
  applyColorCorrection();
  if (image.contrast == g_pipe.gamma_contrast &&
      image.brightness == g_gamma_curve_brightness) {
    return;
  }
  const esp_err_t err = loadGammaCurve(image.contrast, g_pipe.gamma_digital_step);
  if (err != ESP_OK) logCaptureError("Gamma update failed", err);
}

// Writes a new exposure only when it differs. At the exposure limits (dark
// scene, or brightness above the reachable target) every AE step repeats the
// same values; each write is six transfers on the SCCB bus shared with the
// touch controller.
bool setExposureIfChanged(const ExposureSetting& next) {
  if (next.lines == g_exposure.lines && next.gain_x16 == g_exposure.gain_x16) return false;
  g_exposure = next;
  g_sensor.setExposure(g_exposure.lines, g_exposure.gain_x16);
  return true;
}

// Max. gain setting (Web Admin) on top of the board's gain range.
GainLimits currentGainLimits() {
  return gainLimitsFor(currentImageSettings().gain, kMode.min_gain_x16, kMode.max_gain_x16,
                       kMode.max_total_gain_x16);
}

// Applies the Max. gain setting to the AE stages. Runs before every AE step,
// so a change takes effect within a running stream; a sensor gain above the
// new limit drops at once, the digital gain with the next digital step.
// Worker task only.
void applyGainLimit(ExposureStages& stages) {
  const GainLimits limits = currentGainLimits();
  stages.normal.max_gain_x16 = limits.sensor_gain_x16;
  stages.max_total_gain_x16 = limits.sensor_total_gain_x16;
  g_max_digital_step = limits.max_digital_step;
  if (g_exposure.gain_x16 > limits.sensor_total_gain_x16) {
    g_exposure.gain_x16 = limits.sensor_total_gain_x16;
    if (g_sensor_ready) g_sensor.setExposure(g_exposure.lines, g_exposure.gain_x16);
  }
}

void publishExposure(uint32_t mean_luma) {
  g_report_lines.store(g_exposure.lines);
  g_report_gain_x16.store(g_exposure.gain_x16);
  g_report_digital_x100.store(
      static_cast<uint16_t>(digitalGainForStep(g_pipe.gamma_digital_step) * 100.0f + 0.5f));
  g_report_luma.store(static_cast<uint16_t>(mean_luma > 255 ? 255 : mean_luma));
}

// Digital gain after the sensor limits (nextDigitalGainStep). Returns true
// when the curve changed; the caller then skips its sensor step, because the
// statistics show the new curve only on a later frame.
bool stepDigitalGain(uint32_t mean_luma, bool sensor_at_brighter_limit) {
  if (!g_pipe.ae_after_gamma) return false;
  const uint8_t current = g_pipe.gamma_digital_step;
  const uint32_t target = aeTarget();
  const uint8_t next = nextDigitalGainStep(current, mean_luma, target, 12, kGammaExponent,
                                           sensor_at_brighter_limit, g_max_digital_step);
  if (next == current) return false;
  const esp_err_t err = loadGammaCurve(g_pipe.gamma_contrast, next);
  if (err != ESP_OK) {
    logCaptureError("Digital gain update failed", err);
    return false;
  }
  g_digital_step = next;
  publishExposure(mean_luma);
  if (logDue(&g_digital_log_ms, kDigitalGainLogIntervalMs)) {
    Serial.printf("[LocalCam] Digital gain %.2fx (luma %u, target %u, exp %u, gain %u/16)\n",
                  static_cast<double>(digitalGainForStep(next)), static_cast<unsigned>(mean_luma),
                  static_cast<unsigned>(target), static_cast<unsigned>(g_exposure.lines),
                  static_cast<unsigned>(g_exposure.gain_x16));
  }
  return true;
}

// Sensor-side orientation: the display rotation and the mirror setting become
// sensor readout flips, so no pixel pass (PPA or CPU) ever turns the image.
// A quarter-turn mounting maps the mirror to the other sensor flip; the
// Bridge turns the JPEG afterwards.
// live: the sensor streams; the frames still in flight are dropped. Returns
// false when the sensor did not confirm the registers.
bool applyOrientation(bool live) {
  const SensorOrientation wanted =
      desiredOrientation(imageRotated180(), g_mirror.load(), kQuarterTurn);
  const uint8_t code = orientationCode(wanted);
  if (code == g_applied_orientation) return true;
  esp_err_t err = g_sensor.setOrientation(wanted.mirror, wanted.flip);
  if (err != ESP_OK) {
    // The bus is shared with touch: one retry, as for standby.
    vTaskDelay(pdMS_TO_TICKS(kSensorRetryDelayMs));
    err = g_sensor.setOrientation(wanted.mirror, wanted.flip);
  }
  if (err != ESP_OK) {
    g_applied_orientation = kOrientationUnknown;
    rememberError("Sensor orientation", err);
    if (logDue(&g_orientation_log_ms, kErrorLogIntervalMs)) {
      Serial.printf("[LocalCam] Sensor orientation mirror=%u flip=%u failed: %s (0x%x)\n",
                    wanted.mirror ? 1u : 0u, wanted.flip ? 1u : 0u, esp_err_to_name(err),
                    static_cast<unsigned>(err));
    }
    return false;
  }
  g_applied_orientation = code;
  if (logDue(&g_orientation_log_ms, kErrorLogIntervalMs)) {
    Serial.printf("[LocalCam] Sensor orientation mirror=%u flip=%u applied (%s)\n",
                  wanted.mirror ? 1u : 0u, wanted.flip ? 1u : 0u, live ? "live" : "standby");
  }
  if (live) {
    xQueueReset(g_isr.frames);
    FrameEvent event{};
    for (uint8_t i = 0; i < kOrientationSettleFrames; ++i) {
      xQueueReceive(g_isr.frames, &event, pdMS_TO_TICKS(2 * currentFrameMs()));
    }
  }
  return true;
}

void releasePipeline() {
  if (g_pipe.csi && g_pipe.csi_running) {
    esp_cam_ctlr_stop(g_pipe.csi);
    g_pipe.csi_running = false;
  }
  if (g_pipe.ae) {
    esp_isp_ae_controller_disable(g_pipe.ae);
    esp_isp_del_ae_controller(g_pipe.ae);
    g_pipe.ae = nullptr;
  }
  if (g_pipe.awb) {
    esp_isp_awb_controller_disable(g_pipe.awb);
    esp_isp_del_awb_controller(g_pipe.awb);
    g_pipe.awb = nullptr;
  }
  if (g_pipe.isp) {
    if (g_pipe.isp_enabled) esp_isp_disable(g_pipe.isp);
    esp_isp_del_processor(g_pipe.isp);
    g_pipe.isp = nullptr;
    g_pipe.isp_enabled = false;
  }
  if (g_pipe.csi) {
    if (g_pipe.csi_enabled) esp_cam_ctlr_disable(g_pipe.csi);
    esp_cam_ctlr_del(g_pipe.csi);
    g_pipe.csi = nullptr;
    g_pipe.csi_enabled = false;
  }
  if (g_pipe.ppa && !g_pipe.ppa_wedged) {
    ppa_unregister_client(g_pipe.ppa);
    g_pipe.ppa = nullptr;
  }
  for (uint8_t*& buffer : g_isr.buffers) {
    if (buffer && !g_pipe.ppa_wedged) heap_caps_free(buffer);
    buffer = nullptr;
  }
  g_isr.buffer_bytes = 0;
  if (g_pipe.jpeg) {
    jpeg_del_encoder_engine(g_pipe.jpeg);
    g_pipe.jpeg = nullptr;
  }
  if (g_pipe.jpeg_out) {
    heap_caps_free(g_pipe.jpeg_out);
    g_pipe.jpeg_out = nullptr;
    g_pipe.jpeg_capacity = 0;
  }
  g_pipe.ready = false;
  g_pipe.started = false;
}

// A stop can leave the CSI receiver and the ISP inside a frame, and the next
// start continued from that state (8-inch hardware 2026-09-24: red and blue
// swapped or no frames after a stream restart, correct again after the
// rebuild). Every capture and stream therefore starts from a pipeline that
// has not run yet, like the first start after boot; the sensor configuration
// stays. Worker task only, after the previous run has released its buffers.
void releaseUsedPipeline() {
  if (g_pipe.started) releasePipeline();
}

void releaseSensor() {
  // Only an identified sensor receives the standby sequence; after a failed
  // probe the device is just removed from the bus again.
  if (g_sensor_identified) sensorStandby();
  g_sensor_identified = false;
  if (g_sensor.attached()) {
    const esp_err_t err = g_sensor.detach();
    if (err != ESP_OK && logDue(&g_sensor_log_ms, kErrorLogIntervalMs)) {
      Serial.printf("[LocalCam] SCCB device remove failed: %s; retried on next release\n",
                    esp_err_to_name(err));
    }
  }
  g_sensor_ready = false;
  g_applied_orientation = kOrientationUnknown;
  if (g_hw_acquired) {
    board::release();
    g_sccb_bus = board::SccbBus{};
    g_hw_acquired = false;
  }
}

void releaseAll() {
  releasePipeline();
  releaseSensor();
}

// Full re-initialisation after missing frames: pipeline (CSI, ISP, JPEG,
// buffers) and sensor (standby, detach, board release). The next capture
// or stream starts with a software reset of the sensor. Worker task only.
void resetAfterNoFrames(const char* where) {
  static uint32_t log_ms = 0;
  if (logDue(&log_ms, kErrorLogIntervalMs)) {
    Serial.printf("[LocalCam] No frames (%s): rebuilding sensor and pipeline\n", where);
  }
  releaseAll();
}

// Settles the final state against a concurrent disable from the loop task.
void publishWorkerState(ServiceState state, Detail detail) {
  if (!g_enabled.load()) {
    setState(ServiceState::Disabled, Detail::None);
    return;
  }
  setState(state, detail);
}

bool ensureSensor(bool report_state) {
  if (g_sensor_ready) return true;
  const uint32_t started_ms = millis();
  if (!g_hw_acquired) {
    const BoardError board_error = board::acquire(&g_sccb_bus);
    if (board_error != BoardError::None) {
      const Detail detail = board_error == BoardError::BusUnavailable
                                ? Detail::BusUnavailable
                                : Detail::PhySupplyFailed;
      Serial.printf("[LocalCam] Hardware unavailable: %s\n", detailName(detail));
      if (report_state) publishWorkerState(ServiceState::Error, detail);
      return false;
    }
    g_hw_acquired = true;
  }
  esp_err_t err = g_sensor.attach(g_sccb_bus);
  if (err != ESP_OK) {
    Serial.printf("[LocalCam] SCCB device add failed: %s\n", esp_err_to_name(err));
    releaseSensor();
    if (report_state) publishWorkerState(ServiceState::Error, Detail::BusUnavailable);
    return false;
  }
  uint16_t chip_id = 0;
  for (uint8_t attempt = 0; attempt < kProbeAttempts; ++attempt) {
    if (attempt != 0) vTaskDelay(pdMS_TO_TICKS(kProbeRetryDelayMs));
    err = g_sensor.probe(&chip_id);
    // A bus-lock/SCL timeout, or a NACK during the ID read after the address
    // was acknowledged (INVALID_STATE), is transient on the shared bus. An
    // address NACK or a chip-ID mismatch (NOT_FOUND) is final.
    if (err != ESP_ERR_TIMEOUT && err != ESP_ERR_INVALID_STATE) break;
  }
  g_chip_id.store(chip_id);
  if (err != ESP_OK) {
    const bool absent = err == ESP_ERR_NOT_FOUND;
    Serial.printf("[LocalCam] Sensor %s %s at SCCB 0x%02x: %s, chip id 0x%04x\n",
                  kMode.name, absent ? "not detected" : "probe failed",
                  static_cast<unsigned>(kMode.sccb_address), esp_err_to_name(err),
                  static_cast<unsigned>(chip_id));
    releaseSensor();
    if (report_state) {
      if (absent) {
        publishWorkerState(ServiceState::NotFound, Detail::SensorNotFound);
      } else {
        publishWorkerState(ServiceState::Error, Detail::BusUnavailable);
      }
    }
    return false;
  }
  g_sensor_identified = true;
  err = g_sensor.loadDefaultMode(kMode.mirror);
  if (err != ESP_OK) {
    Serial.printf("[LocalCam] Sensor %s mode setup failed: %s\n", kMode.name,
                  esp_err_to_name(err));
    releaseSensor();
    if (report_state) publishWorkerState(ServiceState::Error, Detail::SensorInitFailed);
    return false;
  }
  g_exposure.lines = kMode.default_exposure_lines;
  g_exposure.gain_x16 = kMode.default_gain_x16;
  // The mode table reads out in orientation state 0.
  g_applied_orientation = 0;
  g_sensor_ready = true;
  Serial.printf(
      "[LocalCam] Sensor %s detected (chip id 0x%04x), %ux%u RAW%u %u-lane "
      "mode loaded in %u ms, sensor in standby\n",
      kMode.name, static_cast<unsigned>(chip_id),
      static_cast<unsigned>(kMode.frame_width), static_cast<unsigned>(kMode.frame_height),
      static_cast<unsigned>(kMode.raw_bits), static_cast<unsigned>(kMode.data_lanes),
      static_cast<unsigned>(millis() - started_ms));
  if (report_state) publishWorkerState(ServiceState::Ready, Detail::None);
  return true;
}

bool createAutoExposure(isp_ae_sample_point_t sample_point, uint32_t target) {
  esp_isp_ae_config_t config = {};
  config.sample_point = sample_point;
  // The whole frame: 5x5 blocks of exactly (width / 5) x (height / 5).
  config.window.top_left.x = kStatsLeft;
  config.window.top_left.y = kStatsTop;
  config.window.btm_right.x = kStatsLeft + kStatsWidth;
  config.window.btm_right.y = kStatsTop + kStatsHeight;
  if (esp_isp_new_ae_controller(g_pipe.isp, &config, &g_pipe.ae) != ESP_OK) {
    g_pipe.ae = nullptr;
    return false;
  }
  if (esp_isp_ae_controller_enable(g_pipe.ae) != ESP_OK) {
    esp_isp_del_ae_controller(g_pipe.ae);
    g_pipe.ae = nullptr;
    return false;
  }
  g_pipe.ae_target = target;
  g_pipe.ae_after_gamma = sample_point == ISP_AE_SAMPLE_POINT_AFTER_GAMMA;
  return true;
}

bool createJpegEncoder() {
  jpeg_encode_engine_cfg_t engine = {};
  engine.intr_priority = 0;
  engine.timeout_ms = 200;
  if (jpeg_new_encoder_engine(&engine, &g_pipe.jpeg) != ESP_OK) {
    g_pipe.jpeg = nullptr;
    return false;
  }
  return true;
}

bool ensurePipeline() {
  if (g_pipe.ready) {
    // After an encode failure only the engine was dropped; the buffers stay
    // allocated until the idle release in case the DMA still touched them.
    if (g_pipe.jpeg || createJpegEncoder()) return true;
    if (pipelineErrorLogDue()) {
      Serial.println("[LocalCam] JPEG encoder could not be re-created");
    }
    return false;
  }
  const uint32_t started_ms = millis();
  esp_err_t err = ESP_OK;
  const char* step = "";

  if (!g_isr.frames) {
    g_isr.frames = xQueueCreate(1, sizeof(FrameEvent));
    if (!g_isr.frames) {
      if (pipelineErrorLogDue()) {
        Serial.println("[LocalCam] Frame queue allocation failed");
      }
      return false;
    }
  }

  do {
    // CSI controller first: the ISP shares its bridge.
    step = "CSI controller";
    esp_cam_ctlr_csi_config_t csi_config = {};
    csi_config.ctlr_id = 0;
    csi_config.clk_src = MIPI_CSI_PHY_CLK_SRC_DEFAULT;
    csi_config.h_res = kMode.frame_width;
    csi_config.v_res = kMode.frame_height;
    csi_config.data_lane_num = kMode.data_lanes;
    csi_config.lane_bit_rate_mbps = kMode.lane_bit_rate_mbps;
    csi_config.input_data_color_type = kRaw8 ? CAM_CTLR_COLOR_RAW8 : CAM_CTLR_COLOR_RAW10;
    csi_config.output_data_color_type = CAM_CTLR_COLOR_RGB565;
    csi_config.queue_items = 1;
    csi_config.byte_swap_en = 0;
    csi_config.bk_buffer_dis = 1;
    err = esp_cam_new_csi_ctlr(&csi_config, &g_pipe.csi);
    if (err != ESP_OK) {
      g_pipe.csi = nullptr;
      break;
    }

    step = "frame buffers";
    for (uint8_t*& buffer : g_isr.buffers) {
      // esp_cam_ctlr_start() invalidates the buffer with an aligned-only
      // cache sync; esp_cam_ctlr_alloc_buffer() returned a 16-byte aligned
      // PSRAM block on the V2 (0x494eb050), which the sync rejects. Align to
      // the largest P4 cache line (128 B; kFrameBytes is a multiple of it).
      static_assert(kFrameBytes % kFrameBufferAlign == 0, "frame size must fill cache lines");
      buffer = static_cast<uint8_t*>(
          heap_caps_aligned_calloc(kFrameBufferAlign, 1, kFrameBytes, MALLOC_CAP_SPIRAM));
      if (!buffer) {
        err = ESP_ERR_NO_MEM;
        break;
      }
      // Write back the zeroed lines so later evictions cannot overwrite DMA data.
      esp_cache_msync(buffer, kFrameBytes,
                      ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
    if (err != ESP_OK) break;
    g_isr.buffer_bytes = kFrameBytes;

    step = "CSI callbacks";
    esp_cam_ctlr_evt_cbs_t callbacks = {};
    callbacks.on_get_new_trans = onGetNewTransaction;
    callbacks.on_trans_finished = onTransactionFinished;
    err = esp_cam_ctlr_register_event_callbacks(g_pipe.csi, &callbacks, nullptr);
    if (err != ESP_OK) break;
    step = "CSI enable";
    err = esp_cam_ctlr_enable(g_pipe.csi);
    if (err != ESP_OK) break;
    g_pipe.csi_enabled = true;

    step = "ISP processor";
    esp_isp_processor_cfg_t isp_config = {};
    isp_config.clk_src = ISP_CLK_SRC_DEFAULT;
    isp_config.clk_hz = kIspClockHz;
    isp_config.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
    isp_config.input_data_color_type = kRaw8 ? ISP_COLOR_RAW8 : ISP_COLOR_RAW10;
    isp_config.output_data_color_type = ISP_COLOR_RGB565;
    isp_config.yuv_range = ISP_COLOR_RANGE_FULL;
    isp_config.yuv_std = ISP_YUV_CONV_STD_BT601;
    isp_config.has_line_start_packet = kMode.line_sync_packets;
    isp_config.has_line_end_packet = kMode.line_sync_packets;
    isp_config.h_res = kMode.frame_width;
    isp_config.v_res = kMode.frame_height;
    isp_config.bayer_order = kMode.bayer_order;
    err = esp_isp_new_processor(&isp_config, &g_pipe.isp);
    if (err != ESP_OK) {
      g_pipe.isp = nullptr;
      break;
    }
    step = "ISP enable";
    err = esp_isp_enable(g_pipe.isp);
    if (err != ESP_OK) break;
    g_pipe.isp_enabled = true;

    step = "demosaic";
    esp_isp_demosaic_config_t demosaic = {};
    demosaic.grad_ratio.integer = 1;  // 1.5, IPA tuning at gain 1x
    demosaic.grad_ratio.decimal = 8;
    demosaic.padding_mode = ISP_DEMOSAIC_EDGE_PADDING_MODE_SRND_DATA;
    err = esp_isp_demosaic_configure(g_pipe.isp, &demosaic);
    if (err == ESP_OK) err = esp_isp_demosaic_enable(g_pipe.isp);
    if (err != ESP_OK) break;

    // Generation first: a change after this point is applied by the next
    // applyImageSettingsIfChanged().
    g_pipe.image_generation = g_image_generation.load();
    const ImageSettings image = currentImageSettings();

    step = "colour correction";
    esp_isp_ccm_config_t ccm = {};
    buildImageCcm(kBaseCcm, g_gains, image, ccm.matrix);
    ccm.saturation = true;
    ccm.flags.update_once_configured = 1;
    err = esp_isp_ccm_configure(g_pipe.isp, &ccm);
    if (err == ESP_OK) err = esp_isp_ccm_enable(g_pipe.isp);
    if (err != ESP_OK) break;

    step = "gamma";
    if (g_digital_step > currentGainLimits().max_digital_step) {
      g_digital_step = currentGainLimits().max_digital_step;
    }
    err = loadGammaCurve(image.contrast, g_digital_step);
    if (err == ESP_OK) err = esp_isp_gamma_enable(g_pipe.isp);
    if (err != ESP_OK) break;

    step = "auto-exposure statistics";
    // Prefer the gamma-corrected domain; the linear fallback uses the same
    // target mapped through the gamma curve.
    if (!createAutoExposure(ISP_AE_SAMPLE_POINT_AFTER_GAMMA, 115) &&
        !createAutoExposure(ISP_AE_SAMPLE_POINT_AFTER_DEMOSAIC, 80)) {
      err = ESP_FAIL;
      break;
    }
    if (!g_pipe.ae_after_gamma && g_pipe.gamma_digital_step != 0) {
      // Linear statistics cannot see the digital gain: run without it.
      g_digital_step = 0;
      err = loadGammaCurve(image.contrast, 0);
      if (err != ESP_OK) break;
    }

    step = "white-balance statistics";
    esp_isp_awb_config_t awb = {};
    awb.sample_point = ISP_AWB_SAMPLE_POINT_BEFORE_CCM;
    awb.window.top_left.x = kStatsLeft;
    awb.window.top_left.y = kStatsTop;
    awb.window.btm_right.x = kStatsLeft + kStatsWidth - 1;
    awb.window.btm_right.y = kStatsTop + kStatsHeight - 1;
    awb.subwindow = awb.window;
    awb.white_patch.luminance.min = 30;
    awb.white_patch.luminance.max = 600;
    awb.white_patch.red_green_ratio.min = 0.2f;
    awb.white_patch.red_green_ratio.max = 3.9f;
    awb.white_patch.blue_green_ratio.min = 0.2f;
    awb.white_patch.blue_green_ratio.max = 3.9f;
    err = esp_isp_new_awb_controller(g_pipe.isp, &awb, &g_pipe.awb);
    if (err != ESP_OK) {
      g_pipe.awb = nullptr;
      break;
    }
    err = esp_isp_awb_controller_enable(g_pipe.awb);
    if (err != ESP_OK) break;

    step = "JPEG encoder";
    if (!createJpegEncoder()) {
      err = ESP_FAIL;
      break;
    }
    step = "JPEG output buffer";
    jpeg_encode_memory_alloc_cfg_t output = {};
    output.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER;
    g_pipe.jpeg_out = static_cast<uint8_t*>(
        jpeg_alloc_encoder_mem(kJpegOutputCapacity, &output, &g_pipe.jpeg_capacity));
    if (!g_pipe.jpeg_out || g_pipe.jpeg_capacity < kPanelMaxJpegBytes) {
      err = ESP_ERR_NO_MEM;
      break;
    }

  } while (false);

  if (err != ESP_OK) {
    rememberError(step, err);
    if (pipelineErrorLogDue()) {
      Serial.printf("[LocalCam] Pipeline setup failed at %s: %s (0x%x), PSRAM free %u KB\n",
                    step, esp_err_to_name(err), static_cast<unsigned>(err),
                    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    }
    releasePipeline();
    return false;
  }
  g_pipe.ready = true;
  if (!logDue(&g_pipeline_log_ms, 600000)) return true;
  Serial.printf(
      "[LocalCam] Pipeline ready in %u ms: CSI %ux%u RAW%u %u lane @ %u Mbps, "
      "ISP RGB565, AE target %u, PSRAM free %u KB, internal free %u KB\n",
      static_cast<unsigned>(millis() - started_ms),
      static_cast<unsigned>(kMode.frame_width),
      static_cast<unsigned>(kMode.frame_height),
      static_cast<unsigned>(kMode.raw_bits),
      static_cast<unsigned>(kMode.data_lanes),
      static_cast<unsigned>(kMode.lane_bit_rate_mbps),
      static_cast<unsigned>(aeTarget()),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  return true;
}

// Sensor standby first, then the receiver, in the order of esp_video and the
// Tasmota CSI driver. Returns false when the sensor could not be put into
// standby; it may still be streaming, so the state becomes Error and the next
// probe re-initializes it with a software reset.
bool stopStreaming() {
  const bool standby = sensorStandby() == ESP_OK;
  if (g_pipe.csi && g_pipe.csi_running) {
    // Let the frame in flight end before the receiver stops.
    vTaskDelay(pdMS_TO_TICKS(currentFrameMs() + 2));
    esp_cam_ctlr_stop(g_pipe.csi);
    g_pipe.csi_running = false;
  }
  if (!standby) {
    g_sensor_ready = false;
    publishWorkerState(ServiceState::Error, Detail::SensorInitFailed);
  }
  return standby;
}

struct CaptureStats {
  uint32_t ae_iterations = 0;
  uint32_t mean_luma = 0;
  uint8_t quality = 0;
  uint32_t capture_ms = 0;
  uint32_t encode_ms = 0;
  uint32_t first_frame_bytes = 0;
};

ErrorCode captureJpeg(uint32_t max_bytes, size_t* jpeg_bytes, CaptureStats* stats,
                      Detail* detail) {
  *jpeg_bytes = 0;
  *detail = Detail::None;
  // Re-opens the sensor lazily after an OTA/restart release; a failure also
  // updates the published state so the Bridge capability follows reality.
  if (!ensureSensor(true)) {
    *detail = static_cast<Detail>(g_detail.load());
    return ErrorCode::SensorUnavailable;
  }
  releaseUsedPipeline();
  if (!ensurePipeline()) {
    *detail = Detail::PipelineFailed;
    return ErrorCode::SensorUnavailable;
  }

  if (!applyOrientation(false)) {
    *detail = Detail::SensorInitFailed;
    return ErrorCode::SensorUnavailable;
  }
  const uint32_t started_ms = millis();
  g_isr.inflight = -1;
  g_isr.frozen = -1;
  g_isr.armed = false;
  xQueueReset(g_isr.frames);
  applyImageSettingsIfChanged();
  applyColorCorrection();
  g_sensor.setExposure(g_exposure.lines, g_exposure.gain_x16);

  esp_err_t err = esp_cam_ctlr_start(g_pipe.csi);
  if (err != ESP_OK) {
    logCaptureError("CSI start failed", err);
    *detail = Detail::PipelineFailed;
    return ErrorCode::SensorUnavailable;
  }
  g_pipe.csi_running = true;
  g_pipe.started = true;
  // No indicator for single still images: Home Assistant refreshes camera
  // thumbnails every few seconds, and the pill would pop up each time. It
  // shows while a live stream runs (someone is watching).
  err = g_sensor.setStream(true);
  if (err != ESP_OK) {
    logCaptureError("Sensor stream-on failed", err);
    stopStreaming();
    *detail = Detail::SensorInitFailed;
    return ErrorCode::SensorUnavailable;
  }

  FrameEvent event{};
  if (xQueueReceive(g_isr.frames, &event, pdMS_TO_TICKS(kFirstFrameTimeoutMs)) != pdTRUE) {
    stopStreaming();
    if (logDue(&g_error_log_ms, 10000)) {
      Serial.printf(
          "[LocalCam] No CSI frame within %u ms (%u lane @ %u Mbps, line sync on); "
          "check lane count and bit rate\n",
          static_cast<unsigned>(kFirstFrameTimeoutMs),
          static_cast<unsigned>(kMode.data_lanes),
          static_cast<unsigned>(kMode.lane_bit_rate_mbps));
    }
    resetAfterNoFrames("snapshot");
    *detail = Detail::NoFrames;
    return ErrorCode::SensorUnavailable;
  }
  stats->first_frame_bytes = event.received;

  // Auto exposure and gray-world white balance from ISP statistics.
  static isp_ae_result_t ae_result;
  static isp_awb_stat_result_t awb_result;
  ExposureStages stages;
  stages.normal = ExposureLimits{kMode.min_exposure_lines, kMode.max_exposure_lines,
                                 kMode.min_gain_x16, kMode.max_gain_x16};
  stages.night_max_lines = kMode.max_exposure_lines;
  stages.max_total_gain_x16 = kMode.max_total_gain_x16;
  applyGainLimit(stages);
  bool exposure_done = false;
  bool balance_done = false;
  AutoTuneReport report{};
  for (uint8_t iteration = 0; iteration < kAutoExposureMaxIterations; ++iteration) {
    if (abortRequested()) {
      stopStreaming();
      return abortCode();
    }
    if (static_cast<uint32_t>(millis() - started_ms) >= kAutoExposureBudgetMs) break;
    stats->ae_iterations = iteration + 1u;
    if (esp_isp_ae_controller_get_oneshot_statistics(
            g_pipe.ae, kStatisticsTimeoutMs, &ae_result) != ESP_OK) {
      continue;
    }
    ++report.ae_samples;
    if (esp_isp_awb_controller_get_oneshot_statistics(
            g_pipe.awb, kStatisticsTimeoutMs, &awb_result) == ESP_OK) {
      ++report.awb_samples;
      report.white_patches = awb_result.white_patch_num;
      const WhiteBalanceGains next = grayWorldGains(
          awb_result.sum_r, awb_result.sum_g, awb_result.sum_b,
          awb_result.white_patch_num, kMinAwbSamples, g_gains);
      balance_done = gainsSettled(next, g_gains);
      g_gains = next;
      applyColorCorrection();
    }
    stats->mean_luma = weightedMeanLuma(ae_result.luminance);
    const ExposureStep step = stepStagedExposure(
        g_exposure, stats->mean_luma, aeTarget(), 12, stages);
    publishExposure(stats->mean_luma);
    if (stepDigitalGain(stats->mean_luma, step.limited)) {
      // The new curve shows in the statistics of a later frame.
      exposure_done = false;
      vTaskDelay(pdMS_TO_TICKS(currentFrameMs() * 2));
      continue;
    }
    exposure_done = step.converged;
    report.limited = step.limited;
    if (exposure_done && balance_done) break;
    if (!step.converged && setExposureIfChanged(step.next)) {
      // New exposure registers take effect on a later frame.
      vTaskDelay(pdMS_TO_TICKS(currentFrameMs() * 2));
    }
  }

  report.steps = stats->ae_iterations;
  report.luma = stats->mean_luma;
  report.target = aeTarget();
  report.lines = g_exposure.lines;
  report.gain_x16 = g_exposure.gain_x16;
  report.digital_x100 =
      static_cast<uint16_t>(digitalGainForStep(g_pipe.gamma_digital_step) * 100.0f + 0.5f);
  report.wb_red_x100 = static_cast<uint16_t>(g_gains.red * 100.0f + 0.5f);
  report.wb_blue_x100 = static_cast<uint16_t>(g_gains.blue * 100.0f + 0.5f);
  rememberAutoTune(report);

  // A disable or shutdown during the last AE step: never freeze a new frame.
  if (abortRequested()) {
    stopStreaming();
    return abortCode();
  }

  // Keep the next complete frame; the ISR then never writes that buffer again.
  xQueueReset(g_isr.frames);
  g_isr.armed = true;
  const uint32_t freeze_started_ms = millis();
  const uint32_t freeze_timeout_ms = std::max(kFreezeTimeoutMs, 3 * currentFrameMs());
  while (g_isr.frozen < 0 &&
         static_cast<uint32_t>(millis() - freeze_started_ms) < freeze_timeout_ms) {
    xQueueReceive(g_isr.frames, &event, pdMS_TO_TICKS(currentFrameMs() * 2));
  }
  g_isr.armed = false;
  const bool standby = stopStreaming();
  const int8_t frozen = g_isr.frozen;
  stats->capture_ms = millis() - started_ms;
  if (abortRequested()) return abortCode();
  if (!standby) {
    *detail = Detail::SensorInitFailed;
    return ErrorCode::SensorUnavailable;
  }
  if (frozen < 0) {
    if (logDue(&g_error_log_ms, 10000)) {
      Serial.printf(
          "[LocalCam] No complete frame to keep: last transfer %u of %u bytes\n",
          static_cast<unsigned>(event.received),
          static_cast<unsigned>(kFrameBytes));
    }
    *detail = Detail::NoFrames;
    return ErrorCode::SensorUnavailable;
  }

  // The sensor delivered the JPEG size in the wanted orientation: the frozen
  // CSI buffer is the JPEG input as it is. The CPU never touches it.
  const uint8_t* frame = g_isr.buffers[frozen];

  const uint32_t encode_started_ms = millis();
  for (uint8_t quality : kJpegQualities) {
    if (abortRequested()) return abortCode();
    uint32_t size = 0;
    {
      // JPEG shares the 2D-DMA pool with display PPA rotation and the HA
      // camera decoder; never overlap them.
      Dma2dArbiterGuard guard(500);
      if (!guard.locked()) {
        *detail = Detail::EncoderBusy;
        return ErrorCode::EncoderBusy;
      }
      jpeg_encode_cfg_t config = {};
      config.width = kImageWidth;
      config.height = kImageHeight;
      config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
      config.sub_sample = kJpegSubsampling;
      config.image_quality = quality;
      err = jpeg_encoder_process(g_pipe.jpeg, &config, frame, kJpegInputBytes,
                                 g_pipe.jpeg_out,
                                 static_cast<uint32_t>(g_pipe.jpeg_capacity), &size);
      if (err != ESP_OK) {
        // Drop a possibly wedged engine while the arbiter is still held.
        jpeg_del_encoder_engine(g_pipe.jpeg);
        g_pipe.jpeg = nullptr;
      }
    }
    if (err != ESP_OK) {
      logCaptureError("JPEG encode failed", err);
      *detail = Detail::EncoderBusy;
      return ErrorCode::EncoderBusy;
    }
    stats->quality = quality;
    if (size > 0 && size <= max_bytes && size <= g_pipe.jpeg_capacity) {
      stats->encode_ms = millis() - encode_started_ms;
      if (!looksLikeCompleteJpeg(g_pipe.jpeg_out, size)) {
        *detail = Detail::EncoderBusy;
        return ErrorCode::EncoderBusy;
      }
      *jpeg_bytes = size;
      return ErrorCode::None;
    }
    vTaskDelay(1);  // Let a waiting PPA transaction run between attempts.
  }
  stats->encode_ms = millis() - encode_started_ms;
  *detail = Detail::TooLarge;
  return ErrorCode::TooLarge;
}

void runCapture() {
  SnapshotRequest request = g_pending;
  size_t jpeg_bytes = 0;
  CaptureStats stats;
  Detail detail = Detail::None;
  ErrorCode code = ErrorCode::Disabled;
  if (g_enabled.load()) {
    code = captureJpeg(request.max_bytes, &jpeg_bytes, &stats, &detail);
  }
  g_last_used_ms = millis();
  // A frame captured before a disable/shutdown is never published.
  if (code == ErrorCode::None && abortRequested()) code = abortCode();

  // The Bridge drops an image that arrives after its request timeout, so an
  // upload that has not started by the deadline is skipped (no reply either:
  // the Bridge no longer waits for this id).
  const uint32_t deadline_ms = request.received_ms + kRequestDeadlineMs;
  auto deadlinePassed = [deadline_ms]() {
    return static_cast<int32_t>(millis() - deadline_ms) >= 0;
  };
  if (code == ErrorCode::None) {
    using StreamResult = HomeTilesNetworkManager::StreamPublishResult;
    const String topic = deviceTopic(kImageLeaf, request.id);
    bool sent = false;
    bool late = deadlinePassed();
    bool aborted = false;
    if (!late && networkManager.mqttStreamPublishSubmit(
                     topic.c_str(), g_pipe.jpeg_out, jpeg_bytes, deadline_ms)) {
      for (;;) {
        // Short slices without withdrawing, so a disable, shutdown or the
        // deadline can still cancel a stream that has not started. A stream
        // the worker is already writing cannot be cancelled; wait for it.
        const StreamResult result =
            networkManager.mqttStreamPublishWait(kStreamPollMs, false);
        if (result != StreamResult::Pending) {
          sent = result == StreamResult::Sent;
          late = !sent && deadlinePassed();
          break;
        }
        const bool abort = abortRequested();
        if ((abort || deadlinePassed()) && networkManager.mqttStreamPublishCancel()) {
          aborted = abort;
          late = !abort;
          break;
        }
      }
    }
    if (aborted) {
      g_capture_failed.fetch_add(1);
      publishErrorReply(request.id, abortCode());
    } else if (sent) {
      const uint32_t ok = g_capture_ok.fetch_add(1) + 1;
      g_last_jpeg_bytes.store(static_cast<uint32_t>(jpeg_bytes));
      g_last_capture_ms.store(stats.capture_ms + stats.encode_ms);
      if (ok == 1 || logDue(&g_capture_log_ms, 60000)) {
        Serial.printf(
            "[LocalCam] Snapshot %u bytes q%u: capture %u ms (%u AE steps, "
            "luma %u, exp %u, gain %u/16), encode %u ms, first frame %u bytes "
            "(ok=%u failed=%u)\n",
            static_cast<unsigned>(jpeg_bytes), static_cast<unsigned>(stats.quality),
            static_cast<unsigned>(stats.capture_ms),
            static_cast<unsigned>(stats.ae_iterations),
            static_cast<unsigned>(stats.mean_luma),
            static_cast<unsigned>(g_exposure.lines),
            static_cast<unsigned>(g_exposure.gain_x16),
            static_cast<unsigned>(stats.encode_ms),
            static_cast<unsigned>(stats.first_frame_bytes),
            static_cast<unsigned>(ok),
            static_cast<unsigned>(g_capture_failed.load()));
      }
    } else {
      g_capture_failed.fetch_add(1);
      g_detail.store(static_cast<uint8_t>(Detail::PublishFailed));
      if (logDue(&g_upload_log_ms, kErrorLogIntervalMs)) {
        Serial.printf("[LocalCam] Snapshot upload %s (%u bytes, %u ms after request)\n",
                      late ? "skipped: request deadline passed" : "failed",
                      static_cast<unsigned>(jpeg_bytes),
                      static_cast<unsigned>(millis() - request.received_ms));
      }
    }
  } else {
    g_capture_failed.fetch_add(1);
    if (detail != Detail::None) g_detail.store(static_cast<uint8_t>(detail));
    publishErrorReply(request.id, code);
    if (logDue(&g_error_log_ms, 10000)) {
      Serial.printf("[LocalCam] Snapshot failed: %s (%s)\n", errorCodeName(code),
                    detailName(detail));
    }
  }
  g_capture_busy.store(false);
}


// ===========================================================================
// Live stream: the sensor streams continuously for the whole session; frames
// are frozen at paced deadlines, cropped (full size) or 2x2-averaged (half
// size), JPEG-encoded under the 2D-DMA arbiter and handed to the sender task
// (local_camera_upload.cpp) through two latest-frame-wins slots.
// ===========================================================================
constexpr uint32_t kStreamSettleBudgetMs = 1000;
constexpr int kStreamSettleStatisticsMs = 100;
constexpr uint32_t kStreamTuneIntervalMs = 250;
constexpr uint32_t kStreamAwbIntervalMs = 1000;
// At high frame rates the gap between deadlines is shorter than one
// statistics wait; one step is then forced at most this often.
constexpr uint32_t kStreamTuneForceMs = 2000;
constexpr int kStreamStatisticsTimeoutMs = 40;
constexpr int kStreamMinStatisticsWaitMs = 10;
constexpr uint32_t kStreamArbiterTimeoutMs = 20;
constexpr uint32_t kStreamSliceMs = 50;
constexpr uint32_t kSenderStopWaitMs = 1000;
constexpr uint32_t kStreamStartLogIntervalMs = 10000;

struct StreamRun {
  StreamSettings settings;
  uint8_t quality = 0;
  uint32_t mode_generation = 0;
  ExposureStages stages;
  local_camera_stream::FramePacer pacer;
  uint32_t last_tune_ms = 0;
  uint32_t last_awb_ms = 0;
  bool awb_next = false;
  uint32_t mean_luma = 0;
  StreamWindow window;
  uint32_t window_started_ms = 0;
  uint32_t encoded_total = 0;
  uint32_t big_log_ms = 0;
  // Adaptive JPEG quality: consecutive frames the upload could not take,
  // and frames sent in time since the last change.
  uint8_t busy_frames = 0;
  uint16_t calm_frames = 0;
  uint32_t quality_log_ms = 0;
  // Consecutive capture attempts without a complete CSI frame.
  uint16_t noframe_streak = 0;
};

// A sensor/receiver pair that stopped delivering frames stays stuck while
// its handles are kept: on the Waveshare 8-inch every retry after a quick
// stop/start saw no frame until the idle release rebuilt everything 30 s
// later. After this many attempts in a row (~1-2 s) the run ends and the
// whole camera path is rebuilt.
constexpr uint16_t kNoFrameResetStreak = 12;


StreamRun g_stream_run;  // Worker-owned.
// Adaptive quality: drop one step after this many frames the upload could not
// take in a row, raise one step after this many frames sent in time.
constexpr uint8_t kBusyFramesBeforeQualityDrop = 4;
constexpr uint16_t kCalmFramesBeforeQualityRise = 75;

int workerCore() { return (ARDUINO_RUNNING_CORE == 0) ? 1 : 0; }

// Resolves the stored mode (or Auto with the Bridge hints) and prepares the
// buffers and exposure limits for it.
bool applyStreamSettings(StreamRun& run) {
  run.mode_generation = g_stream_mode_generation.load();
  StreamSettings settings;
  if (!local_camera_stream::resolveSettings(g_stream_mode.load(), currentStreamHints(),
                                            kImageWidth, kImageHeight, &settings,
                                            currentCustomMode())) {
    return false;
  }
  // The stream encodes the CSI frame directly: only the full image works.
  if (settings.width != kImageWidth || settings.height != kImageHeight) return false;
  run.settings = settings;
  run.quality = settings.quality;
  run.stages.normal = ExposureLimits{
      kMode.min_exposure_lines,
      local_camera_stream::maxExposureLinesForFps(settings.fps, kMode.frame_ms,
                                                  kMode.default_exposure_lines,
                                                  kMode.max_exposure_lines),
      kMode.min_gain_x16, kMode.max_gain_x16};
  // Auto trades frame rate for light in the dark, like an IP camera at night;
  // a fixed mode keeps its rate.
  run.stages.night_max_lines = settings.mode_id == local_camera_stream::kModeAuto
                                   ? kMode.max_exposure_lines
                                   : run.stages.normal.max_lines;
  run.stages.max_total_gain_x16 = kMode.max_total_gain_x16;
  applyGainLimit(run.stages);
  const uint16_t longest = run.stages.night_max_lines > run.stages.normal.max_lines
                               ? run.stages.night_max_lines
                               : run.stages.normal.max_lines;
  if (g_exposure.lines > longest) {
    g_exposure.lines = longest;
    if (g_sensor_ready) g_sensor.setExposure(g_exposure.lines, g_exposure.gain_x16);
  }
  run.pacer.reset(static_cast<uint64_t>(esp_timer_get_time()),
                  local_camera_stream::periodUsForFps(settings.fps));
  return true;
}

// Stop conditions the worker sees without the loop task.
StopReason streamStopCondition() {
  if (!g_enabled.load() || g_paused.load()) return StopReason::Disabled;
  if (g_abort.load()) return StopReason::Shutdown;
  if (!g_stream_wanted.load()) {
    const StopReason reason = static_cast<StopReason>(g_stream_stop_reason.load());
    return reason == StopReason::None ? StopReason::StreamStop : reason;
  }
  // The display camera popup has priority over the upload.
  if (camera_stream_is_active()) return StopReason::Popup;
  return StopReason::None;
}

// Waits up to wait_ms for a notification. Bits the outer worker loop must
// still handle are collected in *leftover; a snapshot request is answered as
// busy because the stream owns the pipeline.
StopReason streamWait(uint32_t wait_ms, uint32_t* leftover) {
  uint32_t bits = 0;
  xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(wait_ms));
  if (bits & kNotifyCapture) {
    g_capture_failed.fetch_add(1);
    publishErrorReply(g_pending.id, ErrorCode::Busy);
    g_capture_busy.store(false);
  }
  *leftover |= bits & (kNotifyShutdown | kNotifyDisable | kNotifyRelease | kNotifyProbe);
  if (bits & kNotifyShutdown) return StopReason::Shutdown;
  if (bits & kNotifyDisable) return StopReason::Disabled;
  if (bits & kNotifyRelease) return StopReason::Sleep;
  return streamStopCondition();
}

// Initial exposure/white balance with a bounded budget before the first frame.
StopReason streamSettle(StreamRun& run, uint32_t* leftover) {
  static isp_ae_result_t ae_result;
  static isp_awb_stat_result_t awb_result;
  const uint32_t started_ms = millis();
  bool exposure_done = false;
  bool balance_done = false;
  for (uint8_t iteration = 0; iteration < kAutoExposureMaxIterations; ++iteration) {
    const StopReason reason = streamWait(0, leftover);
    if (reason != StopReason::None) return reason;
    if (static_cast<uint32_t>(millis() - started_ms) >= kStreamSettleBudgetMs) break;
    if (esp_isp_ae_controller_get_oneshot_statistics(
            g_pipe.ae, kStreamSettleStatisticsMs, &ae_result) != ESP_OK) {
      continue;
    }
    if (esp_isp_awb_controller_get_oneshot_statistics(
            g_pipe.awb, kStreamSettleStatisticsMs, &awb_result) == ESP_OK) {
      const WhiteBalanceGains next = grayWorldGains(
          awb_result.sum_r, awb_result.sum_g, awb_result.sum_b,
          awb_result.white_patch_num, kMinAwbSamples, g_gains);
      balance_done = gainsSettled(next, g_gains);
      g_gains = next;
      applyColorCorrection();
    }
    run.mean_luma = weightedMeanLuma(ae_result.luminance);
    const ExposureStep step =
        stepStagedExposure(g_exposure, run.mean_luma, aeTarget(), 12, run.stages);
    publishExposure(run.mean_luma);
    if (stepDigitalGain(run.mean_luma, step.limited)) {
      exposure_done = false;
      vTaskDelay(pdMS_TO_TICKS(currentFrameMs() * 2));
      continue;
    }
    exposure_done = step.converged;
    if (exposure_done && balance_done) break;
    if (!step.converged && setExposureIfChanged(step.next)) {
      vTaskDelay(pdMS_TO_TICKS(currentFrameMs() * 2));
    }
  }
  run.last_tune_ms = millis();
  run.last_awb_ms = run.last_tune_ms;
  return StopReason::None;
}

// At most one statistics read per kStreamTuneIntervalMs, alternating AE and
// AWB (AWB at most once per second), and only in the time left before the
// next deadline. Every exposure change writes the SCCB bus shared with touch.
void streamAutoTune(StreamRun& run, uint32_t budget_ms) {
  static isp_ae_result_t ae_result;
  static isp_awb_stat_result_t awb_result;
  const uint32_t now_ms = millis();
  const uint32_t since_ms = now_ms - run.last_tune_ms;
  if (since_ms < kStreamTuneIntervalMs) return;
  int timeout_ms = budget_ms > 3 ? static_cast<int>(budget_ms - 3) : 0;
  if (timeout_ms > kStreamStatisticsTimeoutMs) timeout_ms = kStreamStatisticsTimeoutMs;
  if (timeout_ms < kStreamMinStatisticsWaitMs) {
    if (since_ms < kStreamTuneForceMs) return;
    timeout_ms = kStreamStatisticsTimeoutMs;
  }
  run.last_tune_ms = now_ms;
  // A Max. gain change applies within the running stream.
  applyGainLimit(run.stages);
  const bool awb = run.awb_next &&
                   static_cast<uint32_t>(now_ms - run.last_awb_ms) >= kStreamAwbIntervalMs;
  run.awb_next = !run.awb_next;
  if (awb) {
    run.last_awb_ms = now_ms;
    if (esp_isp_awb_controller_get_oneshot_statistics(g_pipe.awb, timeout_ms, &awb_result) !=
        ESP_OK) {
      return;
    }
    const WhiteBalanceGains next = grayWorldGains(
        awb_result.sum_r, awb_result.sum_g, awb_result.sum_b,
        awb_result.white_patch_num, kMinAwbSamples, g_gains);
    if (!gainsSettled(next, g_gains)) {
      g_gains = next;
      applyColorCorrection();
    }
    return;
  }
  if (esp_isp_ae_controller_get_oneshot_statistics(g_pipe.ae, timeout_ms, &ae_result) !=
      ESP_OK) {
    return;
  }
  run.mean_luma = weightedMeanLuma(ae_result.luminance);
  const ExposureStep step =
      stepStagedExposure(g_exposure, run.mean_luma, aeTarget(), 12, run.stages);
  publishExposure(run.mean_luma);
  if (stepDigitalGain(run.mean_luma, step.limited)) return;
  if (!step.converged) setExposureIfChanged(step.next);
}

enum class StreamEncode : uint8_t { Ok, ArbiterBusy, Failed };

// One stream frame through the hardware encoder. A short arbiter wait: when
// the display PPA or the HA camera decoder holds the 2D-DMA pool the frame is
// skipped instead of waited for.
// The caller holds the 2D-DMA arbiter.
StreamEncode encodeStreamFrame(const uint8_t* input, uint32_t input_bytes, uint16_t width,
                               uint16_t height, uint8_t quality, uint32_t* size) {
  *size = 0;
  if (!g_pipe.jpeg && !createJpegEncoder()) return StreamEncode::Failed;
  esp_err_t err = ESP_OK;
  {
    jpeg_encode_cfg_t config = {};
    config.width = width;
    config.height = height;
    config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
    config.sub_sample = kJpegSubsampling;
    config.image_quality = quality;
    err = jpeg_encoder_process(g_pipe.jpeg, &config, input, input_bytes, g_pipe.jpeg_out,
                               static_cast<uint32_t>(g_pipe.jpeg_capacity), size);
    if (err != ESP_OK) {
      // Drop a possibly wedged engine while the arbiter is still held.
      jpeg_del_encoder_engine(g_pipe.jpeg);
      g_pipe.jpeg = nullptr;
    }
  }
  if (err != ESP_OK) {
    logCaptureError("Stream JPEG encode failed", err);
    return StreamEncode::Failed;
  }
  return StreamEncode::Ok;
}

// Freezes the next complete CSI frame, prepares the JPEG input and hands the
// encoded frame to the sender. The CSI keeps writing the other buffer.
void requestStreamStop(StopReason reason);

// The upload cannot keep up (noisy low-light frames are much larger):
// smaller frames instead of dropped ones.
void noteUploadBusy(StreamRun& run) {
  ++run.window.busy;
  run.calm_frames = 0;
  if (++run.busy_frames >= kBusyFramesBeforeQualityDrop &&
      run.quality > local_camera_stream::kMinQuality) {
    run.busy_frames = 0;
    const uint8_t previous = run.quality;
    run.quality = local_camera_stream::reducedQuality(run.quality);
    if (logDue(&run.quality_log_ms, kErrorLogIntervalMs)) {
      Serial.printf("[LocalCamStream] Upload busy, quality %u -> %u\n",
                    static_cast<unsigned>(previous), static_cast<unsigned>(run.quality));
    }
  }
}

void streamCaptureFrame(StreamRun& run) {
  StreamWindow& window = run.window;
  const uint32_t frame_ms = currentFrameMs();
  // The sender still has an unsent frame: encoding another one now would only
  // replace it, a wasted 2D-DMA hold the display needs for its rotation.
  // Wait up to one frame interval for the sender to take it.
  if (local_camera_upload::framePending()) {
    const uint32_t wait_started_ms = millis();
    while (local_camera_upload::framePending() &&
           static_cast<uint32_t>(millis() - wait_started_ms) < frame_ms) {
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (local_camera_upload::framePending()) {
      noteUploadBusy(run);
      return;
    }
  }
  xQueueReset(g_isr.frames);
  g_isr.frozen = -1;
  g_isr.armed = true;
  const uint32_t freeze_started_ms = millis();
  FrameEvent event{};
  while (g_isr.frozen < 0 &&
         static_cast<uint32_t>(millis() - freeze_started_ms) < 2 * frame_ms + 10) {
    xQueueReceive(g_isr.frames, &event, pdMS_TO_TICKS(frame_ms));
  }
  g_isr.armed = false;
  const int8_t frozen = g_isr.frozen;
  if (frozen < 0) {
    ++window.noframe;
    if (run.noframe_streak < UINT16_MAX) ++run.noframe_streak;
    return;
  }
  run.noframe_streak = 0;

  // Only the JPEG encoder uses the 2D-DMA pool (no PPA pass): one short
  // hold per frame. When the display or the HA camera decoder has the pool,
  // the frame is skipped instead of waited for.
  const uint8_t* input = g_isr.buffers[frozen];
  const uint32_t prep_ms = 0;  // No pixel pass before the encoder.
  uint32_t size = 0;
  uint32_t encode_ms = 0;
  StreamEncode result = StreamEncode::Failed;
  {
    Dma2dArbiterGuard guard(kStreamArbiterTimeoutMs);
    if (!guard.locked()) {
      g_isr.frozen = -1;
      ++window.arb;
      return;
    }
    const uint32_t encode_started_ms = millis();
    // The sensor delivered the JPEG size in the wanted orientation: the
    // frozen CSI buffer is the encoder input as it is.
    result = encodeStreamFrame(input, kJpegInputBytes, kImageWidth, kImageHeight, run.quality,
                               &size);
    encode_ms = millis() - encode_started_ms;
  }
  // The encoder has read the CSI buffer; the ISR may write it again.
  g_isr.frozen = -1;
  if (result != StreamEncode::Ok) return;
  ++window.encoded;
  ++run.encoded_total;
  window.encode_ms_total += encode_ms;
  if (encode_ms > window.encode_ms_max) window.encode_ms_max = encode_ms;
  window.prep_ms_total += prep_ms;

  if (size > local_camera_stream::kMaxFrameBytes || size > g_pipe.jpeg_capacity) {
    // Never re-encode the same frame (each attempt holds the arbiter); the
    // rest of this mode run uses a lower quality instead, below the normal
    // floor if needed.
    ++window.big;
    const uint8_t previous = run.quality;
    run.quality = local_camera_stream::reducedQualityForSize(run.quality);
    if (logDue(&run.big_log_ms, kErrorLogIntervalMs)) {
      Serial.printf("[LocalCamStream] Frame of %u bytes over the %u byte limit; quality %u -> %u\n",
                    static_cast<unsigned>(size),
                    static_cast<unsigned>(local_camera_stream::kMaxFrameBytes),
                    static_cast<unsigned>(previous), static_cast<unsigned>(run.quality));
    }
    return;
  }
  if (!looksLikeCompleteJpeg(g_pipe.jpeg_out, size)) return;
  bool replaced = false;
  if (!local_camera_upload::publishFrame(g_pipe.jpeg_out, size, &replaced) || replaced) {
    noteUploadBusy(run);
    return;
  }
  run.busy_frames = 0;
  if (run.quality < run.settings.quality && ++run.calm_frames >= kCalmFramesBeforeQualityRise) {
    run.calm_frames = 0;
    const unsigned raised = run.quality + local_camera_stream::kQualityStep;
    run.quality = static_cast<uint8_t>(raised < run.settings.quality ? raised
                                                                      : run.settings.quality);
  }
}

StreamStatus makeStreamStatus(const StreamRun& run, bool active, StopReason last_stop,
                              bool has_window) {
  StreamStatus status;
  status.active = active;
  status.connected = active && local_camera_upload::connected();
  status.settings = run.settings;
  status.quality = run.quality;
  status.frames_total = local_camera_upload::framesSentTotal();
  status.reconnects = local_camera_upload::reconnectsTotal();
  status.last_stop = last_stop;
  status.has_window = has_window;
  if (has_window) status.window = run.window;
  return status;
}

// One aggregated diagnostic line per window, never a line per frame.
void streamDiagnostics(StreamRun& run, bool force) {
  const uint32_t now_ms = millis();
  const uint32_t elapsed_ms = now_ms - run.window_started_ms;
  if (!force && elapsed_ms < local_camera_stream::kDiagWindowMs) return;
  local_camera_upload::takeWindow(&run.window);
  run.window.window_ms = elapsed_ms ? elapsed_ms : 1;
  char line[320];
  if (local_camera_stream::formatDiagLine(line, sizeof(line), run.settings, run.quality,
                                          run.window)) {
    Serial.println(line);
  }
  updateStreamStatus(makeStreamStatus(run, true, StopReason::None, true));
  run.window = StreamWindow{};
  run.window_started_ms = now_ms;
}

// Runs one stream until a stop condition. Returns notification bits that the
// worker loop still has to handle (shutdown, disable, release, probe).
uint32_t runStream() {
  uint32_t leftover = 0;
  StopReason reason = StopReason::None;
  StreamRun& run = g_stream_run;
  run = StreamRun{};
  bool sensor_started = false;
  bool upload_started = false;
  // The sensor or receiver stopped delivering frames: rebuild after the stop.
  bool no_frames = false;
  const uint32_t started_ms = millis();
  const uint32_t frames_at_start = local_camera_upload::framesSentTotal();

  do {
    if (!ensureSensor(true)) {
      reason = StopReason::SensorUnavailable;
      break;
    }
    releaseUsedPipeline();
    if (!ensurePipeline() || !applyStreamSettings(run)) {
      reason = StopReason::Error;
      break;
    }
    if (!local_camera_upload::start(workerCore())) {
      Serial.println("[LocalCamStream] Sender task or frame slots unavailable");
      reason = StopReason::Error;
      break;
    }
    upload_started = true;

    if (!applyOrientation(false)) {
      reason = StopReason::SensorUnavailable;
      break;
    }
    g_isr.inflight = -1;
    g_isr.frozen = -1;
    g_isr.armed = false;
    xQueueReset(g_isr.frames);
    applyImageSettingsIfChanged();
    applyColorCorrection();
    g_sensor.setExposure(g_exposure.lines, g_exposure.gain_x16);
    esp_err_t err = esp_cam_ctlr_start(g_pipe.csi);
    if (err != ESP_OK) {
      logCaptureError("Stream CSI start failed", err);
      reason = StopReason::Error;
      break;
    }
    g_pipe.csi_running = true;
    g_pipe.started = true;
    sensor_started = true;
    g_sensor_capturing.store(true);
    err = g_sensor.setStream(true);
    if (err != ESP_OK) {
      logCaptureError("Stream sensor stream-on failed", err);
      reason = StopReason::Error;
      break;
    }
    FrameEvent event{};
    if (xQueueReceive(g_isr.frames, &event, pdMS_TO_TICKS(kFirstFrameTimeoutMs)) != pdTRUE) {
      logCaptureError("Stream got no CSI frame", ESP_ERR_TIMEOUT);
      reason = StopReason::Error;
      no_frames = true;
      break;
    }
    reason = streamSettle(run, &leftover);
    if (reason != StopReason::None) break;

    Serial.printf(
        "[LocalCamStream] Start mode=%u %ux%u@%u q=%u source=%s exp=%u gain=%u/16 "
        "digital=%.2fx luma=%u in %u ms, int=%uKB psram=%uKB\n",
        static_cast<unsigned>(run.settings.mode_id), static_cast<unsigned>(run.settings.width),
        static_cast<unsigned>(run.settings.height), static_cast<unsigned>(run.settings.fps),
        static_cast<unsigned>(run.quality),
        run.settings.mode_id == local_camera_stream::kModeAuto ? "auto" : "setting",
        static_cast<unsigned>(g_exposure.lines), static_cast<unsigned>(g_exposure.gain_x16),
        static_cast<double>(digitalGainForStep(g_pipe.gamma_digital_step)),
        static_cast<unsigned>(run.mean_luma), static_cast<unsigned>(millis() - started_ms),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U));
    run.window_started_ms = millis();
    updateStreamStatus(makeStreamStatus(run, true, StopReason::None, false));

    for (;;) {
      reason = streamStopCondition();
      if (reason != StopReason::None) break;
      if (g_stream_mode_generation.load() != run.mode_generation) {
        if (!applyStreamSettings(run)) {
          reason = StopReason::Error;
          break;
        }
        // Frames of the new size start on a fresh connection.
        local_camera_upload::requestReconnect();
        Serial.printf("[LocalCamStream] Mode changed to %u: %ux%u@%u q=%u, reconnecting\n",
                      static_cast<unsigned>(run.settings.mode_id),
                      static_cast<unsigned>(run.settings.width),
                      static_cast<unsigned>(run.settings.height),
                      static_cast<unsigned>(run.settings.fps),
                      static_cast<unsigned>(run.quality));
      }
      // User image settings take effect with the next captured frame.
      applyImageSettingsIfChanged();
      // A display rotation or mirror change turns the sensor readout.
      if (!applyOrientation(true)) {
        reason = StopReason::Error;
        break;
      }
      streamDiagnostics(run, false);
      const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
      if (!local_camera_upload::connected()) {
        // Nothing to send to: keep the sensor running, restart the deadlines
        // once the sender is connected.
        run.pacer.reset(now_us, local_camera_stream::periodUsForFps(run.settings.fps));
        reason = streamWait(kStreamSliceMs, &leftover);
        if (reason != StopReason::None) break;
        continue;
      }
      const uint64_t wait_us = run.pacer.waitUs(now_us);
      if (wait_us > 0) {
        uint32_t wait_ms = static_cast<uint32_t>((wait_us + 999u) / 1000u);
        if (wait_ms > kStreamSliceMs) wait_ms = kStreamSliceMs;
        reason = streamWait(wait_ms, &leftover);
        if (reason != StopReason::None) break;
        continue;
      }
      if (run.pacer.consume(now_us)) ++run.window.late;
      streamCaptureFrame(run);
      if (run.noframe_streak >= kNoFrameResetStreak) {
        reason = StopReason::Error;
        no_frames = true;
        break;
      }
      const uint64_t after_us = static_cast<uint64_t>(esp_timer_get_time());
      streamAutoTune(run, static_cast<uint32_t>(run.pacer.waitUs(after_us) / 1000u));
      reason = streamWait(0, &leftover);
      if (reason != StopReason::None) break;
    }
  } while (false);

  // Stop order: sender first (it may still read a slot), then the sensor and
  // the receiver, then the buffers.
  if (upload_started) {
    local_camera_upload::stop();
    if (local_camera_upload::waitIdle(kSenderStopWaitMs)) {
      local_camera_upload::releaseBuffers();
    } else {
      Serial.println("[LocalCamStream] Sender still closing; frame slots kept until it ends");
    }
  }
  if (sensor_started) stopStreaming();
  g_isr.armed = false;
  g_isr.frozen = -1;
  // After the sender stopped (it no longer reads a frame slot or the CSI
  // buffers): the next run starts from a sensor software reset.
  if (no_frames) resetAfterNoFrames("stream");
  g_last_used_ms = millis();
  if (run.window_started_ms != 0) streamDiagnostics(run, true);

  Serial.printf(
      "[LocalCamStream] End: reason=%s frames=%u encoded=%u after %u ms, int=%uKB dma=%uKB\n",
      local_camera_stream::stopReasonName(reason),
      static_cast<unsigned>(local_camera_upload::framesSentTotal() - frames_at_start),
      static_cast<unsigned>(run.encoded_total), static_cast<unsigned>(millis() - started_ms),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) /
                            1024U));
  if (reason == StopReason::Error || reason == StopReason::SensorUnavailable) {
    const uint32_t now_ms = millis();
    g_stream_error_ms.store(now_ms ? now_ms : 1);
  }
  // Keep the last diagnostic window for /api/status; only mark the end.
  portENTER_CRITICAL(&g_stream_mux);
  g_stream_status.active = false;
  g_stream_status.connected = false;
  g_stream_status.last_stop = reason;
  portEXIT_CRITICAL(&g_stream_mux);
  g_stream_stop_reason.store(static_cast<uint8_t>(reason));
  // wanted before running: the loop starts a new run only when running is
  // false, so a keepalive can never be lost between the two stores.
  g_stream_wanted.store(false);
  g_stream_running.store(false);
  return leftover;
}

void workerMain(void*) {
  // Notifications that arrived during a stream run and still need handling.
  uint32_t pending = 0;
  for (;;) {
    TickType_t wait = portMAX_DELAY;
    if (g_pipe.ready) {
      const uint32_t idle_ms = millis() - g_last_used_ms;
      wait = idle_ms >= kPipelineIdleReleaseMs
                 ? 0
                 : pdMS_TO_TICKS(kPipelineIdleReleaseMs - idle_ms);
    }
    if (pending) wait = 0;
    uint32_t bits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &bits, wait);
    bits |= pending;
    pending = 0;

    if (bits & (kNotifyShutdown | kNotifyDisable)) {
      releaseAll();
      // A quick off/on toggle may deliver Disable and Probe together.
      if (!g_enabled.load()) setState(ServiceState::Disabled, Detail::None);
      g_abort.store(false);
      g_shutdown_ack.fetch_add(1);
      if (bits & kNotifyCapture) {
        // A request that raced with disable/shutdown gets an answer without
        // re-opening the sensor that was just released.
        g_capture_failed.fetch_add(1);
        publishErrorReply(g_pending.id, g_enabled.load() ? ErrorCode::Busy
                                                         : ErrorCode::Disabled);
        g_capture_busy.store(false);
        bits &= ~kNotifyCapture;
      }
      if (bits & kNotifyShutdown) continue;
    }
    if (bits & kNotifyProbe) {
      if (g_enabled.load()) {
        ensureSensor(true);
      } else {
        setState(ServiceState::Disabled, Detail::None);
      }
    }
    if (bits & kNotifyCapture) runCapture();
    if ((bits & kNotifyStream) && g_stream_wanted.load() && !abortRequested()) {
      g_stream_running.store(true);
      pending = runStream();
      continue;
    }
    if ((bits & kNotifyStream) && !g_stream_running.load()) g_stream_wanted.store(false);
    if (bits & kNotifyRelease) releasePipeline();
    if (g_pipe.ready && !g_capture_busy.load() &&
        static_cast<uint32_t>(millis() - g_last_used_ms) >= kPipelineIdleReleaseMs) {
      releasePipeline();
      if (logDue(&g_pipeline_log_ms, 600000)) {
        Serial.println("[LocalCam] Pipeline released after idle timeout");
      }
    }
  }
}

bool ensureWorker() {
  if (g_worker) return true;
  const BaseType_t core = (ARDUINO_RUNNING_CORE == 0) ? 1 : 0;
  if (xTaskCreatePinnedToCoreWithCaps(workerMain, "localCam", kWorkerStackBytes,
                                      nullptr, kWorkerPriority, &g_worker, core,
                                      MALLOC_CAP_SPIRAM) != pdPASS) {
    g_worker = nullptr;
    Serial.println("[LocalCam] Worker task could not be created");
    return false;
  }
  return true;
}

void notifyWorker(uint32_t bits) {
  if (g_worker) xTaskNotify(g_worker, bits, eSetBits);
}

// ---------------------------------------------------------------------------
// Live stream control on the loop task.
// ---------------------------------------------------------------------------
// After a stream ended with an error a keepalive restarts it only after this.
constexpr uint32_t kStreamErrorRetryMs = 10000;
// OTA/restart release blocks new streams for this long.
constexpr uint32_t kStreamShutdownBlockMs = 300000;
// Storage work waits at most this long for the stream and a snapshot to stop.
// Flash operations stall both cores; with the camera running, one of them
// tripped the interrupt watchdog during Web Admin tile saves.
constexpr uint32_t kStorageHoldWaitMs = 2000;
// How long the hold lingers after the last storage work (b23 crashed during
// the layout reload about one second after a save, once the stream ran again).
constexpr uint32_t kStorageLingerMs = 5000;

local_camera_stream::GateInputs currentGate() {
  local_camera_stream::GateInputs in;
  const uint32_t now_ms = millis();
  in.session_valid = g_session.valid;
  in.ttl_expired = g_session.valid &&
                   static_cast<int32_t>(now_ms - g_session.deadline_ms) >= 0;
  in.enabled = g_enabled.load() && !g_paused.load();
  in.sensor_ready = currentState() == ServiceState::Ready;
  in.mqtt_connected = networkManager.isMqttConnected();
  in.popup_active = camera_stream_is_active();
  // The upload stays off while the display sleeps (reduced power profile);
  // the session is kept and the first keepalive after wake resumes it.
  in.display_sleeping = powerManager.isInSleep();
  in.storage_hold = storageHoldActive();
  const uint32_t blocked_until = g_stream_blocked_until_ms.load();
  in.shutdown_latched = blocked_until != 0 &&
                        static_cast<int32_t>(now_ms - blocked_until) < 0;
  return in;
}

void requestStreamStop(StopReason reason) {
  if (!g_stream_wanted.load() && !g_stream_running.load()) return;
  g_stream_stop_reason.store(static_cast<uint8_t>(reason));
  g_stream_wanted.store(false);
  notifyWorker(kNotifyStreamStop);
}

void endStreamSession(StopReason reason) {
  if (g_session.valid) {
    Serial.printf("[LocalCamStream] Session ended: reason=%s\n",
                  local_camera_stream::stopReasonName(reason));
  }
  g_session.valid = false;
  g_session.session[0] = '\0';
  local_camera_upload::clearEndpoint();
  requestStreamStop(reason);
}

// Keepalives (re)start a stream that is not running; nothing else starts it.
void tryStartStream() {
  if (!g_session.valid || !g_worker) return;
  if (g_stream_running.load() || g_stream_wanted.load()) return;
  const uint32_t now_ms = millis();
  const uint32_t error_ms = g_stream_error_ms.load();
  if (error_ms != 0 && static_cast<uint32_t>(now_ms - error_ms) < kStreamErrorRetryMs) return;
  const StopReason gate = local_camera_stream::streamGate(currentGate());
  if (gate != StopReason::None) {
    if (logDue(&g_stream_log_ms, kStreamStartLogIntervalMs)) {
      Serial.printf("[LocalCamStream] Start deferred: %s\n",
                    local_camera_stream::stopReasonName(gate));
    }
    return;
  }
  g_stream_error_ms.store(0);
  g_stream_stop_reason.store(static_cast<uint8_t>(StopReason::None));
  g_stream_wanted.store(true);
  notifyWorker(kNotifyStream);
}

void handleStreamCommand(const local_camera_stream::StreamCommand& command) {
  if (!g_enabled.load()) {
    if (logDue(&g_stream_log_ms, kStreamStartLogIntervalMs)) {
      Serial.println("[LocalCamStream] Stream request ignored: camera disabled");
    }
    return;
  }
  if (g_ended_session[0] != '\0' && strcmp(g_ended_session, command.session) == 0) {
    if (logDue(&g_stream_log_ms, kStreamStartLogIntervalMs)) {
      Serial.println("[LocalCamStream] Stream request ignored: session ended on the display");
    }
    return;
  }
  const bool new_session =
      !g_session.valid || strcmp(g_session.session, command.session) != 0;
  if (new_session) {
    if (g_session.valid) {
      Serial.println("[LocalCamStream] Session replaced; the upload reconnects");
    }
    snprintf(g_session.session, sizeof(g_session.session), "%s", command.session);
    g_session.valid = true;
  }
  g_session.deadline_ms = millis() + command.ttl_ms;

  portENTER_CRITICAL(&g_stream_mux);
  const bool hints_changed = !local_camera_stream::sameHints(g_stream_hints, command.hints);
  g_stream_hints = command.hints;
  portEXIT_CRITICAL(&g_stream_mux);
  if (hints_changed && g_stream_mode.load() == local_camera_stream::kModeAuto) {
    g_stream_mode_generation.fetch_add(1);
  }

  local_camera_upload::Endpoint endpoint;
  snprintf(endpoint.session, sizeof(endpoint.session), "%s", command.session);
  snprintf(endpoint.token, sizeof(endpoint.token), "%s", command.token);
  snprintf(endpoint.host, sizeof(endpoint.host), "%s", command.host);
  endpoint.port = command.port;
  local_camera_upload::setEndpoint(endpoint);
  tryStartStream();
}

void handleStreamStop(const char* session) {
  if (!g_session.valid || strcmp(g_session.session, session) != 0) return;
  endStreamSession(StopReason::StreamStop);
}

// Every loop iteration: stop for ttl, MQTT loss, disable, shutdown, popup or
// a sensor that is no longer ready.
void serviceStream() {
  if (!g_session.valid) return;
  const StopReason reason = local_camera_stream::streamGate(currentGate());
  if (reason == StopReason::None) return;
  if (local_camera_stream::reasonEndsSession(reason)) {
    endStreamSession(reason);
  } else {
    requestStreamStop(reason);
  }
}

#endif  // defined(HOMETILES_LOCAL_CAMERA)

}  // namespace

bool supported() { return kSupported; }

bool enabled() { return kSupported && g_enabled.load(); }

void begin() {
#if defined(HOMETILES_LOCAL_CAMERA)
  bool stored = false;
  bool legacy_pause = false;
  uint8_t mode = local_camera_stream::kModeAuto;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    stored = prefs.getBool(kPrefsEnabledKey, false);
    mode = prefs.getUChar(kPrefsStreamModeKey, local_camera_stream::kModeAuto);
    g_mirror.store(prefs.getBool(kPrefsMirrorKey, false));
    // Unknown styles (e.g. from a newer firmware) fall back to the default.
    const uint8_t style =
        prefs.getUChar(kPrefsIndicatorKey, static_cast<uint8_t>(IndicatorStyle::Pill));
    g_indicator_style.store(style <= static_cast<uint8_t>(IndicatorStyle::Pill)
                                ? style
                                : static_cast<uint8_t>(IndicatorStyle::Pill));
    legacy_pause = prefs.isKey(kPrefsLegacyPausedKey);
    const local_camera_stream::CustomMode defaults;
    const local_camera_stream::CustomMode custom = local_camera_stream::makeCustomMode(
        prefs.getUChar(kPrefsCustomFpsKey, defaults.fps),
        prefs.getUChar(kPrefsCustomQualityKey, defaults.quality));
    g_custom_fps.store(custom.fps);
    g_custom_quality.store(custom.quality);
    // Out-of-range values (e.g. from a newer firmware) are clamped.
    const ImageSettings image = makeImageSettings(
        prefs.getChar(kPrefsBrightnessKey, 0), prefs.getChar(kPrefsContrastKey, 0),
        prefs.getUChar(kPrefsSaturationKey, kSaturationDefault),
        prefs.getChar(kPrefsRedKey, 0), prefs.getChar(kPrefsBlueKey, 0),
        prefs.getUChar(kPrefsGainKey, kGainLimitDefault));
    portENTER_CRITICAL(&g_image_mux);
    g_image = image;
    portEXIT_CRITICAL(&g_image_mux);
    g_image_generation.fetch_add(1);
    prefs.end();
  }
  if (legacy_pause) {
    Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
    BatchedNvsWrite::Preferences writable;
    if (writable.begin(kPrefsNamespace, false)) {
      writable.remove(kPrefsLegacyPausedKey);
      if (BatchedNvsWrite::finish(writable)) {
        Serial.println("[LocalCam] Removed the stored pause of an older beta");
      }
    }
  }
  // Unknown ids (from a newer firmware) fall back to Auto.
  g_stream_mode.store(local_camera_stream::isKnownMode(mode) ? mode
                                                             : local_camera_stream::kModeAuto);
  g_enabled.store(stored);
  g_last_capability = false;
  if (!stored) {
    setState(ServiceState::Disabled, Detail::None);
    Serial.println("[LocalCam] Built-in camera disabled (opt-in off)");
    return;
  }
  if (!ensureWorker()) {
    setState(ServiceState::Error, Detail::WorkerUnavailable);
    return;
  }
  setState(ServiceState::Probing, Detail::None);
  notifyWorker(kNotifyProbe);
  Serial.println("[LocalCam] Built-in camera enabled, probing sensor");
#endif
}

bool setEnabled(bool enable) {
#if defined(HOMETILES_LOCAL_CAMERA)
  const bool current = g_enabled.load();
  const ServiceState state = currentState();
  if (enable == current &&
      !(enable && (state == ServiceState::NotFound || state == ServiceState::Error))) {
    return true;
  }
  if (enable != current) {
    Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
    BatchedNvsWrite::Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
      Serial.println("[LocalCam] Could not open preferences");
      return false;
    }
    const bool written = prefs.putBool(kPrefsEnabledKey, enable) > 0;
    if (!BatchedNvsWrite::finish(prefs) || !written) {
      Serial.println("[LocalCam] Could not save the opt-in");
      return false;
    }
  }
  g_enabled.store(enable);
  if (enable) {
    if (!ensureWorker()) {
      setState(ServiceState::Error, Detail::WorkerUnavailable);
      return true;
    }
    g_rate_limiter.reset();
    setState(ServiceState::Probing, Detail::None);
    notifyWorker(kNotifyProbe);
    Serial.println("[LocalCam] Enabled by user, probing sensor");
  } else {
    // Only the worker clears the abort flag; without a worker there is no
    // capture to stop and a stale flag would abort every later capture.
    if (g_worker) g_abort.store(true);
    endStreamSession(StopReason::Disabled);
    setState(ServiceState::Disabled, Detail::None);
    notifyWorker(kNotifyDisable);
    Serial.println("[LocalCam] Disabled by user, releasing camera");
  }
  return true;
#else
  (void)enable;
  return false;
#endif
}

void service() {
  if (!kSupported) return;
#if defined(HOMETILES_LOCAL_CAMERA)
  serviceStream();
#endif
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
}

void onMqttConnected() {
  if (!kSupported) return;
  g_published_generation = g_state_generation.load();
  g_last_capability = bridgeCapability();
  publishStatus();
}

const char* commandTopic() {
  if (!kSupported) return nullptr;
  const char* topic = mqttTopics.topic(TopicKey::LOCAL_CAMERA_CMND);
  return topic && *topic ? topic : nullptr;
}

bool isCommandTopic(const char* topic) {
  const char* command = commandTopic();
  return command && topic && strcmp(topic, command) == 0;
}

bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length) {
  if (!isCommandTopic(topic)) return false;
#if defined(HOMETILES_LOCAL_CAMERA)
  LocalCameraCommand command;
  parseCommand(reinterpret_cast<const char*>(payload), length, &command);
  if (command.kind != CommandKind::Snapshot) {
    if (command.stream_status != local_camera_stream::CommandStatus::Ok) {
      if (logDue(&g_last_request_log_ms, 10000)) {
        Serial.printf("[LocalCamStream] Ignored command: %s (%u bytes)\n",
                      local_camera_stream::commandStatusName(command.stream_status),
                      static_cast<unsigned>(length));
      }
    } else if (command.kind == CommandKind::Stream) {
      handleStreamCommand(command.stream);
    } else if (command.kind == CommandKind::Pause || command.kind == CommandKind::Resume) {
      const bool pause = command.kind == CommandKind::Pause;
      Serial.printf("[LocalCam] Camera %s by Home Assistant\n", pause ? "paused" : "resumed");
      setPaused(pause);
    } else {
      handleStreamStop(command.stop_session);
    }
    return true;
  }
  SnapshotRequest request = command.snapshot;
  const RequestStatus status = command.snapshot_status;
  if (status != RequestStatus::Ok) {
    if (logDue(&g_last_request_log_ms, 10000)) {
      Serial.printf("[LocalCam] Ignored request: %s (%u bytes)\n",
                    requestStatusName(status), static_cast<unsigned>(length));
    }
    return true;
  }

  const uint32_t now_ms = millis();
  const ServiceState state = currentState();
  ErrorCode code = ErrorCode::None;
  if (!g_enabled.load() || g_paused.load()) {
    code = ErrorCode::Disabled;
  } else if (!g_worker || state == ServiceState::NotFound ||
             state == ServiceState::Error) {
    code = ErrorCode::SensorUnavailable;
  } else if (state == ServiceState::Probing || g_capture_busy.load() ||
             g_stream_running.load() || g_stream_wanted.load() ||
             storageHoldActive()) {
    // A running live stream owns the pipeline; the Bridge answers still
    // images from the live frame meanwhile, so a snapshot is simply busy.
    // Storage work holds the camera off as well.
    code = ErrorCode::Busy;
  } else if (!g_rate_limiter.wouldAccept(now_ms)) {
    code = ErrorCode::RateLimited;
  }
  if (code != ErrorCode::None) {
    publishErrorReply(request.id, code);
    return true;
  }
  g_rate_limiter.markAccepted(now_ms);
  request.received_ms = now_ms;
  g_pending = request;
  g_capture_busy.store(true);
  notifyWorker(kNotifyCapture);
#else
  (void)payload;
  (void)length;
#endif
  return true;
}

bool bridgeCapability() {
  return local_camera_contract::bridgeCapability(
      kSupported && Device::kCapabilities.has_builtin_camera, g_enabled.load(),
      currentState() == ServiceState::Ready);
}

const char* stateName() {
  return kSupported ? serviceStateName(currentState()) : "unsupported";
}

void appendStatusJson(String& json) {
  json += "{\"supported\":";
  json += kSupported ? "true" : "false";
  json += ",\"enabled\":";
  json += enabled() ? "true" : "false";
  json += ",\"state\":\"";
  json += stateName();
  json += "\"";
  if (!kSupported) {
    json += "}";
    return;
  }
  const uint16_t chip_id = g_chip_id.load();
  if (chip_id != 0) {
    char id_text[8];
    snprintf(id_text, sizeof(id_text), "0x%04x", static_cast<unsigned>(chip_id));
    json += ",\"chip_id\":\"";
    json += id_text;
    json += "\"";
  }
  if (currentState() == ServiceState::Ready) {
    json += ",\"sensor\":\"";
    json += kSensorName;
    json += "\"";
  }
  json += ",\"detail\":\"";
  json += detailName(static_cast<Detail>(g_detail.load()));
  json += "\"";
#if defined(HOMETILES_LOCAL_CAMERA)
  char last_error[sizeof(g_last_error)];
  portENTER_CRITICAL(&g_last_error_mux);
  memcpy(last_error, g_last_error, sizeof(last_error));
  portEXIT_CRITICAL(&g_last_error_mux);
  last_error[sizeof(last_error) - 1] = '\0';
  if (last_error[0]) {
    // Step names and esp_err_to_name() texts contain no JSON specials.
    json += ",\"last_error\":\"";
    json += last_error;
    json += "\"";
  }
  portENTER_CRITICAL(&g_last_error_mux);
  const AutoTuneReport tune = g_last_autotune;
  const bool has_tune = g_has_autotune;
  portEXIT_CRITICAL(&g_last_error_mux);
  if (has_tune) {
    char text[320];
    snprintf(text, sizeof(text),
             ",\"last_autotune\":{\"steps\":%u,\"ae_samples\":%u,\"awb_samples\":%u,"
             "\"white_patches\":%u,\"luma\":%u,\"target\":%u,\"lines\":%u,\"gain_x16\":%u,"
             "\"digital_x100\":%u,\"limited\":%s,\"wb_red_x100\":%u,\"wb_blue_x100\":%u}",
             static_cast<unsigned>(tune.steps), static_cast<unsigned>(tune.ae_samples),
             static_cast<unsigned>(tune.awb_samples), static_cast<unsigned>(tune.white_patches),
             static_cast<unsigned>(tune.luma), static_cast<unsigned>(tune.target),
             static_cast<unsigned>(tune.lines), static_cast<unsigned>(tune.gain_x16),
             static_cast<unsigned>(tune.digital_x100),
             tune.limited ? "true" : "false", static_cast<unsigned>(tune.wb_red_x100),
             static_cast<unsigned>(tune.wb_blue_x100));
    json += text;
  }
  {
    char exposure[128];
    snprintf(exposure, sizeof(exposure),
             ",\"exposure\":{\"lines\":%u,\"gain_x16\":%u,\"digital_x100\":%u,\"luma\":%u}",
             static_cast<unsigned>(g_report_lines.load()),
             static_cast<unsigned>(g_report_gain_x16.load()),
             static_cast<unsigned>(g_report_digital_x100.load()),
             static_cast<unsigned>(g_report_luma.load()));
    json += exposure;
  }
#endif
  json += ",\"captures\":";
  json += String(g_capture_ok.load());
  json += ",\"failures\":";
  json += String(g_capture_failed.load());
  json += ",\"last_jpeg_bytes\":";
  json += String(g_last_jpeg_bytes.load());
  json += ",\"last_capture_ms\":";
  json += String(g_last_capture_ms.load());
  json += ",\"bridge_capability\":";
  json += bridgeCapability() ? "true" : "false";
#if defined(HOMETILES_LOCAL_CAMERA)
  json += ",\"stream_mode\":";
  json += String(static_cast<unsigned>(g_stream_mode.load()));
  json += ",\"custom\":{\"fps\":";
  json += String(static_cast<unsigned>(g_custom_fps.load()));
  json += ",\"quality\":";
  json += String(static_cast<unsigned>(g_custom_quality.load()));
  json += "}";
  json += ",\"paused\":";
  json += g_paused.load() ? "true" : "false";
  json += ",\"mirror\":";
  json += g_mirror.load() ? "true" : "false";
  json += ",\"indicator\":";
  json += String(static_cast<unsigned>(g_indicator_style.load()));
  {
    const ImageSettings image = currentImageSettings();
    char image_json[128];
    snprintf(image_json, sizeof(image_json),
             ",\"image\":{\"brightness\":%d,\"contrast\":%d,\"saturation\":%u,"
             "\"red\":%d,\"blue\":%d,\"gain\":%u}",
             static_cast<int>(image.brightness), static_cast<int>(image.contrast),
             static_cast<unsigned>(image.saturation), static_cast<int>(image.red),
             static_cast<int>(image.blue), static_cast<unsigned>(image.gain));
    json += image_json;
  }
  portENTER_CRITICAL(&g_stream_mux);
  StreamStatus stream = g_stream_status;
  portEXIT_CRITICAL(&g_stream_mux);
  stream.active = streamActive();
  if (!stream.active) stream.connected = false;
  char stream_json[640];
  if (local_camera_stream::formatStatusJson(stream_json, sizeof(stream_json), stream)) {
    json += ",\"stream\":";
    json += stream_json;
  }
#endif
  json += "}";
}

void releaseForSleep() {
#if defined(HOMETILES_LOCAL_CAMERA)
  // A running stream stops with reason "sleep". While the display sleeps the
  // stream gate defers every keepalive (GateInputs::display_sleeping), so the
  // pipeline stays released; the first keepalive after wake resumes the same
  // session.
  notifyWorker(kNotifyRelease);
#endif
}

bool bridgeStreamCapability() {
  return local_camera_contract::bridgeCapability(
      kSupported && Device::kCapabilities.has_builtin_camera, g_enabled.load(),
      currentState() == ServiceState::Ready);
}

bool streamActive() {
#if defined(HOMETILES_LOCAL_CAMERA)
  return g_stream_running.load() || g_stream_wanted.load() || local_camera_upload::busy();
#else
  return false;
#endif
}

bool stopStreamForTransportRecovery() {
#if defined(HOMETILES_LOCAL_CAMERA)
  // Atomics and xTaskNotify only: safe on the MQTT worker. Ending with Error
  // holds keepalive restarts back for kStreamErrorRetryMs, which gives the
  // Wi-Fi/SDIO recovery time to run; its MQTT disconnect then ends the session.
  const bool was_wanted = g_stream_wanted.load();
  if (!was_wanted && !g_stream_running.load()) return false;
  requestStreamStop(StopReason::Error);
  return was_wanted;
#else
  return false;
#endif
}

uint8_t streamMode() { return g_stream_mode.load(); }

// The picture Home Assistant shows: a quarter-turn board's JPEG turned.
uint16_t imageWidth() {
#if defined(HOMETILES_LOCAL_CAMERA)
  return static_cast<uint16_t>(kQuarterTurn ? kImageHeight : kImageWidth);
#else
  return 0;
#endif
}

uint16_t imageHeight() {
#if defined(HOMETILES_LOCAL_CAMERA)
  return static_cast<uint16_t>(kQuarterTurn ? kImageWidth : kImageHeight);
#else
  return 0;
#endif
}

bool indicatorActive() {
  if (g_sensor_capturing.load()) return true;
  return static_cast<int32_t>(g_indicator_hold_until_ms.load() - millis()) > 0;
}

bool paused() { return kSupported && g_paused.load(); }

bool setPaused(bool paused) {
#if defined(HOMETILES_LOCAL_CAMERA)
  if (paused == g_paused.load()) return true;
  // A running stream and a snapshot in progress stop on their next check
  // (streamStopCondition(), abortRequested()).
  g_paused.store(paused);
  Serial.printf("[LocalCam] Camera %s\n", paused ? "paused" : "resumed");
  publishStatus();
  return true;
#else
  (void)paused;
  return false;
#endif
}

bool endStream() {
#if defined(HOMETILES_LOCAL_CAMERA)
  if (!g_session.valid) return false;
  snprintf(g_ended_session, sizeof(g_ended_session), "%s", g_session.session);
  if (g_stream_running.load()) g_indicator_skip_hold.store(true);
  Serial.println("[LocalCamStream] Stream ended on the display");
  endStreamSession(StopReason::StreamStop);
  // The retained status names the ended session: the Bridge closes its
  // viewers (no frozen image, no still-image fallback) and the next viewer
  // gets a new session that streams normally.
  publishStatus();
  return true;
#else
  return false;
#endif
}

bool mirror() { return g_mirror.load(); }

bool setMirror(bool mirror) {
#if defined(HOMETILES_LOCAL_CAMERA)
  if (mirror == g_mirror.load()) return true;
  {
    Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
    BatchedNvsWrite::Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
      Serial.println("[LocalCam] Could not open preferences");
      return false;
    }
    const bool written = prefs.putBool(kPrefsMirrorKey, mirror) > 0;
    if (!BatchedNvsWrite::finish(prefs) || !written) {
      Serial.println("[LocalCam] Could not save the mirror setting");
      return false;
    }
  }
  g_mirror.store(mirror);
  Serial.printf("[LocalCam] Mirror %s\n", mirror ? "on" : "off");
  return true;
#else
  (void)mirror;
  return false;
#endif
}

IndicatorStyle indicatorStyle() {
  return static_cast<IndicatorStyle>(g_indicator_style.load());
}

bool setIndicatorStyle(IndicatorStyle style) {
#if defined(HOMETILES_LOCAL_CAMERA)
  const uint8_t value = static_cast<uint8_t>(style);
  if (value > static_cast<uint8_t>(IndicatorStyle::Pill)) return false;
  if (value == g_indicator_style.load()) return true;
  {
    Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
    BatchedNvsWrite::Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
      Serial.println("[LocalCam] Could not open preferences");
      return false;
    }
    const bool written = prefs.putUChar(kPrefsIndicatorKey, value) > 0;
    if (!BatchedNvsWrite::finish(prefs) || !written) {
      Serial.println("[LocalCam] Could not save the indicator style");
      return false;
    }
  }
  g_indicator_style.store(value);
  static constexpr const char* kNames[] = {"none", "line", "pill"};
  Serial.printf("[LocalCam] Indicator style %s\n", kNames[value]);
  return true;
#else
  (void)style;
  return false;
#endif
}

void beginStorageHold() {
#if defined(HOMETILES_LOCAL_CAMERA)
  if (g_storage_holds.fetch_add(1) != 0) return;
  const auto camera_busy = [] {
    return g_stream_running.load() || g_stream_wanted.load() || g_capture_busy.load() ||
           local_camera_upload::busy();
  };
  if (!camera_busy()) return;
  // A snapshot in progress aborts (abortRequested()); a running stream stops
  // like on display sleep: the session survives and resumes afterwards.
  requestStreamStop(StopReason::Storage);
  const uint32_t started_ms = millis();
  while (camera_busy() &&
         static_cast<uint32_t>(millis() - started_ms) < kStorageHoldWaitMs) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  Serial.printf("[LocalCam] Camera %s for storage work after %u ms\n",
                camera_busy() ? "still busy" : "stopped",
                static_cast<unsigned>(millis() - started_ms));
#endif
}

void endStorageHold() {
#if defined(HOMETILES_LOCAL_CAMERA)
  if (g_storage_holds.load() == 0) return;
  if (g_storage_holds.fetch_sub(1) != 1) return;
  // The layout reload after a save still reads the flash: the camera stays
  // off a little longer, then the next keepalive resumes the same session.
  const uint32_t until = millis() + kStorageLingerMs;
  g_storage_linger_until_ms.store(until ? until : 1);
#endif
}

ImageSettings imageSettings() { return currentImageSettings(); }

bool setImageSettings(const ImageSettings& requested) {
#if defined(HOMETILES_LOCAL_CAMERA)
  // Clamp again: the handler validates, but NVS must never hold other values.
  const ImageSettings wanted = makeImageSettings(
      requested.brightness, requested.contrast, requested.saturation,
      requested.red, requested.blue, requested.gain);
  const ImageSettings current = currentImageSettings();
  if (sameImageSettings(wanted, current)) return true;
  {
    Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
    BatchedNvsWrite::Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
      Serial.println("[LocalCam] Could not open preferences");
      return false;
    }
    // Only changed keys are written; a slider drag touches one key.
    bool written = true;
    if (wanted.brightness != current.brightness) {
      written = prefs.putChar(kPrefsBrightnessKey, wanted.brightness) > 0 && written;
    }
    if (wanted.contrast != current.contrast) {
      written = prefs.putChar(kPrefsContrastKey, wanted.contrast) > 0 && written;
    }
    if (wanted.saturation != current.saturation) {
      written = prefs.putUChar(kPrefsSaturationKey, wanted.saturation) > 0 && written;
    }
    if (wanted.red != current.red) {
      written = prefs.putChar(kPrefsRedKey, wanted.red) > 0 && written;
    }
    if (wanted.blue != current.blue) {
      written = prefs.putChar(kPrefsBlueKey, wanted.blue) > 0 && written;
    }
    if (wanted.gain != current.gain) {
      written = prefs.putUChar(kPrefsGainKey, wanted.gain) > 0 && written;
    }
    if (!BatchedNvsWrite::finish(prefs) || !written) {
      Serial.println("[LocalCam] Could not save the image settings");
      return false;
    }
  }
  portENTER_CRITICAL(&g_image_mux);
  g_image = wanted;
  portEXIT_CRITICAL(&g_image_mux);
  // The worker applies it before the next snapshot or stream frame.
  g_image_generation.fetch_add(1);
  Serial.printf("[LocalCam] Image brightness=%d contrast=%d saturation=%u red=%d blue=%d "
                "gain=%u%%\n",
                static_cast<int>(wanted.brightness), static_cast<int>(wanted.contrast),
                static_cast<unsigned>(wanted.saturation), static_cast<int>(wanted.red),
                static_cast<int>(wanted.blue), static_cast<unsigned>(wanted.gain));
  return true;
#else
  (void)requested;
  return false;
#endif
}

bool setStreamMode(uint8_t mode) {
#if defined(HOMETILES_LOCAL_CAMERA)
  if (!local_camera_stream::isKnownMode(mode)) return false;
  if (mode == g_stream_mode.load()) return true;
  {
    Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
    BatchedNvsWrite::Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
      Serial.println("[LocalCam] Could not open preferences");
      return false;
    }
    const bool written = prefs.putUChar(kPrefsStreamModeKey, mode) > 0;
    if (!BatchedNvsWrite::finish(prefs) || !written) {
      Serial.println("[LocalCam] Could not save the stream mode");
      return false;
    }
  }
  g_stream_mode.store(mode);
  // A running stream switches after its current frame and reconnects within
  // the same session.
  g_stream_mode_generation.fetch_add(1);
  Serial.printf("[LocalCamStream] Stream mode set to %u\n", static_cast<unsigned>(mode));
  return true;
#else
  (void)mode;
  return false;
#endif
}

local_camera_stream::CustomMode customMode() { return currentCustomMode(); }

bool setCustomMode(const local_camera_stream::CustomMode& requested) {
#if defined(HOMETILES_LOCAL_CAMERA)
  // Clamp again: the handler validates, but NVS must never hold other values.
  const local_camera_stream::CustomMode wanted =
      local_camera_stream::makeCustomMode(requested.fps, requested.quality);
  const local_camera_stream::CustomMode current = currentCustomMode();
  if (wanted.fps == current.fps && wanted.quality == current.quality) return true;
  {
    Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
    BatchedNvsWrite::Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
      Serial.println("[LocalCam] Could not open preferences");
      return false;
    }
    bool written = true;
    if (wanted.fps != current.fps) {
      written = prefs.putUChar(kPrefsCustomFpsKey, wanted.fps) > 0 && written;
    }
    if (wanted.quality != current.quality) {
      written = prefs.putUChar(kPrefsCustomQualityKey, wanted.quality) > 0 && written;
    }
    if (!BatchedNvsWrite::finish(prefs) || !written) {
      Serial.println("[LocalCam] Could not save the custom stream mode");
      return false;
    }
  }
  g_custom_fps.store(wanted.fps);
  g_custom_quality.store(wanted.quality);
  // A running Custom stream switches after its current frame.
  if (g_stream_mode.load() == local_camera_stream::kModeCustom) {
    g_stream_mode_generation.fetch_add(1);
  }
  Serial.printf("[LocalCamStream] Custom mode %u fps q%u\n", static_cast<unsigned>(wanted.fps),
                static_cast<unsigned>(wanted.quality));
  return true;
#else
  (void)requested;
  return false;
#endif
}

void shutdown(const char* reason) {
#if defined(HOMETILES_LOCAL_CAMERA)
  // No keepalive may restart a stream during an OTA or before a restart.
  const uint32_t blocked_until = millis() + kStreamShutdownBlockMs;
  g_stream_blocked_until_ms.store(blocked_until ? blocked_until : 1);
  endStreamSession(StopReason::Shutdown);
  if (!g_worker) return;
  const uint32_t ack = g_shutdown_ack.load();
  g_abort.store(true);
  notifyWorker(kNotifyShutdown);
  const uint32_t started_ms = millis();
  while (g_shutdown_ack.load() == ack &&
         static_cast<uint32_t>(millis() - started_ms) < kShutdownWaitMs) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (g_shutdown_ack.load() == ack) {
    Serial.printf("[LocalCam] Worker did not release the camera for %s in %u ms\n",
                  reason ? reason : "shutdown",
                  static_cast<unsigned>(kShutdownWaitMs));
  } else {
    Serial.printf("[LocalCam] Camera released for %s\n", reason ? reason : "shutdown");
  }
#else
  (void)reason;
#endif
}

}  // namespace local_camera
