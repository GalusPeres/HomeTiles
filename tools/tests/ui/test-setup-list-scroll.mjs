// Issue #41 (Waveshare 7B, 1024 x 600): on a new panel's first setup the
// language step's list is taller than the card and scrolls in itself, but a
// drag only scrolled from its first two rows. LVGL scrolls what a touch lands
// on or its parents; those two are tap rows, the rows with a switch below
// them are no input, and the group had none either, so the touch went to the
// card around it. The real setup on host LVGL (tools/settings-preview.mjs):
// a finger on the name of the third row drags 200 px up, the list reaches
// its end.
import assert from 'node:assert/strict';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {readRepoFile, repoRoot} from '../../lib/admin-source.mjs';

const setup = readRepoFile('src/ui/tabs/settings/setup_screen.cpp').replace(/\r\n/g, '\n');
const takesInput = name => new RegExp(String.raw`lv_obj_add_flag\(${name}, static_cast<lv_obj_flag_t>\(LV_OBJ_FLAG_SCROLLABLE \| LV_OBJ_FLAG_CLICKABLE\)\);\s*lv_obj_set_scroll_dir\(${name}, LV_DIR_VER\);`);
assert.match(setup, takesInput('group'), 'the language step\'s group takes the touch it scrolls with');
assert.match(setup, takesInput('list'), 'the network list too');
assert.doesNotMatch(setup, /lv_obj_add_flag\(\w+, LV_OBJ_FLAG_SCROLLABLE\);/, 'no list scrolls without its own input');

let checked = 0;
for (const panel of ['p1024', 'p43']) {
  const run = spawnSync(process.execPath, [path.join(repoRoot, 'tools/settings-preview.mjs'), panel, '--page=setup', '--step=0', '--no-mockup', '--drag=up'],
    {encoding: 'utf8', timeout: 240000});
  assert.equal(run.status, 0, run.stdout + run.stderr);
  if (/^SKIP:/m.test(run.stdout)) {
    console.log(run.stdout.trim());
    process.exit(0);
  }
  const scroll = run.stdout.match(/page scroll (\d+) of (\d+)/);
  assert.ok(scroll, `${panel}: the drag reports the list's scroll: ${run.stdout.slice(0, 400)}`);
  const [moved, end] = [Number(scroll[1]), Number(scroll[2])];
  assert.ok(end > 0, `${panel}: the first setup's language list is taller than the card (else nothing to check): ${end}`);
  assert.equal(moved, end, `${panel}: a finger on a row with a switch scrolls the list to its end: ${moved} of ${end}`);
  checked++;
}
console.log(`Setup list scroll: ${checked} short landscape panels follow a finger on a row with a switch`);
