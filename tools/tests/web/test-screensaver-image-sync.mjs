// The screensaver image list follows the microSD card: entries of deleted
// files disappear, new images join checked and are stored at once, and the
// display skips missing or unchecked images instead of showing them.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const names = ['ssClamp', 'ssNearestClockFont', 'ssClockAlignment', 'ssNormalizeLoaded',
  'ssSyncCardImages', 'syncScreensaverImages', 'ssPayload', 'ssCurrentWallpaper'];
const posts = [];
let config = null;
const context = vm.createContext({
  structuredClone, JSON, Math, Number, String, Array, Set, Map, Promise,
  screensaverDraft: null, screensaverWallpaperIndex: -1,
  screensaverLoaded: false, screensaverLoading: false,
  screensaverTimeFontSizes: [20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 96],
  screensaverDateFontSizes: [20, 24, 28, 32, 40, 48, 56, 64, 72],
  fetch: async (url, options) => {
    assert.equal(url, '/api/screensaver');
    if (options?.method === 'POST') {
      posts.push(JSON.parse(options.body));
      return {ok: true, json: async () => ({success: true})};
    }
    return {json: async () => structuredClone(config)};
  }
});
vm.runInContext(names.map(extractDeliveredFunction).join('\n'), context);
const call = (name, ...args) => context[name](...args);

const stale = ['03_JPEG_ROSSMANN.jpg', 'Screenshot 2026-08-31 214140.jpg', 'cyberpunk2.jpg']
  .map(file_name => ({file_name, enabled: false, focus_x: 500, focus_y: 500, zoom: 1000}));

// Reported case: only IMG_3854.jpg is left in /images, the list still holds
// deleted images and the new one used to arrive unchecked.
let data = call('ssNormalizeLoaded', {success: true, sd_ready: true,
  wallpapers: structuredClone(stale), available_wallpapers: ['IMG_3854.jpg']});
assert.equal(call('ssSyncCardImages', data), true);
assert.deepEqual(JSON.parse(JSON.stringify(data.wallpapers)),
  [{file_name: 'IMG_3854.jpg', enabled: true, focus_x: 500, focus_y: 500, zoom: 1000}]);

// Configured images keep their choice and crop; FAT names match without case.
data = call('ssNormalizeLoaded', {success: true, sd_ready: true,
  wallpapers: [{file_name: 'Photo.JPG', enabled: false, focus_x: 120, focus_y: 800, zoom: 1500}],
  available_wallpapers: ['photo.jpg']});
assert.equal(call('ssSyncCardImages', data), false);
assert.equal(data.wallpapers.length, 1);
assert.equal(data.wallpapers[0].enabled, false);
assert.equal(data.wallpapers[0].focus_x, 120);

// Without a card nothing is dropped: the images may come back with the card.
data = call('ssNormalizeLoaded', {success: true, sd_ready: false,
  wallpapers: structuredClone(stale), available_wallpapers: []});
assert.equal(call('ssSyncCardImages', data), false);
assert.equal(data.wallpapers.length, stale.length);

// The display stores at most 32 images; more files must not cause a save loop.
const many = Array.from({length: 40}, (_, i) => `img${i}.jpg`);
data = call('ssNormalizeLoaded', {success: true, sd_ready: true, wallpapers: [],
  available_wallpapers: many});
assert.equal(call('ssSyncCardImages', data), true);
assert.equal(data.wallpapers.length, 32);
assert.equal(call('ssSyncCardImages', data), false);

// File manager changes store the synced list at once, without a preview image.
const settle = () => new Promise(resolve => setTimeout(resolve, 0));
config = {success: true, sd_ready: true, use_wallpapers: true, duration_seconds: 5,
  wallpapers: structuredClone(stale), available_wallpapers: ['IMG_3854.jpg']};
call('syncScreensaverImages');
await settle(); await settle();
assert.equal(posts.length, 1);
assert.equal(posts[0].preview_wallpaper, '');
assert.deepEqual(posts[0].wallpapers.map(w => [w.file_name, w.enabled]), [['IMG_3854.jpg', true]]);
assert.equal(posts[0].duration_seconds, 5);

config = {success: true, sd_ready: true, wallpapers: posts[0].wallpapers,
  available_wallpapers: ['IMG_3854.jpg']};
call('syncScreensaverImages');
await settle(); await settle();
assert.equal(posts.length, 1, 'An unchanged list is not saved again');

// Device: missing or unchecked images are skipped, and the first-image
// fallback only applies when no configured image is on the card.
const device = readRepoFile('src/ui/screensaver/image_screensaver.cpp');
const body = name => {
  const start = device.indexOf(name);
  assert.ok(start >= 0, `${name} exists`);
  return device.slice(start, device.indexOf('\n}\n', start));
};
assert.match(body('bool wallpaper_usable('), /enabled && sd_wallpaper_file_exists/);
assert.match(body('bool find_first_sd_wallpaper('), /any_configured_wallpaper_on_card\(\)/);
assert.match(body('int first_enabled_wallpaper('), /wallpaper_usable\(/);
assert.equal((body('int next_enabled_wallpaper(').match(/wallpaper_usable\(/g) || []).length, 2);
assert.doesNotMatch(body('int next_enabled_wallpaper('), /is_wallpaper_file\(/);

const handler = readRepoFile('src/web/server/handlers/web_admin_screensaver.cpp');
assert.match(handler, /\\"sd_ready\\":/);

console.log('PASS screensaver image list follows the card');
