// Reopening the Media popup of the same player keeps its body visible for
// the first frame and applies the new state afterwards. A song that changed
// while the popup was closed showed the previous cover (and title) until then
// (user 2026-10-08, every device, also the S3). The tile's current cover and
// the song's text are taken before the first frame now.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/ui/popups/media/media_popup.cpp'), 'utf8').replace(/\r\n/g, '\n');
const fn = name => cppFunctionDefinitions(source).find(f => f.name === name).source;

const prepare = fn('prepare_media_popup_open');
const same = prepare.indexOf('const bool same_entity = ctx->entity_id == init.entity_id;');
const lookup = prepare.indexOf('tile_renderer_find_media_cover(init.entity_id, cover_hash);');
const cover = prepare.indexOf('update_cover(ctx, cover, cover_hash);');
const text = prepare.indexOf('apply_media_text(ctx, init);');
const defer = prepare.indexOf('defer_popup_body(');
assert.ok(same >= 0 && same < lookup && lookup < cover && cover < text && text < defer,
  'the kept body gets the current cover and text before the first frame');
assert.match(prepare, /if \(same_entity\) \{[\s\S]*?update_cover\(ctx, cover, cover_hash\);[\s\S]*?\}/);
assert.match(prepare, /finish_media_popup_open,\s*same_entity\)\)/, 'the body stays visible only for the same player');

// The body applies the same text through the same helper, so both paths agree.
assert.match(fn('apply_init_to_context'), /popup_layout::alignHeader\(ctx->card, ctx->title_label, ctx->icon_label\);\s*apply_media_text\(ctx, init\);/);
const mediaText = fn('apply_media_text');
assert.match(mediaText, /set_popup_label\(ctx->media_title_label, media_title\);/);
assert.match(mediaText, /media_no_playback/);
// The cover update keeps an unchanged cover as it is.
assert.match(fn('update_cover'), /if \(cover_dsc && ctx->cover_hash == cover_hash && ctx->cover_dsc\) \{/);

console.log('Media popup: a reopened player shows its current cover and song from the first frame');
