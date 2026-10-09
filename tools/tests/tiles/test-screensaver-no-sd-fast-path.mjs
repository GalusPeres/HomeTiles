import assert from 'node:assert/strict';
import fs from 'node:fs';

const deviceHeader = fs.readFileSync('src/devices/device.h', 'utf8');
const deviceSource = fs.readFileSync('src/devices/device.cpp', 'utf8');
const screensaverSource = fs.readFileSync(
  'src/ui/screensaver/image_screensaver.cpp',
  'utf8',
);

assert.match(
  deviceHeader,
  /bool sdReadyCached\(\);/,
  'Device API must expose a non-blocking cached SD readiness check',
);
assert.match(
  deviceSource,
  /bool initSDCard\(\)\s*\{[\s\S]*?g_sd_ready_cached\s*=\s*DeviceImpl::initSDCard\(\)/,
  'the normal boot-time SD probe must seed the cached readiness state',
);
assert.match(
  deviceSource,
  /bool sdReadyCached\(\)\s*\{\s*return g_sd_ready_cached;\s*\}/,
  'cached readiness must not call the device implementation or mount storage',
);
assert.match(
  deviceSource,
  /bool resumeSDCardAfterNetworkTransition\(\)\s*\{[\s\S]*?g_sd_ready_cached\s*=\s*DeviceImpl::resumeSDCardAfterNetworkTransition\(\)/,
  'an SD remount must refresh the cached readiness state',
);
assert.match(
  deviceSource,
  /bool suspendSDCardForNetworkTransition\(\)\s*\{[\s\S]*?DeviceImpl::suspendSDCardForNetworkTransition\(\);[\s\S]*?g_sd_ready_cached\s*=\s*false;/,
  'an SD suspend attempt must always clear cached readiness',
);

const preloadStart = screensaverSource.indexOf('void preload_image_screensaver()');
const preloadEnd = screensaverSource.indexOf(
  'void show_image_screensaver()',
  preloadStart,
);
assert.ok(preloadStart >= 0 && preloadEnd > preloadStart);
const preloadSource = screensaverSource.slice(preloadStart, preloadEnd);
assert.match(
  preloadSource,
  /if \(!screensaverConfig\.get\(\)\.use_wallpapers\) return;/,
  'disabled wallpapers must not schedule an unnecessary preload',
);

const wallpaperIoStart = screensaverSource.indexOf('uint8_t* read_wallpaper_file');
const wallpaperIoEnd = screensaverSource.indexOf('int first_enabled_wallpaper');
assert.ok(wallpaperIoStart >= 0 && wallpaperIoEnd > wallpaperIoStart);
const wallpaperIo = screensaverSource.slice(wallpaperIoStart, wallpaperIoEnd);
assert.doesNotMatch(
  wallpaperIo,
  /Device::sdReady\(\)/,
  'screensaver wallpaper discovery must never mount/probe SD in the UI path',
);
assert.match(
  wallpaperIo,
  /Device::sdReadyCached\(\)/,
  'screensaver wallpaper discovery must use the boot-time cached SD result',
);

const showStart = screensaverSource.indexOf('void show_image_screensaver()');
const showEnd = screensaverSource.indexOf('void hide_image_screensaver()', showStart);
assert.ok(showStart >= 0 && showEnd > showStart);
const showSource = screensaverSource.slice(showStart, showEnd);
assert.doesNotMatch(
  showSource,
  /Device::(?:initSDCard|sdReady)\(\)/,
  'opening the screensaver must not perform an SD mount or readiness probe',
);
assert.match(
  showSource,
  /wallpaper_visible\s*=\s*apply_wallpaper\([\s\S]*?#if defined\(CONFIG_IDF_TARGET_ESP32P4\)[\s\S]*?if \(!wallpaper_visible\) present_composited_screensaver_frame\(st\);/,
  'P4 black fallback must use the same fast complete-frame presentation as wallpapers',
);

console.log('Screensaver no-SD fast-path contract OK');
