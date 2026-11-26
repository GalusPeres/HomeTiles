// The screensaver clock can sit on one line (time beside the date) so the
// overlay fits the very top of the screen: a config flag, the panel's row
// layout (ClockWidgetConfig.horizontal), the Web Admin checkbox with its
// central label and the preview that shows the same one-line arrangement.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (name) => fs.readFileSync(path.join(root, name), 'utf8').replace(/\r\n?/g, '\n');

// The stored flag: field, JSON load and save.
const configHeader = read('src/ui/screensaver/screensaver_config.h');
assert.match(configHeader, /bool clock_single_line = false;/);
const configCpp = read('src/ui/screensaver/screensaver_config.cpp');
assert.match(configCpp, /loaded\.clock_single_line = doc\["clock_single_line"\] \| false;/);
assert.match(configCpp, /doc\["clock_single_line"\] = data_\.clock_single_line;/);

// The panel draws the two lines in one row through the shared clock widget.
assert.match(read('src/ui/screensaver/image_screensaver.cpp'),
  /widget_config\.horizontal = config\.clock_single_line;/);
assert.match(read('src/ui/screensaver/image_screensaver.cpp'),
  /widget_config\.one_line = config\.clock_single_line;/);
assert.match(read('src/types/clock/renderer.h'),
  /bool horizontal = false;/);
assert.match(read('src/types/clock/renderer.cpp'),
  /config\.horizontal \? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN/);
// Both boxes take the width of the longer line: the gap between the two shows
// and each line's alignment acts, like the Web preview (the half-height clock
// tiles keep their natural widths).
assert.match(read('src/types/clock/renderer.cpp'),
  /if \(!data \|\| data->fill_parent \|\| \(data->horizontal && !data->one_line\)\) return;/);
assert.match(read('src/types/clock/renderer.cpp'), /data->one_line = config\.one_line;/);

// The label lives in the central Strings schema, in every language (the
// table/alignment tests prove all eight have 675 fields in the same order).
assert.match(read('src/core/i18n/i18n.h'), /const char\* screensaver_clock_single_line;/);
const i18n = read('src/core/i18n/i18n.cpp');
assert.match(i18n, /static const Strings kStringsEn = \{[\s\S]{0,200000}?"Clock on one line"/);
assert.match(i18n, /static const Strings kStringsRu = \{[\s\S]{0,200000}?"Часы в одну строку"/);
assert.match(i18n, /static const Strings kStringsCs = \{[\s\S]{0,200000}?"Hodiny v jednom řádku"/);

// The Web Admin: checkbox, label binding and the one-line preview.
const html = read('src/web/server/render/web_admin_html.cpp');
assert.match(html, /id="screensaverClockSingleLine"/);
assert.match(html, /html \+= tr\.screensaver_clock_single_line;/);
const editor = read('src/web/admin/screensaver/editor.js');
assert.match(editor,
  /getElementById\('screensaverClockSingleLine'\)\.checked = !!d\.clock_single_line;/);
assert.match(editor,
  /bind\('screensaverClockSingleLine', 'change', el => \{ screensaverDraft\.clock_single_line = el\.checked; \}\);/);
assert.match(editor,
  /clock\.style\.display = d\.clock_single_line \? 'flex' : '';/,
  'the preview shows the one-line arrangement');

console.log('Screensaver clock on one line: config, panel row, label and preview wired');
