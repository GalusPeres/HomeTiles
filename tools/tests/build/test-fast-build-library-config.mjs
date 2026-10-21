// A new device in src/devices/device_select.h sent every warm build cache to a
// full arduino-cli build (2026-10-18): LVGL reads that file through lv_conf.h,
// and the fast build only compared file times. The cached library objects stay
// valid as long as what the library sees is unchanged, so the fast build keeps
// them when the recorded fingerprint of the preprocessed headers matches, and
// still falls back for a real configuration change or any other stale file.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import {findCompiler} from '../../lib/cpp-host.mjs';
import {depFiles, libraryConfigKept} from '../../fast-build.mjs';
import {fingerprint, probeEntry, repoConfigHeaders, writeRecord} from '../../lib/library-config.mjs';

const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: library configuration fingerprint requires a C/C++ compiler');
} else {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hometiles-libcfg-test-'));
  try {
    const repo = path.join(root, 'repo');
    const lib = path.join(root, 'lib');
    const bp = path.join(root, 'build');
    for (const dir of [path.join(repo, 'src', 'devices'), lib, bp]) fs.mkdirSync(dir, {recursive: true});
    const conf = path.join(repo, 'lv_conf.h');
    const select = path.join(repo, 'src', 'devices', 'device_select.h');
    const source = path.join(lib, 'lv_group.c');
    fs.writeFileSync(conf, '#include "src/devices/device_select.h"\n#if defined(DEVICE_S3)\n#define LV_MEM 2\n#else\n#define LV_MEM 12\n#endif\n');
    fs.writeFileSync(select, '#pragma once\n#if defined(DEVICE_S3)\n#define S3_FAMILY 1\n#endif\n');
    fs.writeFileSync(source, '#include "lv_conf.h"\nint lv_mem = LV_MEM;\n');
    const obj = path.join(bp, 'lv_group.c.o');
    const entry = {
      directory: repo, file: source, obj,
      arguments: [compiler, '-x', 'c', '-DDEVICE_P4', '-I', repo, '-c', source, '-o', obj],
    };
    // The object and its dependency file, newer than every header.
    fs.writeFileSync(obj, 'object');
    fs.writeFileSync(path.join(bp, 'lv_group.c.d'), `${obj}: ${source} ${conf} ${select}\n`);
    const old = Date.now() / 1000 - 3600;
    for (const file of [conf, select, source]) fs.utimesSync(file, old, old);

    const entries = [entry];
    const headers = repoConfigHeaders(entries, repo, depFiles);
    assert.deepEqual(headers, ['lv_conf.h', 'src/devices/device_select.h'], 'lv_conf.h first, then what it includes');
    const probe = probeEntry(entries, repo, depFiles);
    assert.equal(probe, entry, 'the probe is a library object that includes lv_conf.h');
    const recorded = fingerprint(probe, headers, repo);
    assert.match(recorded, /^[0-9a-f]{64}$/);
    writeRecord(bp, {fingerprint: recorded, headers});

    // A new S3 board in device_select.h: newer file, same configuration for
    // this P4 build. The cache is kept.
    fs.writeFileSync(select, '#pragma once\n#if defined(DEVICE_S3) || defined(DEVICE_T3E)\n#define S3_FAMILY 1\n#endif\n');
    assert.equal(libraryConfigKept(bp, repo, entries, headers, probe), recorded,
      'a header change for another device keeps the cached library');

    // A change that reaches this configuration falls back to the full build.
    fs.writeFileSync(conf, '#include "src/devices/device_select.h"\n#if defined(DEVICE_S3)\n#define LV_MEM 2\n#else\n#define LV_MEM 16\n#endif\n');
    assert.equal(libraryConfigKept(bp, repo, entries, headers, probe), null,
      'a real configuration change needs the full build');
    fs.writeFileSync(conf, '#include "src/devices/device_select.h"\n#if defined(DEVICE_S3)\n#define LV_MEM 2\n#else\n#define LV_MEM 12\n#endif\n');
    assert.equal(libraryConfigKept(bp, repo, entries, headers, probe), recorded, 'the same configuration again');

    // A newer library source is never covered by the fingerprint (a second
    // object: the fast build reads every file time once per run).
    const source2 = path.join(lib, 'lv_obj.c');
    const obj2 = path.join(bp, 'lv_obj.c.o');
    fs.writeFileSync(source2, '#include "lv_conf.h"\nint lv_obj;\n');
    fs.writeFileSync(obj2, 'object');
    fs.writeFileSync(path.join(bp, 'lv_obj.c.d'), `${obj2}: ${source2} ${conf} ${select}\n`);
    fs.utimesSync(obj2, old, old);
    const entry2 = {...entry, file: source2, obj: obj2, arguments: [compiler, '-x', 'c', '-DDEVICE_P4', '-I', repo, '-c', source2, '-o', obj2]};
    assert.equal(libraryConfigKept(bp, repo, [entry, entry2], headers, probe), null,
      'a changed library source needs the full build');
  } finally {
    fs.rmSync(root, {recursive: true, force: true});
  }
  console.log('Fast build: a header change for another device keeps the cached library objects');
}
