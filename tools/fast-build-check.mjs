#!/usr/bin/env node
// Says before a build whether a device's warm cache can take the fast path:
// the library and core objects are fresh, or out of date only through
// repository headers whose recorded configuration fingerprint is unchanged
// (tools/lib/library-config.mjs). Exit 0 = fast path, 3 = a full build would
// run. Nothing is compiled or written.
// Usage: node tools/fast-build-check.mjs --build-path <cache> [--repo <repo>]
import fs from 'node:fs';
import path from 'node:path';

import {depFiles, firstOutOfDate, libraryConfigKept} from './fast-build.mjs';
import {probeEntry, repoConfigHeaders} from './lib/library-config.mjs';

const argv = process.argv.slice(2);
const value = name => {
  const i = argv.indexOf(name);
  return i >= 0 ? argv[i + 1] : undefined;
};
const bp = path.resolve(value('--build-path') || '');
const repo = path.resolve(value('--repo') || '.');
const ccPath = path.join(bp, 'compile_commands.json');
if (!fs.existsSync(ccPath)) {
  console.log(`FULL: no compile_commands.json in ${bp} (no warm cache)`);
  process.exit(3);
}
const sketch = path.join(bp, 'sketch').toLowerCase() + path.sep;
const other = JSON.parse(fs.readFileSync(ccPath, 'utf8'))
  .filter(c => !c.file.toLowerCase().startsWith(sketch))
  .map(c => ({...c, obj: c.arguments[c.arguments.indexOf('-o') + 1]}));
const stale = firstOutOfDate(other);
if (!stale) {
  console.log('FAST: library and core objects are up to date');
  process.exit(0);
}
const headers = repoConfigHeaders(other, repo, depFiles);
const kept = libraryConfigKept(bp, repo, other, headers, probeEntry(other, repo, depFiles));
if (kept) {
  console.log(`FAST: library configuration unchanged (${kept.slice(0, 12)}), ${path.relative(bp, stale.obj)} is only out of date through ${headers.join(', ')}`);
  process.exit(0);
}
console.log(`FULL: ${path.relative(bp, stale.obj)} is out of date and the library configuration is changed or not recorded`);
process.exit(3);
