// Song change on a Media card with "From cover": the cover color fades from
// the old to the new color in steps that each run the unchanged apply_cover.
// The harness drives the production fade code with a small animation engine
// and checks that the last color is exactly the new one, that no step shows
// "no color" (grey), that a new song during a fade starts from the color
// shown, and that every other change still applies at once.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const source = read('src/tiles/runtime/tile_icon_source.cpp');

const begin = source.indexOf('constexpr uint32_t kCoverFadeMs');
const end = source.indexOf('String layer_entity(');
assert.ok(begin > 0 && end > begin, 'fade block precedes layer_entity');
const fade = source.slice(begin, end);
const definitions = cppFunctionDefinitions(source);
const setCover = (definitions.find(f => f.name === 'tile_icon_source::set_cover_color') ??
  definitions.find(f => f.name === 'set_cover_color')).source;

// The fade only feeds colors into the existing pipeline.
assert.match(fade, /lv_obj_set_style_bg_color\(card, lv_color_hex\(rgb\), kCoverStore\);\n  apply_cover\(card\);/);
assert.doesNotMatch(fade, /set_tile_tint|force_icon_color|apply_card_background/, 'no own color math');
assert.match(setCover, /if \(known && had && start_cover_fade\(card, stored, rgb\)\) return;/);

const harness = `
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "src/tiles/config/tile_tint.h"

struct lv_obj_t { uint32_t cover = 0; bool has_cover = false; bool visible = true; bool icon = true;
                  uint8_t tile = 20; std::vector<long> applied; };
struct lv_color_t { uint32_t v; };
typedef int lv_style_selector_t;
constexpr int LV_STYLE_BG_COLOR = 1;
constexpr lv_style_selector_t kCoverStore = 7;
lv_color_t lv_color_hex(uint32_t v) { return lv_color_t{v}; }
void lv_obj_set_style_bg_color(lv_obj_t* card, lv_color_t c, lv_style_selector_t) { card->cover = c.v; card->has_cover = true; }
void lv_obj_remove_local_style_prop(lv_obj_t* card, int, lv_style_selector_t) { card->has_cover = false; }
bool lv_obj_is_visible(const lv_obj_t* card) { return card->visible; }

struct lv_anim_t;
typedef void (*lv_anim_exec_xcb_t)(void*, int32_t);
typedef void (*lv_anim_cb_t)(lv_anim_t*);
typedef int32_t (*lv_anim_path_cb_t)(const lv_anim_t*);
struct lv_anim_t { void* var = nullptr; lv_anim_exec_xcb_t exec_cb = nullptr; int32_t start = 0, end = 0;
                   uint32_t duration = 0, time = 0; lv_anim_cb_t completed_cb = nullptr, deleted_cb = nullptr; };
int32_t lv_anim_path_ease_in_out(const lv_anim_t*) { return 0; }
void lv_anim_init(lv_anim_t* a) { *a = lv_anim_t{}; }
void lv_anim_set_var(lv_anim_t* a, void* v) { a->var = v; }
void lv_anim_set_exec_cb(lv_anim_t* a, lv_anim_exec_xcb_t cb) { a->exec_cb = cb; }
void lv_anim_set_values(lv_anim_t* a, int32_t s, int32_t e) { a->start = s; a->end = e; }
void lv_anim_set_duration(lv_anim_t* a, uint32_t d) { a->duration = d; }
void lv_anim_set_path_cb(lv_anim_t*, lv_anim_path_cb_t) {}
void lv_anim_set_completed_cb(lv_anim_t* a, lv_anim_cb_t cb) { a->completed_cb = cb; }
void lv_anim_set_deleted_cb(lv_anim_t* a, lv_anim_cb_t cb) { a->deleted_cb = cb; }
std::vector<lv_anim_t*> g_anims;
bool g_fail_start = false;
lv_anim_t* lv_anim_start(const lv_anim_t* a) {
  if (g_fail_start) return nullptr;
  g_anims.push_back(new lv_anim_t(*a));
  return g_anims.back();
}
bool lv_anim_delete(void* var, lv_anim_exec_xcb_t exec_cb) {
  bool any = false;
  for (size_t i = 0; i < g_anims.size();) {
    lv_anim_t* a = g_anims[i];
    if ((a->var == var || !var) && (a->exec_cb == exec_cb || !exec_cb)) {
      g_anims.erase(g_anims.begin() + i);
      if (a->deleted_cb) a->deleted_cb(a);
      delete a;
      any = true;
    } else {
      ++i;
    }
  }
  return any;
}
// Like lv_anim's timer; the last value is one short to prove completed_cb.
void tick(uint32_t ms) {
  for (size_t i = 0; i < g_anims.size();) {
    lv_anim_t* a = g_anims[i];
    a->time += ms;
    if (a->time < a->duration) {
      a->exec_cb(a->var, a->start + (a->end - a->start) * static_cast<int32_t>(a->time) / static_cast<int32_t>(a->duration));
      ++i;
      continue;
    }
    a->exec_cb(a->var, a->end - 1);
    g_anims.erase(g_anims.begin() + i);
    if (a->completed_cb) a->completed_cb(a);
    if (a->deleted_cb) a->deleted_cb(a);
    delete a;
  }
}

bool cover_color(lv_obj_t* card, uint32_t& rgb) { rgb = card->cover; return card->has_cover; }
bool cover_icon(lv_obj_t* card) { return card->icon; }
uint8_t cover_tile(lv_obj_t* card) { return card->tile; }
// Records what the unchanged pipeline would show: the color or -1 (none).
void apply_cover(lv_obj_t* card) {
  card->applied.push_back(card->has_cover && tile_tint::has_hue(card->cover) ? static_cast<long>(card->cover) : -1L);
}

${fade}
${setCover.replace(/^void\s+(?:tile_icon_source::)?set_cover_color/, 'void set_cover_color')}

void check(bool ok, const char* what) {
  if (!ok) { std::printf("FAIL %s\\n", what); std::exit(1); }
}
void run_out() { for (int i = 0; i < 40 && !g_anims.empty(); ++i) tick(17); }
bool no_grey(const lv_obj_t& card) { for (long c : card.applied) if (c < 0) return false; return true; }

int main() {
  lv_obj_t card;
  set_cover_color(&card, true, 0xFF0000);
  check(g_anims.empty() && card.applied.size() == 1 && card.applied[0] == 0xFF0000, "first cover applies at once");
  card.applied.clear();

  set_cover_color(&card, true, 0x0000FF);
  check(g_anims.size() == 1 && card.applied.empty() && card.cover == 0xFF0000, "song change starts a fade");
  run_out();
  check(g_anims.empty() && card.cover == 0x0000FF && card.applied.back() == 0x0000FF, "fade ends exactly on the new color");
  check(card.applied.size() > 5 && no_grey(card), "fade shows several real colors");

  // Dark red to teal crosses grey (100,100,100): that step is skipped.
  card.applied.clear();
  set_cover_color(&card, true, 0xC80000);
  run_out();
  card.applied.clear();
  set_cover_color(&card, true, 0x00C8C8);
  for (int i = 0; i < 350; ++i) tick(1);
  run_out();
  check(no_grey(card) && card.cover == 0x00C8C8, "grey in-between color never shows");

  // A new song during a fade starts from the shown color; the same color
  // again does not restart it.
  card.applied.clear();
  set_cover_color(&card, true, 0x0000FF);
  tick(170);
  const uint32_t shown = card.cover;
  check(shown != 0x00C8C8 && shown != 0x0000FF, "halfway color shown");
  set_cover_color(&card, true, 0x0000FF);
  check(g_anims.size() == 1 && g_anims[0]->time == 170, "same target keeps the fade");
  set_cover_color(&card, true, 0x00FF00);
  check(g_anims.size() == 1 && g_anims[0]->time == 0 && card.cover == shown, "new target starts from the shown color");
  run_out();
  check(card.cover == 0x00FF00 && no_grey(card), "retargeted fade ends on the newest color");

  // Grey, no cover, hidden cards, cards without "From cover": at once.
  card.applied.clear();
  set_cover_color(&card, true, 0x808080);
  check(g_anims.empty() && card.cover == 0x808080 && card.applied.size() == 1 && card.applied[0] == -1, "grey applies at once");
  set_cover_color(&card, true, 0xFF0000);
  check(g_anims.empty() && card.applied.back() == 0xFF0000, "from grey applies at once");
  set_cover_color(&card, true, 0x0000FF);
  tick(100);
  set_cover_color(&card, false, 0);
  check(g_anims.empty() && !card.has_cover && card.applied.back() == -1, "no cover stops the fade at once");
  set_cover_color(&card, true, 0xFF0000);
  card.visible = false;
  set_cover_color(&card, true, 0x0000FF);
  check(g_anims.empty() && card.cover == 0x0000FF, "hidden card applies at once");
  card.visible = true;
  card.icon = false;
  card.tile = 0;
  set_cover_color(&card, true, 0xFF0000);
  check(g_anims.empty() && card.cover == 0xFF0000, "card without From cover only keeps the value");
  card.icon = true;
  card.tile = 20;
  g_fail_start = true;
  set_cover_color(&card, true, 0x0000FF);
  check(g_anims.empty() && card.cover == 0x0000FF, "failed animation applies at once");
  g_fail_start = false;

  // Deleted cards free their slot; more fades than slots apply at once.
  lv_obj_t cards[9];
  for (lv_obj_t& other : cards) set_cover_color(&other, true, 0xFF0000);
  for (lv_obj_t& other : cards) set_cover_color(&other, true, 0x0000FF);
  check(g_anims.size() == 8 && cards[8].cover == 0x0000FF, "ninth fade applies at once");
  lv_anim_delete(&cards[0], nullptr);
  set_cover_color(&cards[8], true, 0x00FF00);
  check(g_anims.size() == 8, "a deleted card's slot is free again");
  run_out();
  for (int i = 1; i < 8; ++i) check(cards[i].cover == 0x0000FF, "other fades finish");
  check(cards[8].cover == 0x00FF00, "reused slot finishes");
  std::printf("ok\\n");
  return 0;
}
`;

const output = compileAndRun({label: 'Media cover fade', harness});
if (output !== null) {
  assert.equal(output.trim(), 'ok');
  console.log('Media "From cover" fade: exact final color, no grey step, retarget and instant paths pass.');
}
