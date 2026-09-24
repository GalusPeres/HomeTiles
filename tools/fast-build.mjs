#!/usr/bin/env node
// Fast incremental firmware build for local test builds.
//
// arduino-cli re-runs its serial library detection (one preprocessor pass per
// file, one file at a time) for every source whose object is out of date, so a
// change to a widely included header costs a long serial pass before the real,
// parallel compile starts. The library set of HomeTiles never changes between
// test builds, so this tool reuses what the last arduino-cli build in the same
// build folder recorded: compile_commands.json, the per-folder sketch archives,
// the link input order from the map file and the expanded platform recipes.
// It recompiles only out-of-date sketch sources in parallel, refreshes their
// archives, relinks and runs the same image recipes.
//
// Exit code 3 means "use a full arduino-cli build instead" (missing or
// incompatible cache, changed build flags, sketch preprocessing not possible).

import { spawn, spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const FALLBACK = 3;
const SOURCE_EXT = new Set(['.c', '.cpp', '.S']);
const SYNC_EXT = new Set(['.h', '.hh', '.hpp', '.c', '.cc', '.cpp', '.cxx', '.S', '.tpp', '.ipp']);

function parseArgs(argv) {
  const args = { jobs: os.cpus().length, linkOnly: false };
  for (let i = 0; i < argv.length; i++) {
    const key = argv[i];
    const next = () => argv[++i];
    if (key === '--build-path') args.buildPath = path.resolve(next());
    else if (key === '--repo') args.repo = path.resolve(next());
    else if (key === '--props') args.props = path.resolve(next());
    else if (key === '--output-dir') args.outputDir = path.resolve(next());
    else if (key === '--expect-flags') args.expectFlags = next();
    else if (key === '--fqbn') args.fqbn = next();
    else if (key === '--jobs') args.jobs = Math.max(1, Number(next()) || 1);
    else if (key === '--link-only') args.linkOnly = true;
    else if (key === '--image-dir') args.imageDir = path.resolve(next());
    else throw new Error(`Unknown argument: ${key}`);
  }
  for (const required of ['buildPath', 'repo', 'props']) {
    if (!args[required]) throw new Error(`Missing --${required.replace(/[A-Z]/g, c => '-' + c.toLowerCase())}`);
  }
  return args;
}

function fallback(reason) {
  console.log(`Fast build not possible: ${reason}`);
  process.exit(FALLBACK);
}

// Windows command-line splitting for the expanded platform recipes.
export function splitCommand(command) {
  const out = [];
  let current = '';
  let quoted = false;
  let active = false;
  for (const ch of command) {
    if (ch === '"') { quoted = !quoted; active = true; continue; }
    if (!quoted && /\s/.test(ch)) {
      if (active) out.push(current);
      current = '';
      active = false;
      continue;
    }
    current += ch;
    active = true;
  }
  if (active) out.push(current);
  return out;
}

function run(command, args, options = {}) {
  const result = spawnSync(command, args, { encoding: 'utf8', maxBuffer: 64 << 20, ...options });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    process.stderr.write(result.stdout || '');
    process.stderr.write(result.stderr || '');
    throw new Error(`Command failed (${result.status}): ${path.basename(command)}`);
  }
  return result.stdout || '';
}

function runRecipe(recipe, cwd) {
  if (/^cmd \/c /i.test(recipe)) {
    run('cmd', ['/d', '/s', '/c', `"${recipe.slice(7)}"`], { cwd, windowsVerbatimArguments: true });
    return;
  }
  const [exe, ...rest] = splitCommand(recipe);
  run(exe, rest, { cwd });
}

function loadProps(file) {
  const props = new Map();
  for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
    const eq = line.indexOf('=');
    if (eq > 0) props.set(line.slice(0, eq), line.slice(eq + 1));
  }
  return props;
}

const mtimes = new Map();
function mtime(file) {
  if (mtimes.has(file)) return mtimes.get(file);
  let value = -1;
  try { value = fs.statSync(file).mtimeMs; } catch {}
  mtimes.set(file, value);
  return value;
}

export function depFiles(depPath) {
  let text;
  try { text = fs.readFileSync(depPath, 'utf8'); } catch { return null; }
  const joined = text.replace(/\\\r?\n/g, ' ');
  const colon = joined.search(/\.o:\s/);
  const body = colon >= 0 ? joined.slice(colon + 3) : joined;
  return body.split(/(?<!\\)\s+/).map(t => t.replace(/\\ /g, ' ')).filter(t => t && t !== '\\');
}

function isOutOfDate(entry) {
  const objTime = mtime(entry.obj);
  if (objTime < 0) return true;
  if (mtime(entry.file) > objTime) return true;
  const deps = depFiles(entry.obj.replace(/\.o$/, '.d'));
  if (!deps) return true;
  return deps.some(dep => {
    const t = mtime(path.isAbsolute(dep) ? dep : path.join(entry.directory, dep));
    return t < 0 || t > objTime;
  });
}

// arduino-cli copies each sketch source with a leading #line directive that
// points at the original file; copies must match that byte for byte.
export function sketchCopyContent(file) {
  const directive = `#line 1 "${file.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"\n`;
  return Buffer.concat([Buffer.from(directive, 'utf8'), fs.readFileSync(file)]);
}

function fileEquals(file, content) {
  try {
    if (fs.statSync(file).size !== content.length) return false;
    return fs.readFileSync(file).equals(content);
  } catch {
    return false;
  }
}

function walk(dir, visit) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(full, visit);
    else visit(full);
  }
}

// Mirrors what arduino-cli copies and compiles: root-level sources and src/**.
function syncSketchCopy(repo, sketchDir) {
  const copied = [];
  const present = new Set();
  const consider = file => {
    const ext = path.extname(file);
    if (!SYNC_EXT.has(ext)) return;
    const rel = path.relative(repo, file);
    present.add(rel.toLowerCase());
    const target = path.join(sketchDir, rel);
    const content = sketchCopyContent(file);
    if (fileEquals(target, content)) return;
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, content);
    mtimes.delete(target);
    copied.push(rel);
  };
  for (const entry of fs.readdirSync(repo, { withFileTypes: true })) {
    if (entry.isFile()) consider(path.join(repo, entry.name));
  }
  walk(path.join(repo, 'src'), consider);
  return { copied, present };
}

// Regenerates sketch/HomeTiles.ino.cpp without ctags: the prototype block of
// the previous arduino-cli output is kept and re-anchored to the new .ino.
export function regenerateInoCpp(inoPath, generatedPath) {
  const ino = fs.readFileSync(inoPath, 'utf8').split(/\r?\n/);
  if (ino.length && ino[ino.length - 1] === '') ino.pop();
  const old = fs.readFileSync(generatedPath, 'utf8').split(/\r?\n/);
  if (old[old.length - 1] === '') old.pop();
  const lineRe = /^#line (\d+) "(.*)"$/;
  if (old[0] !== '#include <Arduino.h>' || !lineRe.test(old[1] || '')) return null;
  const inoRef = old[1].match(lineRe)[2];
  // Old body without directives, and the prototype block position.
  let blockStart = -1;
  let blockEnd = -1;
  const protos = [];
  for (let i = 2; i < old.length; i++) {
    const m = old[i].match(lineRe);
    if (!m) continue;
    if (blockStart < 0) blockStart = i;
    if (i + 1 < old.length && !lineRe.test(old[i + 1]) && /\);\s*$/.test(old[i + 1]) &&
        (i + 2 >= old.length || lineRe.test(old[i + 2]))) {
      protos.push({ line: Number(m[1]), text: old[i + 1] });
      i++;
      continue;
    }
    blockEnd = i;
    break;
  }
  if (blockStart < 0 || blockEnd < 0) return null;
  const resumeLine = Number(old[blockEnd].match(lineRe)[1]);
  const oldIno = [...old.slice(2, blockStart), ...old.slice(blockEnd + 1)];
  if (oldIno.some(l => lineRe.test(l))) return null;
  // Re-anchor the insertion point and every prototype by the definition text.
  const findUnique = text => {
    const hits = [];
    ino.forEach((l, i) => { if (l === text) hits.push(i + 1); });
    return hits.length === 1 ? hits[0] : -1;
  };
  const anchor = findUnique(oldIno[resumeLine - 1]);
  if (anchor < 0) return null;
  const block = [];
  for (const proto of protos) {
    const at = findUnique(oldIno[proto.line - 1]);
    if (at < 0) return null;
    block.push(`#line ${at} "${inoRef}"`, proto.text);
  }
  return [
    old[0], old[1],
    ...ino.slice(0, anchor - 1),
    ...block,
    `#line ${anchor} "${inoRef}"`,
    ...ino.slice(anchor - 1),
    '',
  ].join('\n');
}

function compileAll(entries, jobs) {
  return new Promise((resolve, reject) => {
    let next = 0;
    let running = 0;
    let failed = null;
    const start = () => {
      if (failed) return;
      if (next >= entries.length && running === 0) { resolve(); return; }
      while (running < jobs && next < entries.length) {
        const entry = entries[next++];
        running++;
        const [exe, ...args] = entry.arguments;
        const child = spawn(exe, args, { cwd: entry.directory });
        let err = '';
        child.stderr.on('data', d => { err += d; });
        child.stdout.on('data', d => { err += d; });
        child.on('error', e => { failed = e; reject(e); });
        child.on('close', code => {
          running--;
          if (code !== 0) {
            failed = new Error(`Compile failed: ${path.relative(entry.directory, entry.file)}`);
            failed.output = err;
            failed.file = entry.file;
            reject(failed);
            return;
          }
          if (err.trim()) process.stdout.write(err);
          start();
        });
      }
    };
    start();
  });
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const started = Date.now();
  const bp = args.buildPath;
  const sketchDir = path.join(bp, 'sketch');
  const ccPath = path.join(bp, 'compile_commands.json');
  const mapPath = path.join(bp, 'HomeTiles.ino.map');
  const generatedIno = path.join(sketchDir, 'HomeTiles.ino.cpp');
  for (const file of [ccPath, mapPath, args.props, generatedIno]) {
    if (!fs.existsSync(file)) fallback(`missing ${path.basename(file)} from a previous arduino-cli build`);
  }
  // Board options (USB mode, PSRAM, ...) change compiler defines that the
  // cached compile commands would silently keep.
  if (args.fqbn) {
    let cached = '';
    try {
      cached = JSON.parse(fs.readFileSync(path.join(bp, 'build.options.json'), 'utf8')).fqbn || '';
    } catch {}
    if (cached !== args.fqbn) fallback('board options (FQBN) differ from the cached build');
  }
  // arduino-cli rewrites build.options.json when a build starts and
  // compile_commands.json when it finishes: an interrupted or failed build
  // leaves compile commands that may not match the recorded options.
  if (mtime(path.join(bp, 'build.options.json')) > mtime(ccPath)) {
    fallback('the last arduino-cli build in this folder did not finish');
  }
  const props = loadProps(args.props);
  const recipes = {
    combine: props.get('recipe.c.combine.pattern'),
    ar: props.get('recipe.ar.pattern'),
    bin: props.get('recipe.objcopy.bin.pattern'),
    partitions: props.get('recipe.objcopy.partitions.bin.pattern'),
    size: props.get('recipe.size.pattern'),
  };
  if (Object.values(recipes).some(v => !v)) fallback('platform recipes are incomplete');
  if (!recipes.combine.includes('{object_files}') || !recipes.combine.includes('{archive_file_path}')) {
    fallback('unexpected link recipe');
  }

  const commands = JSON.parse(fs.readFileSync(ccPath, 'utf8'));
  const sketchPrefix = sketchDir.toLowerCase() + path.sep;
  const sketchEntries = [];
  for (const c of commands) {
    if (!c.file.toLowerCase().startsWith(sketchPrefix)) continue;
    const o = c.arguments.indexOf('-o');
    if (o < 0) fallback('compile command without -o');
    sketchEntries.push({ ...c, obj: c.arguments[o + 1] });
  }
  if (!sketchEntries.length) fallback('no sketch compile commands');
  if (args.expectFlags) {
    const sample = sketchEntries.find(e => e.file.endsWith('.cpp')).arguments;
    const expected = new Set(splitCommand(args.expectFlags));
    for (const flag of expected) {
      if (!sample.includes(flag)) fallback(`build flag ${flag} is missing from the cached build`);
    }
    for (const flag of sample) {
      if (/^-D(HOMETILES_|DEVICE_)/.test(flag) && !expected.has(flag)) {
        fallback(`cached build has the extra flag ${flag}`);
      }
    }
  }

  let changed = [];
  const known = new Map(sketchEntries.map(e => [e.file.toLowerCase(), e]));
  const templates = {};
  for (const e of sketchEntries) templates[path.extname(e.file)] ??= e;

  if (!args.linkOnly) {
    const { copied, present } = syncSketchCopy(args.repo, sketchDir);
    if (copied.length) console.log(`Synced ${copied.length} changed sketch file(s).`);

    const inoCpp = regenerateInoCpp(path.join(args.repo, 'HomeTiles.ino'), generatedIno);
    if (inoCpp === null) fallback('HomeTiles.ino changed in a way that needs arduino-cli preprocessing');
    if (fs.readFileSync(generatedIno, 'utf8').replace(/\r\n/g, '\n') !== inoCpp) {
      fs.writeFileSync(generatedIno, inoCpp);
      mtimes.delete(generatedIno);
      console.log('Regenerated HomeTiles.ino.cpp.');
    }

    // Sources removed from the repository leave the build; new ones join it.
    for (const e of sketchEntries) {
      const rel = path.relative(sketchDir, e.file).toLowerCase();
      e.removed = rel !== 'homeTiles.ino.cpp'.toLowerCase() && !present.has(rel);
    }
    walk(path.join(sketchDir, 'src'), file => {
      if (!SOURCE_EXT.has(path.extname(file)) || known.has(file.toLowerCase())) return;
      if (!present.has(path.relative(sketchDir, file).toLowerCase())) return;
      const t = templates[path.extname(file)];
      if (!t) fallback(`no compile template for ${path.extname(file)}`);
      const obj = file + '.o';
      const argsCopy = t.arguments.map(a => (a === t.file ? file : a === t.obj ? obj : a));
      const entry = { directory: t.directory, file, arguments: argsCopy, obj, added: true };
      sketchEntries.push(entry);
      known.set(file.toLowerCase(), entry);
    });
    changed = sketchEntries.filter(e => !e.removed && isOutOfDate(e));
  }

  const liveEntries = sketchEntries.filter(e => !e.removed);
  if (changed.length) {
    console.log(`Compiling ${changed.length} file(s) with ${args.jobs} jobs...`);
    try {
      await compileAll(changed, args.jobs);
    } catch (error) {
      process.stderr.write(error.output || '');
      if (error.file && error.file.toLowerCase() === generatedIno.toLowerCase()) {
        fallback('the regenerated HomeTiles.ino.cpp does not compile (new .ino functions need prototypes)');
      }
      throw error;
    }
  } else {
    console.log('No sketch source changed.');
  }

  // Refresh the per-folder archives that arduino-cli links: every folder whose
  // archive is missing or older than one of its objects. A previous run that
  // stopped on a compile error leaves new objects behind in stale archives.
  const dirs = new Set([...changed, ...sketchEntries.filter(e => e.removed || e.added)].map(e => path.dirname(e.obj)));
  for (const e of liveEntries) {
    const dir = path.dirname(e.obj);
    if (dirs.has(dir)) continue;
    let archiveTime = -1;
    try { archiveTime = fs.statSync(path.join(dir, 'objs.a')).mtimeMs; } catch {}
    let objTime = -1;
    try { objTime = fs.statSync(e.obj).mtimeMs; } catch {}
    if (archiveTime < 0 || objTime > archiveTime) dirs.add(dir);
  }
  const [arExe, ...arBase] = splitCommand(recipes.ar);
  const arFlags = arBase.filter(a => !a.includes('{'));
  for (const dir of dirs) {
    const members = liveEntries.filter(e => path.dirname(e.obj) === dir).map(e => path.basename(e.obj)).sort();
    const archive = path.join(dir, 'objs.a');
    fs.rmSync(archive, { force: true });
    if (members.length) run(arExe, [...arFlags, 'objs.a', ...members], { cwd: dir });
  }

  // Link inputs in the order of the previous link; new sketch folders join the
  // sketch archives, emptied folders leave.
  const loads = fs.readFileSync(mapPath, 'utf8').split(/\r?\n/)
    .filter(l => l.startsWith('LOAD ')).map(l => l.slice(5));
  const coreIndex = loads.findIndex(l => /^core[\\/]core\.a$/i.test(l));
  if (coreIndex < 0) fallback('core archive not found in the previous link map');
  let inputs = loads.slice(0, coreIndex);
  const sketchInputs = inputs.filter(l => /^sketch[\\/]/i.test(l));
  const otherInputs = inputs.filter(l => !/^sketch[\\/]/i.test(l));
  const liveArchives = new Set(liveEntries.map(e => path.relative(bp, path.join(path.dirname(e.obj), 'objs.a'))));
  const kept = sketchInputs.filter(l => liveArchives.has(l.replace(/\//g, '\\')));
  const keptSet = new Set(kept.map(l => l.toLowerCase()));
  const added = [...liveArchives].filter(l => !keptSet.has(l.toLowerCase())).sort();
  inputs = [...kept, ...added, ...otherInputs];

  const imageDir = args.imageDir || bp;
  fs.mkdirSync(imageDir, { recursive: true });
  const fwd = p => p.replace(/\\/g, '/');
  // Image outputs can be redirected (used to prove identical output).
  const retarget = recipe => (imageDir === bp ? recipe
    : recipe.split(fwd(bp) + '/HomeTiles.ino.').join(fwd(imageDir) + '/HomeTiles.ino.')
      .split(bp + '/HomeTiles.ino.').join(imageDir + '/HomeTiles.ino.'));
  const combine = retarget(recipes.combine);
  const linkArgs = [];
  for (const token of splitCommand(combine)) {
    // arduino-cli links every sketch and library archive whole.
    if (token === '{object_files}') linkArgs.push('-Wl,--whole-archive', ...inputs, '-Wl,--no-whole-archive');
    else linkArgs.push(token.replace('{archive_file_path}', loads[coreIndex]));
  }
  const [linkExe, ...linkRest] = linkArgs;
  const commandLength = linkArgs.reduce((n, a) => n + a.length + 3, 0);
  console.log(`Linking ${inputs.length} inputs...`);
  if (commandLength > 30000) {
    const rsp = path.join(bp, 'hometiles-fast-link.rsp');
    fs.writeFileSync(rsp, linkRest.map(a => `"${fwd(a)}"`).join('\n'));
    run(linkExe, [`@${rsp}`], { cwd: bp });
  } else {
    run(linkExe, linkRest, { cwd: bp });
  }

  // Image recipes exactly as the platform runs them.
  runRecipe(retarget(recipes.bin), bp);
  if (imageDir === bp) {
    runRecipe(recipes.partitions, bp);
    for (let n = 1; props.has(`recipe.hooks.objcopy.postobjcopy.${n}.pattern`); n++) {
      runRecipe(props.get(`recipe.hooks.objcopy.postobjcopy.${n}.pattern`), bp);
    }
  }
  const [sizeExe, ...sizeArgs] = splitCommand(retarget(recipes.size));
  const sizeOut = run(sizeExe, sizeArgs, { cwd: bp });
  const text = sizeOut.split(/\r?\n/).filter(l => /^\.(flash\.text|flash\.rodata)\s/.test(l)).join(', ');

  if (args.outputDir && imageDir === bp) {
    fs.mkdirSync(args.outputDir, { recursive: true });
    for (const suffix of ['bin', 'elf', 'map', 'bootloader.bin', 'partitions.bin', 'merged.bin']) {
      const from = path.join(bp, `HomeTiles.ino.${suffix}`);
      if (fs.existsSync(from)) fs.copyFileSync(from, path.join(args.outputDir, `HomeTiles.ino.${suffix}`));
    }
  }
  const bin = path.join(imageDir, 'HomeTiles.ino.bin');
  const sha = crypto.createHash('sha256').update(fs.readFileSync(bin)).digest('hex').toUpperCase();
  console.log(`Fast build done in ${Math.round((Date.now() - started) / 1000)} s: ${bin}`);
  console.log(`SHA256: ${sha}${text ? ` (${text.replace(/\s+/g, ' ')})` : ''}`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  main().catch(error => {
    console.error(error.message);
    process.exit(1);
  });
}
