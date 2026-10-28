// A page turn is drawn where the panel shows it. b582 drew the first frame
// after a page turn into the framebuffer the panel does not scan and showed
// it as a whole: nothing happened until the whole screen was drawn, and the
// panel felt unresponsive after every swipe (user 2026-10-28), like b128's UI
// frame swap. The presenter keeps the hidden frame calls unused.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const tab = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
assert.ok(!tab.includes('displayBeginHiddenFrame') && !tab.includes('displayEndHiddenFrame'),
  'page turns never draw out of sight');
assert.match(tab, /tiles_process_reload_requests\(\);\s*if \(g_folder_switch_pending\) return;[\s\S]*?yield\(\);\s*lv_refr_now\(display\);\s*yield\(\);/,
  'the page turn is drawn at once where it shows');
console.log('Page turn: drawn where it shows (the hidden frame of b582 felt unresponsive)');
