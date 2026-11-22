// Host regression for voice timers: the generated chimes (timer_chime.h)
// against the reference renderer of the chime sample page, the Bridge
// contract (voice_timer_contract.h: parsing, requests, countdown text), and
// the wiring into the speaker, the MQTT topics, the loop, the popup hide
// paths and the central translations.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');

// --- Wiring (no compiler needed) -------------------------------------------
const speaker = read('src/audio/speaker.cpp');
assert.match(speaker, /if \(voice_timers::handleCommandPayload\(payload, length\)\) return;\n\s+assist::Command command;/,
             'timer messages are taken before the 256-byte microphone command parser');
assert.match(speaker, /constexpr uint32_t kNotifyChime = 1u << 5;/);
assert.match(speaker, /if \(bits & kNotifyChime\)/);
assert.match(speaker, /void renderChime\(uint16_t request, uint32_t generation\)/);
// A stop that arrives while the chime's output opens still ends it.
assert.match(speaker, /g_chime_active\.store\(true\);\n\s*const uint16_t chime = g_chime_request\.exchange\(kNoChime\);/);
assert.match(speaker, /g_stop_generation\.fetch_add\(1\);\n\s*g_chime_request\.store\(static_cast<uint16_t>\(chime \| \(repeats << 8\)\)\);/);
assert.match(read('src/network/mqtt/mqtt_topics.cpp'),
             /\{TopicKey::ASSIST_TIMER_STAT, TopicDomain::State, "assist_timer"\}/);
const ino = read('HomeTiles.ino');
assert.match(ino, /timer_ui::service\(\);/);
assert.match(ino, /if \(timer_ui::serviceSleep\(\)\)/);
for (const file of ['src/ui/ui_manager.cpp', 'src/ui/navigation/view_navigation.cpp']) {
  const text = read(file);
  const pins = text.match(/hide_pin_popup\(\);/g) || [];
  const timers = text.match(/timer_ui::hide_popup\(\);/g) || [];
  assert.ok(timers.length >= pins.length, `${file}: every popup hide path hides the Timers popup`);
}
assert.match(read('src/ui/navigation/view_navigation.cpp'),
             /void viewNavigationClosePopups\(\) \{\n  timer_ui::hide_popup\(\);/,
             'tab switches and folder changes close the Timers popup');
// Review fixes of the alert: every new timer rings in full, the display may
// sleep after the ring limit, music does not hold the chime, a theme change
// rebuilds the card, a camera in full screen closes.
{
  const ui = read('src/ui/timers/timer_ui.cpp');
  assert.match(ui, /\/\/ the card of an earlier one stopped ringing\.\n  g_ring\.active = true;\n  g_ring\.started_ms = now;/);
  assert.match(ui, /if \(ringing && now - g_ring\.activity_ms >= kActivityRefreshMs\)/);
  assert.match(ui, /if \(speaker::chimePlaying\(\) \|\|\n\s+\(speaker::outputBusy\(\) && static_cast<int32_t>\(now - g_ring\.speech_until_ms\) < 0\)\)/);
  assert.match(ui, /g_ring\.first_round_end_ms = now;/, 'no millis() wrap on a silent alert');
  assert.match(ui, /if \(camera_popup_is_full_screen\(\)\) hide_camera_popup\(\);/);
  assert.match(read('src/ui/ui_manager.cpp'), /preloadPopups\(\);\n  timer_ui::theme_changed\(\);/);
  const timers = read('src/ui/timers/voice_timers.cpp');
  assert.match(timers, /if \(rangBefore\(s, timer\.id\)\) return;/);
  assert.match(timers, /elapsed_ms < timer\.left_s \* 1000u \+ kLocalFinishGraceMs/);
  assert.match(ui, /void service\(\) \{\n  if \(!voice_timers::supported\(\)\) return;\n  voice_timers::checkExpired\(\);/);
}
// A timer's own sound from the Bridge plays at the timer volume, unmuted.
{
  const request = read('src/audio/speaker_request.h');
  assert.match(request, /out->timer = !out->live && strncmp\(url_text, "tcp:\/\/", 6\) == 0 && timer\.is<bool>\(\) && timer\.as<bool>\(\);/);
  const spk = read('src/audio/speaker.cpp');
  assert.match(spk, /g_request->volume_override = command\.timer \? voice_timers::volumePercent\(\) \/ 100\.0f : -1\.0f;/);
  assert.match(spk, /g_volume_override = request\.volume_override >= 0\.0f \? clampVolume\(request\.volume_override\) : -1\.0f;/);
  assert.match(spk, /closeOutput\(\);\n  if \(g_volume_override >= 0\.0f\) \{\n    g_volume_override = -1\.0f;\n    applyVolumeIfChanged\(\);/);
}
const homeBar = read('src/ui/tabs/tiles/home_bar.cpp');
assert.match(homeBar, /timer_ui::add_head_pill\(bar, pill_right, g\.center_y, g\.disc, head_base\(\)\)/);

// Every visible text of the timer UI comes from the central Strings.
const ui = read('src/ui/timers/timer_ui.cpp');
const fields = ['timers_title', 'timer_unnamed', 'timer_done_title', 'timer_set_for', 'timer_done_too',
                'timer_stop', 'timer_add_minute', 'timer_paused_name', 'timer_start', 'timer_none', 'timer_minutes'];
const header = read('src/core/i18n/i18n.h');
for (const field of fields) {
  assert.match(header, new RegExp(`const char\\* ${field};`), `Strings has ${field}`);
  assert.match(ui, new RegExp(`tr\\(\\)\\.${field}`), `timer_ui shows ${field}`);
}
// Literals hold only format punctuation, never words.
for (const call of ui.matchAll(/lv_label_set_text(?:_fmt)?\([^,]+,\s*"([^"]*)"/g)) {
  assert.doesNotMatch(call[1].replace(/%[sdu]/g, ""), /[A-Za-z]/, `literal display text in timer_ui.cpp: "${call[1]}"`);
}
// The fields lead the schema, so every language's table starts with them.
const i18n = read('src/core/i18n/i18n.cpp');
const tables = [...i18n.matchAll(/static const Strings kStrings(\w+) = \{\n((?:\s+".*",\n){11})/g)];
assert.equal(tables.length, 8, 'all eight languages carry the timer texts');
for (const [, lang, block] of tables) {
  const values = [...block.matchAll(/"(.*)",/g)].map((m) => m[1]);
  assert.equal(values.length, 11, lang);
  assert.ok(values[3].includes('%s'), `${lang} timer_set_for keeps %s`);
  assert.ok(values[4].includes('%s'), `${lang} timer_done_too keeps %s`);
  assert.ok(values[10].includes('%d'), `${lang} timer_minutes keeps %d`);
  for (const value of values) assert.ok(value.length > 0, `${lang} has every timer text`);
}

// --- Host programs -----------------------------------------------------------
function findCompiler() {
  for (const candidate of [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}
const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: voice timer programs need a host C++ compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/voice-timers');
fs.mkdirSync(out, {recursive: true});
function compileAndRun(name, source) {
  const cpp = path.join(out, `${name}.cpp`);
  const exe = path.join(out, process.platform === 'win32' ? `${name}.exe` : name);
  fs.writeFileSync(cpp, source);
  const build = spawnSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', root, cpp, '-o', exe],
                          {encoding: 'utf8'});
  assert.equal(build.status, 0, `${name} did not compile:\n${build.stdout}${build.stderr}`);
  const run = spawnSync(exe, [], {encoding: 'utf8', maxBuffer: 64 * 1024 * 1024});
  assert.equal(run.status, 0, `${name} failed:\n${run.stdout}${run.stderr}`);
  return run.stdout;
}

// Chimes: lengths, the ring pattern's length, and every sample of each chime
// against the sample page's renderer (render() on the page).
const chimeOut = compileAndRun('chimes', String.raw`
#include "src/audio/timer_chime.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace timer_chime;
int main() {
  assert(lengthMs(0) == 1260 && lengthMs(1) == 760 && lengthMs(2) == 1800);
  assert(lengthMs(3) == 960 && lengthMs(4) == 2550 && lengthMs(5) == 0);
  ChimeSynth bad;
  assert(!bad.begin(kChimeCount, 1) && bad.done());
  assert(!bad.begin(0, 0));
  for (uint8_t c = 0; c < kChimeCount; ++c) {
    // Twice with the gap: both chimes plus the gap, in blocks of 100.
    ChimeSynth twice;
    assert(twice.begin(c, 2));
    std::vector<int16_t> all;
    int16_t block[100];
    size_t n;
    while ((n = twice.render(block, 100)) > 0) all.insert(all.end(), block, block + n);
    const size_t one = static_cast<size_t>(kSampleRate * lengthMs(c) / 1000.0 + 0.5);
    const size_t gap = static_cast<size_t>(kSampleRate * kRepeatGapMs / 1000.0 + 0.5);
    assert(all.size() == 2 * one + gap);
    for (size_t i = one; i < one + gap; ++i) assert(all[i] == 0);
    for (size_t i = 0; i < one; ++i) assert(all[i] == all[one + gap + i]);
    // One pass for the comparison.
    std::printf("C%u", static_cast<unsigned>(c));
    for (size_t i = 0; i < one; ++i) std::printf(" %d", all[i]);
    std::printf("\n");
  }
  return 0;
}
`);

const TONES = {
  bell: {partials: [[1, 1], [2, 0.35], [3, 0.12]], decayMs: 420},
  softbell: {partials: [[1, 1], [2.76, 0.25], [5.4, 0.08]], decayMs: 700},
  pure: {partials: [[1, 1], [2, 0.12]], decayMs: 380},
  beep: {partials: [[1, 1], [2, 0.08]], decayMs: 250},
  marimba: {partials: [[1, 1], [4, 0.1]], decayMs: 220},
};
const CHIMES = [
  {tone: 'pure', notes: [{f: 1046.5, t: 0, d: 500, g: 0.8}, {f: 1318.5, t: 180, d: 500, g: 0.8}, {f: 1568, t: 360, d: 900, g: 0.85}], level: 0.333},
  {tone: 'beep', notes: [0, 210, 420, 630].map((t) => ({f: 1046.5, t, d: 130, g: 0.55})), level: 0.97},
  {tone: 'softbell', notes: [{f: 880, t: 0, d: 1800, g: 0.9}], level: 0.565},
  {tone: 'marimba', notes: [{f: 523.3, t: 0, d: 350, g: 0.9}, {f: 659.3, t: 120, d: 350, g: 0.9}, {f: 784, t: 240, d: 350, g: 0.9}, {f: 1046.5, t: 360, d: 600, g: 0.9}], level: 0.335},
  {tone: 'bell', notes: [{f: 659.3, t: 0, d: 600, g: 0.85}, {f: 523.3, t: 450, d: 600, g: 0.85}, {f: 587.3, t: 900, d: 600, g: 0.85}, {f: 392, t: 1350, d: 1200, g: 0.9}], level: 0.552},
];
const SINE = new Float64Array(257);
for (let i = 0; i <= 256; i++) SINE[i] = Math.sin(2 * Math.PI * i / 256);
const sineAt = (phase) => {
  const x = phase * 256, i = Math.min(255, x | 0), fr = x - i;
  return SINE[i] + (SINE[i + 1] - SINE[i]) * fr;
};
function render(chime, rate) {
  const tone = TONES[chime.tone];
  const endMs = Math.max(...chime.notes.map((n) => n.t + n.d));
  const len = Math.round(rate * endMs / 1000);
  const mix = new Float64Array(len);
  const attack = Math.round(rate * 0.006), release = Math.round(rate * 0.025);
  const norm = tone.partials.reduce((s, p) => s + p[1], 0);
  for (const n of chime.notes) {
    const start = Math.round(rate * n.t / 1000), count = Math.round(rate * n.d / 1000);
    const phases = tone.partials.map(() => 0);
    const steps = tone.partials.map((p) => n.f * p[0] / rate);
    for (let k = 0; k < count && start + k < len; k++) {
      let env = 1;
      if (tone.decayMs > 0) env = Math.exp(-(k / rate) * 1000 / tone.decayMs);
      if (k < attack) env *= k / attack;
      if (k > count - release) env *= (count - k) / release;
      let s = 0;
      for (let p = 0; p < steps.length; p++) {
        if (steps[p] >= 0.5) continue;
        s += sineAt(phases[p]) * tone.partials[p][1];
        phases[p] += steps[p];
        if (phases[p] >= 1) phases[p] -= 1;
      }
      mix[start + k] += s / norm * env * n.g;
    }
  }
  return Array.from(mix, (v) => Math.round(Math.max(-1, Math.min(1, v * chime.level)) * 32767));
}
const lines = chimeOut.trim().split('\n');
assert.equal(lines.length, CHIMES.length);
lines.forEach((line, index) => {
  const samples = line.split(' ').slice(1).map(Number);
  const reference = render(CHIMES[index], 22050);
  assert.equal(samples.length, reference.length, `chime ${index} length`);
  let worst = 0, peak = 0;
  for (let i = 0; i < samples.length; i++) {
    worst = Math.max(worst, Math.abs(samples[i] - reference[i]));
    peak = Math.max(peak, Math.abs(samples[i]));
  }
  // Float math on the panel: a few LSB from the page's double precision.
  assert.ok(worst <= 48, `chime ${index} differs from the sample page by ${worst}`);
  assert.ok(peak > 8000, `chime ${index} is audible (peak ${peak})`);
  // Every chime keeps about 6 dB of headroom: none clips, none is much louder.
  assert.ok(peak < 0.56 * 32767 && peak > 0.42 * 32767, `chime ${index} peak ${peak} is not near -6 dBFS`);
  // No piercing notes: every fundamental stays at or below 1.6 kHz.
  assert.ok(CHIMES[index].notes.every((n) => n.f <= 1600), `chime ${index} has a note above 1.6 kHz`);
});

// The timer volume reaches the chime, and the media volume returns after it.
{
  const ui = read('src/ui/timers/timer_ui.cpp');
  assert.match(ui, /playChime\(sound, 2, voice_timers::volumePercent\(\) \/ 100\.0f\)/);
  const spk = read('src/audio/speaker.cpp');
  assert.match(spk, /g_volume_override = g_chime_volume\.load\(\);/);
  assert.match(spk, /closeOutput\(\);\n  g_volume_override = -1\.0f;\n  applyVolumeIfChanged\(\);/);
}

// Contract.
compileAndRun('contract', String.raw`
#include "src/audio/voice_timer_contract.h"
#include <cassert>
#include <cstring>
#include <string>
using namespace voice_timer;
static Op op(const char* json) { return opOf(json, strlen(json)); }
int main() {
  assert(op("{\"v\":1,\"op\":\"timers\",\"timers\":[]}") == Op::Timers);
  assert(op("{\"v\":1,\"op\":\"timer_done\",\"id\":\"A\"}") == Op::Done);
  assert(op("{\"v\":1,\"op\":\"timer_sound\",\"sound\":\"off\"}") == Op::Sound);
  assert(op("{\"v\":1,\"op\":\"start\",\"id\":\"0123456789abcdef\"}") == Op::None);
  assert(op("{\"v\":1,\"op\":\"wake_words\",\"active\":[]}") == Op::None);
  assert(op("not json") == Op::None);

  const char* list =
      "{\"v\":1,\"op\":\"timers\",\"timers\":["
      "{\"id\":\"01JB8X3K9Q2W4E6R8T0Y1U3I5O\",\"total\":600,\"left\":272,\"active\":true,\"name\":\"Pasta\"},"
      "{\"id\":\"B2\",\"total\":300,\"left\":300,\"active\":false},"
      "{\"id\":\"bad-id\",\"total\":60,\"left\":60,\"active\":true},"
      "{\"id\":\"C3\",\"total\":90000,\"left\":95000,\"active\":true,\"name\":\"Caf\\u00e9 \\\"x\\\" \\ud83c\\udf5d\"},"
      "{\"id\":\"D4\",\"total\":60,\"left\":30,\"active\":true,\"name\":\"\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\\u00e4\"}"
      "]}";
  TimerList timers;
  assert(parseTimers(list, strlen(list), &timers));
  assert(timers.count == 4);
  assert(std::string(timers.timers[0].id) == "01JB8X3K9Q2W4E6R8T0Y1U3I5O");
  assert(std::string(timers.timers[0].name) == "Pasta");
  assert(timers.timers[0].total_s == 600 && timers.timers[0].left_s == 272 && timers.timers[0].active);
  assert(timers.timers[1].name[0] == '\0' && !timers.timers[1].active);
  assert(std::string(timers.timers[2].id) == "C3");
  // Clamped to 24 h; UTF-8 from \u escapes and a surrogate pair.
  assert(timers.timers[2].left_s == kMaxSeconds && timers.timers[2].total_s == kMaxSeconds);
  assert(std::string(timers.timers[2].name) == "Caf\xC3\xA9 \"x\" \xF0\x9F\x8D\x9D");
  // Cut at a whole character: 24 two-byte letters fit in 48 bytes.
  assert(strlen(timers.timers[3].name) == 48);
  // Invalid raw UTF-8 becomes '?': a stray continuation byte, a 0xF8 byte and
  // a lead byte without its continuation.
  {
    const char* bad = "{\"op\":\"timers\",\"timers\":[{\"id\":\"A1\",\"total\":60,\"left\":60,\"active\":true,"
                      "\"name\":\"a\x80" "b\xF8" "c\xC3" "x\xC3\xA4\"}]}";
    TimerList broken;
    assert(parseTimers(bad, strlen(bad), &broken) && broken.count == 1);
    assert(std::string(broken.timers[0].name) == "a?b?c?x\xC3\xA4");
  }
  TimerList none;
  assert(!parseTimers("{\"op\":\"timers\"}", 15, &none));

  // Soonest first: running by the time left, paused after them, ties stable.
  {
    TimerList order{};
    const struct { const char* id; uint32_t left; bool active; } in[] = {
        {"P1", 30, false}, {"A1", 392, true}, {"A2", 354, true}, {"P2", 10, false}, {"A3", 888, true}, {"A4", 354, true}};
    for (const auto& t : in) {
      Timer& timer = order.timers[order.count++];
      snprintf(timer.id, sizeof(timer.id), "%s", t.id);
      timer.left_s = t.left;
      timer.active = t.active;
    }
    sortSoonest(&order);
    const char* want[] = {"A2", "A4", "A1", "A3", "P2", "P1"};
    for (int i = 0; i < 6; ++i) assert(std::string(order.timers[i].id) == want[i]);
    sortSoonest(nullptr);
  }

  // At most kMaxTimers.
  std::string many = "{\"op\":\"timers\",\"timers\":[";
  for (int i = 0; i < 12; ++i) many += std::string(i ? "," : "") + "{\"id\":\"T" + std::to_string(i) + "\",\"total\":60,\"left\":60}";
  many += "]}";
  assert(parseTimers(many.c_str(), many.size(), &timers) && timers.count == kMaxTimers);

  Timer done;
  const char* finished = "{\"v\":1,\"op\":\"timer_done\",\"id\":\"A1\",\"total\":600,\"name\":\"Eggs\"}";
  assert(parseDone(finished, strlen(finished), &done));
  assert(std::string(done.name) == "Eggs" && done.total_s == 600 && done.left_s == 0 && !done.active);
  assert(!parseDone("{\"op\":\"timer_done\",\"id\":\"\"}", 30, &done));

  assert(parseSound("{\"op\":\"timer_sound\",\"sound\":\"westminster\"}", 43) == 4);
  assert(parseSound("{\"op\":\"timer_sound\",\"sound\":\"bridge\"}", 38) == kSoundBridge);
  assert(parseSound("{\"op\":\"timer_sound\",\"sound\":\"off\"}", 35) == kSoundOff);
  assert(parseSound("{\"op\":\"timer_sound\",\"sound\":\"loud\"}", 36) == -1);
  {
    auto volume = [](const char* text) { return parseSoundVolume(text, strlen(text)); };
    assert(volume("{\"op\":\"timer_sound\",\"sound\":\"marimba\",\"volume\":45}") == 45);
    assert(volume("{\"op\":\"timer_sound\",\"sound\":\"marimba\",\"volume\":0}") == 0);
    assert(volume("{\"op\":\"timer_sound\",\"sound\":\"marimba\",\"volume\":250}") == 100);
    assert(volume("{\"op\":\"timer_sound\",\"sound\":\"marimba\",\"volume\":-5}") == -1);
    assert(volume("{\"op\":\"timer_sound\",\"sound\":\"marimba\"}") == -1);
  }

  Timer running = timers.timers[0];
  running.left_s = 272;
  running.active = true;
  assert(secondsLeft(running, 0) == 272 && secondsLeft(running, 2999) == 270);
  assert(secondsLeft(running, 400000) == 0);
  running.active = false;
  assert(secondsLeft(running, 100000) == 272);

  char text[16];
  formatClock(272, text, sizeof(text));
  assert(std::string(text) == "4:32");
  formatClock(725, text, sizeof(text));
  assert(std::string(text) == "12:05");
  formatClock(3723, text, sizeof(text));
  assert(std::string(text) == "1:02:03");
  formatClock(0, text, sizeof(text));
  assert(std::string(text) == "0:00");

  char out[200];
  assert(buildStart(300, nullptr, out, sizeof(out)) && std::string(out) == "{\"v\":1,\"op\":\"start\",\"seconds\":300}");
  assert(buildStart(60, "Pa\"sta\\", out, sizeof(out)) &&
         std::string(out) == "{\"v\":1,\"op\":\"start\",\"seconds\":60,\"name\":\"Pa\\\"sta\\\\\"}");
  assert(buildStart(0, nullptr, out, sizeof(out)) == 0);
  assert(buildStart(kMaxSeconds + 1, nullptr, out, sizeof(out)) == 0);
  assert(buildAction("pause", "A1", out, sizeof(out)) && std::string(out) == "{\"v\":1,\"op\":\"pause\",\"id\":\"A1\"}");
  assert(buildAction("speak", "A1", out, sizeof(out)));
  assert(buildAction("play_sound", "A1", out, sizeof(out)));
  assert(buildAction("explode", "A1", out, sizeof(out)) == 0);
  assert(buildAction("cancel", "A-1", out, sizeof(out)) == 0);
  assert(buildAdd("A1", 60, out, sizeof(out)) && std::string(out) == "{\"v\":1,\"op\":\"add\",\"id\":\"A1\",\"seconds\":60}");
  assert(buildAdd("A1", 0, out, sizeof(out)) == 0);
  assert(buildStart(300, nullptr, out, 10) == 0);
  return 0;
}
`);

console.log('voice timers: OK');
