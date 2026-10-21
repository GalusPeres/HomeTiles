// AGENTS.md keeps the shared PROJECT_CONTEXT.md below 160 lines and 12 KiB:
// every agent loads it at the start of each session, and running diaries had
// grown it to 48 KB. Finished work belongs in git and
// docs-dev/project-history.md.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
// LF line ends, so a CRLF checkout counts the same as the repository.
const text = fs.readFileSync(path.join(root, 'PROJECT_CONTEXT.md'), 'utf8').replaceAll('\r\n', '\n');
const lines = text.endsWith('\n') ? text.split('\n').length - 1 : text.split('\n').length;
const bytes = Buffer.byteLength(text, 'utf8');

assert.ok(lines < 160, `PROJECT_CONTEXT.md has ${lines} lines (limit: below 160)`);
assert.ok(bytes < 12 * 1024, `PROJECT_CONTEXT.md has ${bytes} bytes (limit: below 12 KiB)`);

console.log(`PASS: PROJECT_CONTEXT.md has ${lines} lines and ${bytes} bytes`);
