#pragma once

// Voice timers on the panel (voice_timer_contract.h): the timer list the
// Bridge sends, the finished timers waiting for their alert, the chosen
// sound, and the requests the panel's controls send to the Bridge. No LVGL
// here; timer_ui.h shows them.
//
// Threading: everything runs on the Arduino loop task (the MQTT inbound
// drainer and the sealed command channel both deliver there).

#include <stddef.h>
#include <stdint.h>

#include "src/audio/voice_timer_contract.h"

namespace voice_timers {

// Exact-profile capability: speaker boards (the timers come through the
// Assist satellite, which needs the speaker and microphone).
bool supported();

// A timer message on the Assist command topic; false for anything else
// (the microphone commands go on to assist_contract.h).
bool handleCommandPayload(const uint8_t* payload, size_t length);

// Bumped by every list change (not by the countdown).
uint32_t generation();
uint8_t count();
// Timer i of the list, soonest first (voice_timer::sortSoonest), with its
// seconds left now; false past the end.
bool timerAt(uint8_t index, voice_timer::Timer* out, uint32_t* left_s);
// The first timer: the running one with the fewest seconds left, else the
// first paused one; false without timers.
bool nextTimer(voice_timer::Timer* out, uint32_t* left_s);

// A running timer the Bridge never reported finished (lost link) finishes
// on the panel's own count a few seconds after 0:00. Rate-limited; call from
// the awake and the sleep loop.
void checkExpired();

// Finished timers, oldest first, until the alert takes them. Each timer
// rings once, however often its timer_done arrives.
bool takeFinished(voice_timer::Timer* out);
bool hasFinished();

// The sound for finished timers (voice_timer::kSoundIds index).
uint8_t sound();
// The timer volume in percent (0 = silent).
uint8_t volumePercent();

// Requests to the Bridge ({base}/stat/assist_timer); false when nothing
// could be queued (no Bridge connection, invalid argument).
bool requestStart(uint32_t seconds, const char* name = nullptr);
// op: pause, resume, cancel, speak or play_sound.
bool requestAction(const char* op, const char* id);
bool requestAdd(const char* id, uint32_t seconds);

}  // namespace voice_timers
