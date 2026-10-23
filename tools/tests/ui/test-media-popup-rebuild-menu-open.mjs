// The Media popup stayed empty after a song change (user 2026-10-23, V2,
// v0.9.4): with the speakers list open, Spotify started a song with another
// cover, the card took its color and the player was built again. A player
// built while a menu list was open was created hidden, a leftover of the
// popup whose lists replaced the player; since the menu box lies over the
// player nothing showed it again, so the card stayed empty after the menu
// closed and after reopening, until the next rebuild. The player is always
// shown now; only the menu box opens and closes over it.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const popup = readRepoFile('src/ui/popups/media/media_popup.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(popup).filter(f => f.name === name);
  assert.equal(found.length, 1, name);
  return found[0].source;
};

const player = fn('build_player');
assert.match(player, /ctx->player = plain\(ctx->root\);\s*lv_obj_set_size\(ctx->player, W, H\);/);
assert.ok(!/ctx->view != MediaView::Player\) lv_obj_add_flag\(ctx->player/.test(player),
  'a player built while a menu list is open is not hidden');
// Nothing hides or shows the player by the open view: the menu box covers it.
assert.ok(!/(lv_obj_add_flag|lv_obj_remove_flag|lv_obj_clear_flag|set_shown)\(ctx->player\b/.test(popup),
  'the player keeps its visibility');
// The rebuild during an open list rebuilds that list over the new player.
assert.match(fn('apply_init_to_context'),
  /build_player\(ctx, init\.cover_centered\);\s*if \(ctx->view != MediaView::Player\) rebuild_list\(ctx, true\);/);
console.log('Media popup: a player rebuilt while the menu is open stays visible');
