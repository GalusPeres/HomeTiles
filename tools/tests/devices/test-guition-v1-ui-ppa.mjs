import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const harness = fileURLToPath(new URL('./test-guition-v2-ui-ppa.mjs', import.meta.url));
const result = spawnSync(process.execPath, [harness, '--v1'], {encoding: 'utf8'});
process.stdout.write(result.stdout || '');
process.stderr.write(result.stderr || '');
assert.equal(result.status, 0, 'Guition V1 production UI rotation regression failed');
