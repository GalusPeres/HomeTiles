import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs
  .readFileSync(path.join(repoRoot, 'src/web/server/handlers/web_admin_diagnostics.cpp'), 'utf8')
  .replace(/\r\n/g, '\n');

// The P4 hardware JPEG encoder shares the 2D-DMA pool with display PPA
// rotation and the camera, cover and screensaver decoders. The Web Admin
// screenshot must hold the shared arbiter for the whole engine lifecycle.
const start = source.indexOf('bool saveDrawBufferAsJpeg(');
assert.notEqual(start, -1, 'saveDrawBufferAsJpeg is missing');
const end = source.indexOf('\nvoid getSnapshotAreaForObject(', start);
assert.notEqual(end, -1, 'saveDrawBufferAsJpeg end marker is missing');
const body = source.slice(start, end);

const hardwareStart = body.indexOf('#else');
assert.notEqual(hardwareStart, -1, 'hardware encoder branch is missing');
const hardware = body.slice(hardwareStart);

const guard = hardware.indexOf('Dma2dArbiterGuard dma2d_guard(kScreenshotArbiterTimeoutMs);');
const lockedCheck = hardware.indexOf('if (!dma2d_guard.locked())');
const create = hardware.indexOf('jpeg_new_encoder_engine(');
const encode = hardware.indexOf('jpeg_encoder_process(');
const destroy = hardware.indexOf('jpeg_del_encoder_engine(');
const fileWrite = body.indexOf('sdFS().open(path, FILE_WRITE)');

assert.ok(guard > 0, 'screenshot must take the 2D-DMA arbiter');
assert.ok(lockedCheck > guard, 'screenshot must stop when the arbiter is not available');
assert.ok(create > lockedCheck, 'encoder creation must happen under the arbiter');
assert.ok(encode > create, 'encoding must happen under the arbiter');
assert.ok(destroy > encode, 'encoder deletion must happen under the arbiter');

// The guard is scoped: the lock is released before the slow microSD write.
const guardScope = hardware.slice(0, destroy);
const scopeOpen = guardScope.lastIndexOf('{\n', guard);
assert.ok(scopeOpen > 0, 'arbiter guard must live in its own block');
const scopeClose = hardware.indexOf('\n  }\n', destroy);
assert.ok(scopeClose > destroy, 'arbiter block must end after encoder deletion');
assert.ok(fileWrite > hardwareStart + scopeClose, 'microSD write must run after the arbiter is released');

const timeout = source.match(/constexpr uint32_t kScreenshotArbiterTimeoutMs = (\d+);/);
assert.ok(timeout, 'screenshot arbiter timeout is missing');
assert.ok(
  Number(timeout[1]) >= 500,
  'screenshot must wait long enough for other one-frame 2D-DMA users',
);

assert.ok(
  source.includes('#include "src/core/display/dma2d_arbiter.h"'),
  'screenshot must include the shared 2D-DMA arbiter',
);

console.log('PASS: Web Admin screenshot encodes under the 2D-DMA arbiter');
