// Host test for src/ui/shared/command_pacer.h (GitHub issue #11): slider
// gestures follow Home Assistant's own color temperature slider
// (throttle 500 ms, leading and trailing): a tap sends one command on
// release, the first drag movement sends at once, later ones at most every
// 500 ms, and the final value always goes out but never within 500 ms of the
// previous command. The harness replays finger timelines through the same
// flow as the Light popup and the Switch dimmer (press = begin_gesture, drag
// movement = live publish, release = paced commit) and through the previous
// behavior, so the difference for ordinary lights stays visible.
import assert from 'node:assert/strict';

import {compileAndRun} from '../../lib/cpp-host.mjs';

const harness = String.raw`
#include "src/ui/shared/command_pacer.h"

#include <cstdio>
#include <vector>

struct Event { uint32_t t; int type; int value; };  // 0 press, 1 move, 2 release
struct Send { uint32_t t; int value; };
enum { PRESS, MOVE, RELEASE };

// Current flow (light_popup.cpp schedule_live_publish / commit_paced).
static std::vector<Send> run_paced(const std::vector<Event>& events, int start,
                                   uint32_t end, uint32_t previous_send = 0,
                                   bool has_previous = false) {
  command_pacer::Pacer pacer;
  if (has_previous) pacer.sent(previous_send, 0);
  std::vector<Send> out;
  int value = start;
  bool dragging = false, live_pending = false, final_pending = false;
  uint32_t live_due = 0, final_due = 0;
  size_t next = 0;
  auto send = [&](uint32_t t) {
    out.push_back({t, value});
    pacer.sent(t, static_cast<uint32_t>(value));
  };
  for (uint32_t t = 0; t <= end; ++t) {
    if (live_pending && t >= live_due) {
      live_pending = false;
      if (dragging) send(t);
    }
    if (final_pending && t >= final_due) {
      final_pending = false;
      send(t);
    }
    while (next < events.size() && events[next].t == t) {
      const Event& e = events[next++];
      const bool changed = e.value != value;
      value = e.value;
      if (e.type == PRESS) {
        // The press jumps to the finger but sends nothing (a tap sends on
        // release); moves stand for drags past the 10 px threshold.
        dragging = true;
        pacer.begin_gesture();
        continue;
      }
      if (e.type != RELEASE) {
        if (!changed) continue;
        const uint32_t wait = pacer.wait(t);
        if (wait == 0) {
          live_pending = false;
          send(t);
        } else if (!live_pending) {
          live_pending = true;
          live_due = t + wait;
        }
        continue;
      }
      dragging = false;
      live_pending = false;
      const bool repeat = pacer.final_redundant(static_cast<uint32_t>(value));
      pacer.end_gesture();
      if (repeat) continue;
      const uint32_t wait = pacer.wait(t);
      if (wait == 0) {
        final_pending = false;
        send(t);
      } else {
        final_pending = true;
        final_due = t + wait;
      }
    }
  }
  return out;
}

// Previous flow: immediate command on press, 500 ms live throttle from the
// last command, unconditional command on release.
static std::vector<Send> run_previous(const std::vector<Event>& events,
                                      int start, uint32_t end) {
  std::vector<Send> out;
  int value = start;
  bool dragging = false, live_pending = false, has_last = false;
  uint32_t last = 0, live_due = 0;
  size_t next = 0;
  auto send = [&](uint32_t t) {
    out.push_back({t, value});
    last = t;
    has_last = true;
  };
  for (uint32_t t = 0; t <= end; ++t) {
    if (live_pending && t >= live_due) {
      live_pending = false;
      if (dragging) send(t);
    }
    while (next < events.size() && events[next].t == t) {
      const Event& e = events[next++];
      const bool changed = e.value != value;
      value = e.value;
      if (e.type == PRESS) dragging = true;
      if (e.type != RELEASE) {
        if (!changed) continue;
        if (!has_last || t - last >= 500) {
          live_pending = false;
          send(t);
        } else if (!live_pending) {
          live_pending = true;
          live_due = last + 500;
        }
        continue;
      }
      dragging = false;
      live_pending = false;
      send(t);
    }
  }
  return out;
}

static void print(const char* name, const char* flow, const std::vector<Send>& s) {
  std::printf("%s %s", name, flow);
  for (const Send& x : s) std::printf(" %u:%d", x.t, x.value);
  std::printf("\n");
}

static std::vector<Event> drag(uint32_t release_at, uint32_t stop_at, int from) {
  std::vector<Event> e{{0, PRESS, from}};
  int value = from;
  for (uint32_t t = 50; t < release_at; t += 50) {
    if (t <= stop_at) ++value;
    e.push_back({t, MOVE, value});
  }
  e.push_back({release_at, RELEASE, value});
  return e;
}

int main() {
  struct Case { const char* name; std::vector<Event> events; int start; };
  std::vector<Case> cases;
  cases.push_back({"tap", {{0, PRESS, 40}, {100, RELEASE, 40}}, 70});
  cases.push_back({"tap_same_value", {{0, PRESS, 70}, {90, RELEASE, 70}}, 70});
  cases.push_back({"quick_swipe", {{0, PRESS, 10}, {100, MOVE, 20},
                                   {200, MOVE, 40}, {300, MOVE, 60},
                                   {350, RELEASE, 60}}, 70});
  cases.push_back({"long_drag_release_moving", drag(1420, 1420, 10), 70});
  cases.push_back({"long_drag_release_still", drag(1500, 800, 10), 70});
  for (const Case& c : cases) {
    print(c.name, "paced", run_paced(c.events, c.start, 3000));
    print(c.name, "previous", run_previous(c.events, c.start, 3000));
  }
  // A tap 200 ms after the previous command (another control) waits for
  // the gap instead of overlapping it.
  print("tap_after_command", "paced",
        run_paced({{1000, PRESS, 40}, {1100, RELEASE, 40}}, 70, 3000, 900, true));
  return 0;
}
`;

const output = compileAndRun({label: 'Command pacer', harness});
if (output === null) process.exit(0);

const runs = new Map();
for (const line of output.trim().split('\n')) {
  const [name, flow, ...sends] = line.trim().split(/\s+/);
  runs.set(`${name}/${flow}`, sends.map(item => {
    const [t, value] = item.split(':').map(Number);
    return {t, value};
  }));
}
const get = key => {
  assert.ok(runs.has(key), key);
  return runs.get(key);
};
const gaps = sends => sends.slice(1).map((send, i) => send.t - sends[i].t);

// A tap sends exactly one command, on release, with the tapped value.
assert.deepEqual(get('tap/paced'), [{t: 100, value: 40}]);
assert.deepEqual(get('tap/previous'), [{t: 0, value: 40}, {t: 100, value: 40}]);
assert.deepEqual(get('tap_same_value/paced'), [{t: 90, value: 70}]);

// A quick swipe: the first movement at once (leading edge), the final value
// one interval later (trailing edge), like Home Assistant.
assert.deepEqual(get('quick_swipe/paced'), [{t: 100, value: 20}, {t: 600, value: 60}]);
assert.deepEqual(get('quick_swipe/previous'), [{t: 0, value: 10}, {t: 350, value: 60}]);

for (const name of ['long_drag_release_moving', 'long_drag_release_still']) {
  const paced = get(`${name}/paced`);
  const previous = get(`${name}/previous`);
  // Same end value as before, and never two commands within 500 ms.
  assert.equal(paced.at(-1).value, previous.at(-1).value, name);
  for (const gap of gaps(paced)) assert.ok(gap >= 500, `${name} gap ${gap}`);
  // The press sends nothing; the first movement sends at once.
  assert.equal(paced[0].t, 50, name);
  // The final value is at most one interval later than before (earlier when
  // the finger rested and a live command already sent it).
  const delay = paced.at(-1).t - previous.at(-1).t;
  assert.ok(delay <= 500, `${name} final delay ${delay}`);
}
// Released while still: the final value was already sent live, no repeat.
assert.deepEqual(get('long_drag_release_still/paced'),
                 [{t: 50, value: 11}, {t: 550, value: 20}, {t: 1050, value: 26}]);
// Released while moving: the final value one interval after the last live one.
assert.deepEqual(get('long_drag_release_moving/paced').at(-1), {t: 1550, value: 38});

// A tap right after another command keeps the 500 ms gap.
assert.deepEqual(get('tap_after_command/paced'), [{t: 1400, value: 40}]);

console.log('Command pacer tests passed.');
