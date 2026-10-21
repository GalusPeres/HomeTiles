// The configuration the cached library and core objects of a build folder were
// compiled with, as one fingerprint. They see repository headers only through
// lv_conf.h (and what it includes, such as src/devices/device_select.h), so a
// changed file time there says nothing about their own configuration: a new
// device added to device_select.h leaves every other device's LVGL exactly as
// it was (2026-10-18: one new board sent every warm cache to a full build).
// The fingerprint is those headers preprocessed with the library's own compile
// command (-E -dD -P): the macros and declarations the library really sees.
import { spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

export const RECORD_FILE = 'hometiles-library-config.json';

const norm = file => path.resolve(file).replace(/\//g, '\\').toLowerCase();

// Repository files the library and core objects depend on, from their .d
// files, relative to the repository (forward slashes, lv_conf.h first).
export function repoConfigHeaders(entries, repo, depFiles) {
  const root = norm(repo) + '\\';
  const found = new Set();
  for (const entry of entries) {
    const deps = depFiles(entry.obj.replace(/\.o$/, '.d')) || [];
    for (const dep of deps) {
      const full = path.isAbsolute(dep) ? dep : path.join(entry.directory, dep);
      if (norm(full).startsWith(root)) {
        found.add(path.relative(repo, path.resolve(full)).replace(/\\/g, '/'));
      }
    }
  }
  return [...found].sort((a, b) => (a === 'lv_conf.h' ? -1 : b === 'lv_conf.h' ? 1 : a.localeCompare(b)));
}

// The compile command of one library object that includes lv_conf.h.
export function probeEntry(entries, repo, depFiles) {
  const conf = norm(path.join(repo, 'lv_conf.h'));
  return entries.find(entry => (depFiles(entry.obj.replace(/\.o$/, '.d')) || [])
    .some(dep => norm(path.isAbsolute(dep) ? dep : path.join(entry.directory, dep)) === conf));
}

// SHA-256 of the headers as the probe entry's compiler sees them. `headerDir`
// holds the header files (the repository, or a folder with the versions of an
// earlier commit; lv_conf.h's relative includes resolve inside it).
export function fingerprint(entry, headers, headerDir) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hometiles-libcfg-'));
  try {
    const probe = path.join(tmp, 'probe.c');
    fs.writeFileSync(probe, headers.map(h => `#include "${path.join(headerDir, h).replace(/\\/g, '/')}"`).join('\n') + '\n');
    const args = entry.arguments.slice(1);
    const out = [];
    for (let i = 0; i < args.length; i++) {
      const a = args[i];
      if (a === '-o' || a === '-MF' || a === '-MT' || a === '-MQ') { i++; continue; }
      if (a === '-c' || a === '-MMD' || a === '-MD' || a === '-MP') continue;
      if (path.resolve(entry.directory, a) === path.resolve(entry.file)) continue;
      out.push(a);
    }
    out.push('-E', '-dD', '-P', probe);
    const run = spawnSync(entry.arguments[0], out, { cwd: entry.directory, encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 });
    if (run.status !== 0) return null;
    const text = run.stdout.split(headerDir.replace(/\\/g, '/')).join('<headers>');
    return crypto.createHash('sha256').update(text).digest('hex');
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

export function readRecord(buildPath) {
  try {
    return JSON.parse(fs.readFileSync(path.join(buildPath, RECORD_FILE), 'utf8'));
  } catch {
    return null;
  }
}

export function writeRecord(buildPath, record) {
  fs.writeFileSync(path.join(buildPath, RECORD_FILE), JSON.stringify(record, null, 2) + '\n');
}

// The header versions of a commit in a temporary folder (for a cache that was
// built before the fingerprint existed). Returns the folder; the caller removes it.
export function headersFromCommit(repo, commit, headers) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'hometiles-libcfg-commit-'));
  for (const header of headers) {
    const show = spawnSync('git', ['-C', repo, 'show', `${commit}:${header}`], { encoding: 'buffer', maxBuffer: 64 * 1024 * 1024 });
    if (show.status !== 0) {
      fs.rmSync(dir, { recursive: true, force: true });
      throw new Error(`${header} does not exist in ${commit}`);
    }
    fs.mkdirSync(path.dirname(path.join(dir, header)), { recursive: true });
    fs.writeFileSync(path.join(dir, header), show.stdout);
  }
  return dir;
}
