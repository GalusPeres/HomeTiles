// Firmware C++ is compiled with -fno-exceptions. The flag must follow the SDK
// flag file (which sets -fexceptions), so both the CI workflow and the local
// build script override compiler.cpp.flags with the same platform.txt line.
// Firmware code must never rely on try/catch/throw.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { maskCpp } from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (relativePath) =>
  fs.readFileSync(path.join(root, relativePath), 'utf8').replaceAll('\r\n', '\n');

const platformLine =
  '-MMD -c "@{compiler.sdk.path}/flags/cpp_flags" {compiler.warning_flags} ' +
  '{compiler.optimization_flags} {compiler.common_werror_flags}';

const workflow = read('.github/workflows/firmware.yml');
assert.ok(workflow.includes(`platform_cpp_flags='${platformLine}'`),
  'CI repeats the platform.txt compiler.cpp.flags line');
assert.ok(workflow.includes('--build-property "compiler.cpp.flags=${platform_cpp_flags} -fno-exceptions"'),
  'CI appends -fno-exceptions after the SDK flag file');
assert.ok(workflow.includes('grep -Fqx "compiler.cpp.flags=${platform_cpp_flags}"'),
  'CI fails when the installed platform line changes');

const localBuild = read('tools/build-firmware-local.ps1');
assert.ok(localBuild.includes(`$platformCppFlags = '${platformLine}'`),
  'the local build repeats the same platform.txt line');
assert.match(localBuild, /\$noExceptionsFlag = '-fno-exceptions'/);
assert.match(localBuild, /'--build-property', "compiler\.cpp\.flags=\$cppCompileFlags"/);
assert.match(localBuild, /--expect-flags "\$cppFlags \$noExceptionsFlag"/,
  'the fast build rejects caches compiled with exceptions');
assert.doesNotMatch(localBuild, /compiler\.cpp\.extra_flags=[^"]*-fno-exceptions/,
  'extra_flags precede the SDK flag file and cannot disable exceptions');

const sourceFiles = [path.join(root, 'HomeTiles.ino')];
const walk = (dir) => {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(full);
    else if (/\.(?:c|cc|cpp|h|hpp|ino)$/.test(entry.name)) sourceFiles.push(full);
  }
};
walk(path.join(root, 'src'));
const offenders = [];
for (const file of sourceFiles) {
  const code = maskCpp(fs.readFileSync(file, 'utf8'));
  if (/\btry\s*\{|\bcatch\s*\(|\bthrow\b/.test(code)) {
    offenders.push(path.relative(root, file));
  }
}
assert.deepEqual(offenders, [], 'firmware sources must not use exceptions');

console.log(`-fno-exceptions override is consistent; ${sourceFiles.length} sources use no exceptions.`);
