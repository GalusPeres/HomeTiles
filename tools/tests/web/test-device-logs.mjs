import assert from "node:assert/strict";
import fs from "node:fs";
import { SerialLogBuffer, LOG_LIMITS } from "../../../docs/assets/javascripts/serial-log-buffer.mjs";
import { SerialLogSession, SERIAL_LOG_OPTIONS } from "../../../docs/assets/javascripts/serial-log-session.mjs";
import { SerialLogStore, SERIAL_LOG_STORAGE_KEY } from "../../../docs/assets/javascripts/serial-log-store.mjs";
import { runDomHarness } from "../../lib/headless-dom.mjs";

const read = (path) => fs.readFileSync(new URL(`../../../${path}`, import.meta.url), "utf8");
const tick = () => new Promise((resolve) => setImmediate(resolve));
const deferred = () => {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
};

const log = new SerialLogBuffer();
log.append("\x1b[0;");
log.append("32m[Setup] Ready\x1b[0m\r");
log.append("\n[Mem] 40 KB\rnext\n\x00");
assert.equal(log.text, "[Setup] Ready\n[Mem] 40 KB\nnext\n");
for (let index = 0; index < 10000; index += 1) log.append(`${index}: ${"x".repeat(100)}\n`);
assert.ok(log.length <= LOG_LIMITS.characters);
assert.ok(log.text.split("\n").length <= LOG_LIMITS.lines + 1);
assert.ok(log.text.endsWith(`9999: ${"x".repeat(100)}\n`));
assert.equal(log.truncated, true);
log.clear();
assert.equal(log.text, "");
assert.equal(log.truncated, false);
log.append("x".repeat(1000000));
assert.equal(log.length, LOG_LIMITS.line, "Output without newlines must also stay bounded");
log.clear();
log.append("😀" + "x".repeat(LOG_LIMITS.line - 1));
assert.equal(log.text, "x".repeat(LOG_LIMITS.line - 1), "Trimming must not retain half of a Unicode character");
log.clear();
log.resetStream();
log.append("\x1b[" + "0".repeat(100) + "\nRecovered\n");
assert.ok(log.text.endsWith("\nRecovered\n"), "A damaged ANSI sequence must not swallow the rest of the session");

const saved = new Map();
const storage = {
  getItem: (key) => saved.get(key) ?? null,
  setItem: (key, value) => saved.set(key, value),
  removeItem: (key) => saved.delete(key),
};
const store = new SerialLogStore({ sessionStorage: storage });
assert.equal(store.save(log), true);
assert.deepEqual(store.load(), { version: 1, text: log.text, truncated: log.truncated });
saved.set("unrelated-key", "keep");
log.clear();
assert.equal(store.save(log), true);
assert.equal(store.load(), null);
assert.equal(saved.get("unrelated-key"), "keep", "Clear only the log snapshot");
for (const invalid of ["broken", "null", '{"version":2,"text":"old","truncated":false}',
  JSON.stringify({ version: 1, text: "x".repeat(LOG_LIMITS.characters + 1), truncated: false })]) {
  saved.set(SERIAL_LOG_STORAGE_KEY, invalid);
  assert.equal(store.load(), undefined);
  assert.equal(store.failed, true);
}
assert.equal(store.save(log), true, "Clear recovers a malformed snapshot");
const blocked = new SerialLogStore({ get sessionStorage() { throw new Error("Storage blocked"); } });
assert.equal(blocked.load(), undefined);
assert.equal(blocked.save(log), false);
assert.equal(blocked.failed, true);
const full = new SerialLogStore({ sessionStorage: { ...storage, setItem() { throw new Error("Quota exceeded"); } } });
log.append("Still readable\n");
assert.equal(full.save(log), false);
assert.equal(log.text, "Still readable\n", "A storage failure must not affect live capture");

function fakePort({ openWait, cancelWait, openError, closeError } = {}) {
  const events = [];
  let controller;
  const readable = new ReadableStream({
    start(value) { controller = value; },
    async cancel() { events.push("cancel"); await cancelWait; events.push("cancelled"); },
  });
  return {
    readable, events, controller,
    async open(options) {
      assert.deepEqual(options, SERIAL_LOG_OPTIONS);
      events.push("open");
      await openWait;
      if (openError) throw openError;
    },
    async close() {
      assert.equal(readable.locked, false, "Release the read lock before closing the port");
      events.push("close");
      if (closeError) throw closeError;
    },
    get writable() { throw new Error("The log viewer must never write serial data"); },
    setSignals() { throw new Error("The log viewer must never send reset/BOOT signals"); },
  };
}

function capture(requestPort) {
  const states = [];
  let text = "";
  const session = new SerialLogSession({
    serial: { requestPort },
    onState: (state, error) => states.push({ state, error }),
    onText: (value) => { text += value; },
  });
  return { session, states, get text() { return text; } };
}

// Native CDC and UART bridges share the same port contract; no USB ID filter.
for (const usbVendorId of [0x303a, 0x1a86, 0x10c4, 0x0403, 0xffff]) {
  const port = fakePort();
  port.getInfo = () => ({ usbVendorId });
  let requests = 0;
  const run = capture((...args) => { requests += 1; assert.equal(args.length, 0); return Promise.resolve(port); });
  const done = run.session.connect();
  run.session.connect();
  assert.equal(requests, 1, "Double Connect must not create a second chooser");
  await tick();
  assert.equal(run.states.at(-1).state, "connected");
  const bytes = new TextEncoder().encode("Boot: Küche ☀️\r\n");
  for (const byte of bytes) port.controller.enqueue(new Uint8Array([byte]));
  await tick();
  await run.session.disconnect();
  await done;
  assert.equal(run.text, "Boot: Küche ☀️\r\n", "Decode split UTF-8 without lost characters");
  assert.deepEqual(port.events, ["open", "cancel", "cancelled", "close"]);
  assert.equal(run.session.active, false);
}

{
  const chooser = deferred();
  const port = fakePort();
  const run = capture(() => chooser.promise);
  run.session.connect();
  const stop = run.session.disconnect();
  chooser.resolve(port);
  await stop;
  assert.deepEqual(port.events, [], "Leaving during the chooser must not open a port afterwards");
}

{
  const opening = deferred();
  const port = fakePort({ openWait: opening.promise });
  const run = capture(() => Promise.resolve(port));
  run.session.connect();
  await tick();
  const stop = run.session.disconnect();
  opening.resolve();
  await stop;
  assert.deepEqual(port.events, ["open", "close"], "A pending open must still be closed after navigation");
}

{
  const cancelling = deferred();
  const port = fakePort({ cancelWait: cancelling.promise });
  const run = capture(() => Promise.resolve(port));
  run.session.connect();
  await tick();
  const stop = run.session.disconnect();
  run.session.disconnect();
  await tick();
  assert.equal(run.session.active, true);
  assert.deepEqual(port.events, ["open", "cancel"], "Wait for stream cancellation before port.close()");
  cancelling.resolve();
  await stop;
  assert.deepEqual(port.events, ["open", "cancel", "cancelled", "close"]);
}

for (const mode of ["unplug", "eof", "open-error", "close-error", "cancelled"]) {
  const port = fakePort({
    openError: mode === "open-error" ? new Error("Port busy") : null,
    closeError: mode === "close-error" ? new Error("USB removed") : null,
  });
  const run = capture(() => mode === "cancelled"
    ? Promise.reject(new DOMException("No port selected", "NotFoundError")) : Promise.resolve(port));
  const done = run.session.connect();
  await tick();
  if (mode === "unplug" || mode === "close-error") port.controller.error(new Error("Device removed"));
  if (mode === "eof") port.controller.close();
  await done;
  assert.equal(run.session.active, false);
  assert.equal(port.readable.locked, false);
  assert.equal(run.states.at(-1).state, mode === "eof" ? "ended" : mode === "cancelled" ? "cancelled" : "error");
  const next = fakePort();
  run.session.serial.requestPort = () => Promise.resolve(next);
  run.session.connect();
  await tick();
  assert.equal(run.states.at(-1).state, "connected", `Reconnect after ${mode}`);
  await run.session.disconnect();
}

const page = read("docs/device-logs.md");
assert.match(read("mkdocs.yml"), /- Help:[\s\S]*- Device logs: device-logs\.md/);
assert.match(read(".github/workflows/docs.yml"), /node tools\/tests\/web\/test-device-logs\.mjs/);
assert.match(read("HomeTiles.ino"), /Serial\.begin\(115200\)/);
const moduleSource = read("docs/assets/javascripts/device-logs.mjs");
assert.doesNotMatch(moduleSource, /esptool|fetch\(|localStorage|setInterval/);
assert.doesNotMatch(read("docs/assets/javascripts/serial-log-store.mjs"), /fetch\(|localStorage|setInterval/);
const dataModule = (source) => `data:text/javascript;base64,${Buffer.from(source).toString("base64")}`;
const inlineModule = (source) => source.replace(/\.\/([a-z-]+)\.mjs\?v=[a-z0-9-]+/g,
  (_, name) => dataModule(inlineModule(read(`docs/assets/javascripts/${name}.mjs`))));
const browserModule = inlineModule(moduleSource);
const markup = page.slice(page.indexOf('<div class="ht-installer'), page.indexOf("<noscript>"));

// Run the actual page controls and modules in Chrome, replacing only Web Serial
// and the clipboard. Firmware text must remain text, and Copy must use new data
// even if the batched render has not happened yet.
runDomHarness({
  label: "Device log viewer DOM and serial lifecycle",
  tmpPrefix: "hometiles-device-logs-",
  extraArgs: ["--virtual-time-budget=10000"],
  html: `<!doctype html><html><head><style>
    ${read("docs/stylesheets/extra.css")}
    ${read("docs/stylesheets/device-logs.css")}
  </style></head><body>${markup}<pre id="result">running</pre><script type="module">
  const check = (value, message) => { if (!value) throw new Error(message); };
  const wait = (ms = 120) => new Promise(resolve => setTimeout(resolve, ms));
  const el = name => document.querySelector('[data-log-' + name + ']');
  let controller, port, requests = 0, closes = 0, clipboard = '', denyClipboard = false;
  Object.defineProperty(navigator, 'serial', { configurable: true, value: {
    async requestPort() {
      requests++;
      const readable = new ReadableStream({ start(value) { controller = value; } });
      port = { readable, async open() {}, async close() {
        check(!readable.locked, 'Port lock leaked'); closes++;
      } };
      return port;
    }
  } });
  Object.defineProperty(navigator, 'clipboard', { configurable: true, value: {
    async writeText(text) { if (denyClipboard) throw new Error('Denied'); clipboard = text; }
  } });
  const emit = text => controller.enqueue(new TextEncoder().encode(text));
  try {
    const { mountDeviceLogs } = await import(${JSON.stringify(dataModule(browserModule))});
    if (document.readyState !== 'complete') await new Promise(resolve => addEventListener('load', resolve, {once:true}));
    check(!el('connect').disabled && el('disconnect').disabled, 'Initial controls');
    el('connect').click(); el('connect').click();
    await wait();
    check(requests === 1 && el('connect').disabled && !el('disconnect').disabled, 'Connected controls');
    dispatchEvent(new Event('pageshow'));
    check(!el('disconnect').disabled && el('status').dataset.kind === 'success', 'Initial pageshow reset an active connection');
    emit('<img src=x onerror="window.logXss=true">\\n[Mem] free=40 KB\\n');
    await wait();
    check(!el('output').querySelector('img') && !window.logXss, 'Unsafe serial HTML');
    check(el('output').textContent.includes('[Mem] free=40 KB'), 'Missing live output');
    emit('Newest output\\n');
    await Promise.resolve(); await Promise.resolve();
    el('copy').click();
    await wait();
    check(clipboard.includes('Newest output'), 'Copy missed pending render data');
    check(el('action').textContent === 'Log copied.', 'Copy feedback');
    await wait(1400);
    el('clear').click(); await wait();
    check(!el('output').textContent && el('copy').disabled, 'Clear log');
    check(el('action').textContent === 'Log cleared.', 'Clear feedback');
    await wait(1400);
    check(el('action').textContent === 'Log cleared.', 'An older confirmation timer hid the new message');
    await wait(1200);
    check(el('action').textContent === '', 'Clear confirmation must disappear automatically');
    emit(Array.from({length:3000}, (_, i) => 'row ' + i + ' x'.repeat(20)).join('\\n') + '\\n');
    await wait();
    check(!el('limit').hidden && !el('output').textContent.includes('row 0 '), 'Bounded log');
    check(el('output').textContent.includes('row 2999 '), 'Keep the recent tail');
    el('follow').checked = false;
    el('output').scrollTop = 100;
    emit('After scrolling\\n'); await wait();
    check(el('output').scrollTop < el('output').scrollHeight - el('output').clientHeight - 8, 'Scroll position lost');
    el('follow').checked = true; el('follow').dispatchEvent(new Event('change'));
    check(el('output').scrollHeight - el('output').scrollTop - el('output').clientHeight <= 8, 'Follow log');
    denyClipboard = true; el('copy').click(); await wait();
    check(getSelection().toString().includes('row 2999 '), 'Manual clipboard fallback');
    const selected = getSelection().toString();
    emit('Selection stays readable\\n'); await wait();
    check(getSelection().toString() === selected, 'Incoming output destroyed copy selection');
    getSelection().removeAllRanges(); await wait();
    check(el('output').textContent.includes('Selection stays readable'), 'Capture stopped during selection');
    el('disconnect').click(); await wait();
    check(closes === 1 && !el('connect').disabled && el('disconnect').disabled, 'Disconnect cleanup');
    check(el('output').textContent.includes('row 2999 '), 'Disconnect discarded the log');
    el('connect').click(); await wait();
    controller.error(new Error('USB unplugged')); await wait();
    check(!el('connect').disabled && el('status').dataset.kind === 'error', 'Unplug recovery');
    el('connect').click(); await wait();
    controller.enqueue(new Uint8Array([0xe2]));
    await Promise.resolve(); await Promise.resolve();
    const beforeLeaving = el('output').textContent;
    dispatchEvent(new Event('pagehide'));
    dispatchEvent(new Event('pageshow')); await wait();
    check(closes === 3, 'Navigation leaked a port');
    check(el('output').textContent === beforeLeaving, 'Cancelled old reader changed the restored snapshot');
    check(!el('connect').disabled && el('disconnect').disabled, 'Back navigation controls');
    const unsupported = document.querySelector('[data-device-logs]').cloneNode(true);
    document.body.append(unsupported);
    mountDeviceLogs(unsupported, { browser: window, platform: {} });
    check(unsupported.querySelector('[data-log-connect]').disabled, 'Unsupported browser offered a port');
    check(unsupported.querySelector('[data-log-status]').textContent.includes('Chrome'), 'Unsupported browser guidance');
    document.body.dataset.result = 'pass';
    document.getElementById('result').textContent = 'pass';
  } catch (error) {
    document.body.dataset.result = 'fail';
    document.getElementById('result').textContent = error.stack;
  }
  </script></body></html>`,
});

// Follow a real link away and back, then reload twice. Dispatching pageshow on
// the same module alone cannot reproduce the original loss of its RAM buffer.
runDomHarness({
  label: "Device log persistence across navigation and reload",
  tmpPrefix: "hometiles-device-logs-navigation-",
  extraArgs: ["--virtual-time-budget=10000"],
  html: `<!doctype html><html><body>${markup}
  <a id="navigation" href="?phase=away">Another documentation page</a>
  <pre id="result">running</pre><script type="module">
  const check = (value, message) => { if (!value) throw new Error(message); };
  const wait = () => new Promise(resolve => setTimeout(resolve, 120));
  const key = ${JSON.stringify(SERIAL_LOG_STORAGE_KEY)};
  const phase = new URL(location.href).searchParams.get('phase');
  const el = name => document.querySelector('[data-log-' + name + ']');
  let controller, copied = '', requests = 0;
  Object.defineProperty(navigator, 'serial', { configurable: true, value: {
    async requestPort() {
      requests++;
      const readable = new ReadableStream({ start(value) { controller = value; } });
      return { readable, async open() {}, async close() { check(!readable.locked, 'Navigation leaked a reader'); } };
    }
  } });
  Object.defineProperty(navigator, 'clipboard', { configurable: true, value: {
    async writeText(text) { copied = text; }
  } });
  try {
    if (!phase) sessionStorage.removeItem(key);
    if (phase !== 'away') await import(${JSON.stringify(dataModule(browserModule))});
    if (document.readyState !== 'complete') await new Promise(resolve => addEventListener('load', resolve, {once:true}));
    await wait();
    if (!phase) {
      el('connect').click(); await wait();
      controller.enqueue(new TextEncoder().encode(
        Array.from({length:2050}, (_, i) => 'Captured row ' + i).join('\\n') + '\\nLast partial line: Küche ☀️'
      ));
      await wait();
      sessionStorage.setItem('test.expectedLog', el('output').textContent);
      check(!el('limit').hidden, 'Fixture did not reach the bounded tail');
      document.getElementById('navigation').click();
    } else if (phase === 'away') {
      check(JSON.parse(sessionStorage.getItem(key)).text === sessionStorage.getItem('test.expectedLog'),
        'Navigating away did not save the received log');
      const link = document.getElementById('navigation');
      link.href = '?phase=returned';
      link.click();
    } else if (sessionStorage.getItem('test.reloadStage') === 'cleared') {
      check(!el('output').textContent && el('copy').disabled, 'Cleared output reappeared after reload');
      check(sessionStorage.getItem(key) === null, 'Clear retained a saved log');
      check(requests === 0, 'Reload silently reconnected the device');
      document.body.dataset.result = 'pass';
      document.getElementById('result').textContent = 'pass';
    } else {
      const expected = sessionStorage.getItem('test.expectedLog');
      check(el('output').textContent === expected, 'Fresh page lost received or partial output');
      check(!el('copy').disabled && !el('limit').hidden, 'Restored controls or truncation notice lost');
      check(requests === 0 && el('disconnect').disabled, 'Restore must not open a USB session');
      el('copy').click(); await wait();
      check(copied.endsWith(expected) && copied.startsWith('[Log viewer:'), 'Restored log copy changed');
      if (sessionStorage.getItem('test.reloadStage') === 'restored') {
        el('clear').click();
        check(sessionStorage.getItem(key) === null, 'Clear must remove storage immediately');
        sessionStorage.setItem('test.reloadStage', 'cleared');
      } else sessionStorage.setItem('test.reloadStage', 'restored');
      location.reload();
    }
  } catch (error) {
    document.body.dataset.result = 'fail';
    document.getElementById('result').textContent = error.stack;
  }
  </script></body></html>`,
});
console.log("Device log buffer, USB cancellation, reconnect and browser controls passed.");
