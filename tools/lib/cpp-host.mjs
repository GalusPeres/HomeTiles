// Compiles and runs small host C++ harnesses against production sources. A
// missing compiler is reported as SKIP locally; CI images provide one.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

import {repoRoot} from './admin-source.mjs';

export function findCompiler() {
  return [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)
    .find(candidate => {
      const result = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
      return !result.error && result.status === 0;
    });
}

// files: {relativePath: text} written next to the harness (shim headers).
// sources: repository-relative .cpp files compiled with the harness.
// Returns stdout of the harness, or null after printing SKIP.
export function compileAndRun({label, harness, files = {}, sources = [], input = ''}) {
  const compiler = findCompiler();
  if (!compiler) {
    console.log(`SKIP: ${label} requires a C++ compiler`);
    return null;
  }
  const buildRoot = path.join(repoRoot, 'build', 'tests');
  fs.mkdirSync(buildRoot, {recursive: true});
  const tempRoot = fs.mkdtempSync(path.join(buildRoot, 'cpp-host-'));
  try {
    for (const [relative, text] of Object.entries({...files, 'harness.cpp': harness})) {
      const filename = path.join(tempRoot, relative);
      fs.mkdirSync(path.dirname(filename), {recursive: true});
      fs.writeFileSync(filename, text);
    }
    const output = path.join(tempRoot, process.platform === 'win32' ? 'harness.exe' : 'harness');
    const compile = spawnSync(compiler, [
      '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
      '-I', tempRoot, '-I', repoRoot,
      path.join(tempRoot, 'harness.cpp'),
      ...sources.map(source => path.join(repoRoot, source)),
      '-o', output
    ], {encoding: 'utf8'});
    assert.equal(compile.status, 0,
      `${label} did not compile:\n${compile.stdout}${compile.stderr}`);
    const run = spawnSync(output, [], {encoding: 'utf8', input, maxBuffer: 64 * 1024 * 1024});
    assert.equal(run.status, 0, `${label} failed:\n${run.stdout}${run.stderr}`);
    return run.stdout;
  } finally {
    assert.ok(path.resolve(tempRoot).startsWith(path.resolve(buildRoot) + path.sep));
    fs.rmSync(tempRoot, {recursive: true, force: true});
  }
}
