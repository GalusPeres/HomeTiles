// A touch on the screensaver pauses whatever composes a whole frame (the
// next slide, a new Home Assistant picture, the pixel shift) until the screen
// was left alone for one slide duration, at least the old 1.5 s settle: a
// slide change 1.5 s after a touch blocked the next touches for about a
// second (user 2026-11-02, V2 b648: "Step lvgl took 900 ms" per slide).
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const source = readRepoFile('src/ui/screensaver/image_screensaver.cpp').replace(/\r\n/g, '\n');
const fn = name => cppFunctionDefinitions(source).find(f => f.name === name)?.source ?? '';

const quiet = fn('quiet_before_frame_ms');
assert.match(quiet, /static_cast<uint32_t>\(screensaverConfig\.get\(\)\.duration_seconds\) \* 1000U;/, 'one slide duration');
assert.match(quiet, /return duration > kInteractionSettleBeforeSlideMs \? duration : kInteractionSettleBeforeSlideMs;/, 'never shorter than the settle');

const timer = fn('global_screensaver_timer_cb');
assert.match(timer, /const uint32_t since_activity =\s*static_cast<uint32_t>\(now - displayManager\.getLastActivityTime\(\)\);\n  const uint32_t quiet_ms = quiet_before_frame_ms\(\);/);
assert.match(timer, /if \(g_ha_picture_arrived\) \{[\s\S]*?if \(since_activity >= quiet_ms\) \{/, 'a new picture waits too');
assert.match(timer, /pixel_shift_due\(st, screensaver_pixel_shift::kMaxIntervalMs\) &&\s*since_activity >= quiet_ms\)/, 'the pixel shift waits too');
assert.match(timer, /if \(since_activity < quiet_ms\) \{\s*st->next_wallpaper_ms = now \+ \(quiet_ms - since_activity\);\s*return;\s*\}/,
  'a due slide moves to the end of the quiet time');
assert.doesNotMatch(timer, /kInteractionSettleBeforeSlideMs/, 'no frame after the short settle alone');

// The rule as numbers: a slide due 3 s after a touch with a 15 s duration
// comes 15 s after the touch; with a 1 s duration after the 1.5 s settle.
const quietMs = seconds => Math.max(seconds * 1000, 1500);
const nextSlide = (touch, due, seconds) => {
  const since = due - touch;
  return since < quietMs(seconds) ? due + (quietMs(seconds) - since) : due;
};
assert.equal(nextSlide(0, 3000, 15), 15000);
assert.equal(nextSlide(0, 1000, 1), 1500);
assert.equal(nextSlide(0, 20000, 15), 20000);

console.log('Screensaver pause on touch: slides, pictures and the pixel shift wait one slide duration after a touch');
