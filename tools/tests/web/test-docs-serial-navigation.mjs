import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import http from "node:http";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { findBrowser } from "../../lib/headless-dom.mjs";
import { startDocsBrowser } from "../../lib/docs-browser.mjs";
import { setupSerial, esptool } from "./fixtures/docs-serial.mjs";
import { SerialAccess } from "../../../docs/assets/javascripts/serial-access.mjs";
import { SerialActivity } from "../../../docs/assets/javascripts/serial-activity.mjs";
import { DEVICE_PROFILES, INSTALLER_SCHEMA_VERSION, releaseAssetNames } from "../../../docs/assets/javascripts/installer-contract.mjs";

// A cancelled chooser must leave capture alone. A chosen port must await its
// old reader; a second operation cannot enter while that handoff is pending.
const access = new SerialAccess();
assert.equal(access.acquire("logs"), true);
let stopped = 0;
access.disconnectLogs = async () => { stopped++; access.release("logs"); };
await assert.rejects(access.requestInstallerPort({ requestPort: async () => { throw new DOMException("Cancelled", "NotFoundError"); } }));
assert.equal(stopped, 0);
assert.equal(access.owner, "logs");
let finish;
access.disconnectLogs = () => new Promise(resolve => { finish = () => { access.release("logs"); resolve(); }; });
const handoff = access.requestInstallerPort({ requestPort: async () => "chosen" });
await Promise.resolve();
assert.equal(access.owner, "logs");
assert.equal(access.installerPending, true);
assert.equal(access.acquire("logs"), false);
await assert.rejects(access.requestInstallerPort({ requestPort: () => assert.fail("Duplicate chooser") }));
finish();
assert.equal(await handoff, "chosen");
assert.equal(access.owner, "installer");
access.release("logs");
assert.equal(access.owner, "installer");
access.release("installer");
access.acquire("logs");
access.disconnectLogs = async () => { throw new Error("USB close failed"); };
await assert.rejects(access.requestInstallerPort({ requestPort: async () => "chosen" }), /USB close failed/);
assert.equal(access.owner, "logs");
assert.equal(access.installerPending, false);

const activity = new SerialActivity();
let notifications = 0;
activity.subscribe(() => notifications++);
activity.set("logs", "Logs active", "success", true);
activity.set("installer", "Choose USB port", "busy", true);
assert.equal(activity.current.source, "installer");
activity.set("installer", "Flash cancelled");
assert.equal(activity.current.label, "Logs active");
activity.set("logs", "Logs active", "success", true);
assert.equal(notifications, 4, "Unchanged activity must not redraw the header");
activity.clear("installer");
assert.equal(activity.current.label, "Logs active", "Clearing a flash result must preserve active capture status");
activity.clear("logs");
assert.equal(activity.current.label, "USB disconnected");

const executable = findBrowser();
const python = process.env.PYTHON || (process.platform === "win32" ? "python" : "python3");
const check = spawnSync(python, ["-c", "import mkdocs, material"], { encoding: "utf8" });
if (!executable || check.status !== 0) {
  if (process.env.HOMETILES_REQUIRE_DOCS_BROWSER === "1") throw new Error("Docs navigation tests require Chrome and mkdocs-material");
  console.log("SKIP: Docs navigation tests need Chrome or Edge and Python with mkdocs-material; serial ownership checks passed.");
  process.exit(0);
}

const repository = fileURLToPath(new URL("../../../", import.meta.url));
const artifacts = path.join(repository, "build/docs-serial-navigation");
fs.mkdirSync(artifacts, { recursive: true });
const profile = fs.mkdtempSync(path.join(artifacts, "browser-"));
const site = path.join(artifacts, "site");
const device = DEVICE_PROFILES.find(item => item.key === "guition_esp32_4848s040");
const names = releaseAssetNames("v0.6.10", device.key);
const index = { schemaVersion: INSTALLER_SCHEMA_VERSION, partial: true, tag: "v0.6.10", devices: [{ ...device,
  update: { file: names.update, size: 512, sha256: "a".repeat(64) },
  factory: { file: names.factory, size: device.flashSize, sha256: "a".repeat(64) },
}] };
let failEsptool = false;
let indexRequests = 0;
const server = http.createServer((request, response) => {
  const pathname = new URL(request.url, "http://localhost").pathname;
  response.setHeader("Cache-Control", "no-store");
  if (pathname === "/preview/firmware/latest/release.json") {
    indexRequests++;
    response.setHeader("Content-Type", "application/json");
    response.end(JSON.stringify(index));
    return;
  }
  if (pathname.startsWith("/__test__/")) {
    response.setHeader("Content-Type", "text/javascript");
    if (failEsptool) { response.writeHead(503); response.end("Unavailable"); return; }
    response.end(pathname.endsWith("bundle.js") ? esptool
      : `export class ${pathname.includes("esp32p4") ? "ESP32P4ROM" : "ESP32S3ROM"} {}`);
    return;
  }
  let filename = path.resolve(site, decodeURIComponent(pathname.replace(/^\/preview\//, "")));
  if (!filename.startsWith(site + path.sep) && filename !== site) { response.writeHead(404); response.end(); return; }
  if (fs.existsSync(filename) && fs.statSync(filename).isDirectory()) filename = path.join(filename, "index.html");
  if (!fs.existsSync(filename)) { response.writeHead(404); response.end(); return; }
  const type = { ".html": "text/html", ".js": "text/javascript", ".mjs": "text/javascript", ".css": "text/css", ".json": "application/json", ".xml": "application/xml", ".svg": "image/svg+xml" }[path.extname(filename)];
  if (type) response.setHeader("Content-Type", type);
  if (filename.endsWith(`${path.sep}installer.mjs`)) {
    response.end(fs.readFileSync(filename, "utf8").replaceAll("https://unpkg.com/esptool-js@0.6.1/", "/__test__/"));
  } else response.end(fs.readFileSync(filename));
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const base = origin + "/preview/";
let browser;
try {
  const build = spawnSync(python, ["-c", "import sys; from mkdocs.config import load_config; from mkdocs.commands.build import build; c=load_config('mkdocs.yml', site_url=sys.argv[1], site_dir=sys.argv[2], strict=True); c.extra['sitemap_aliases']=[sys.argv[3]]; build(c)", origin + "/", site, base], { cwd: repository, encoding: "utf8", timeout: 60000 });
  assert.equal(build.status, 0, build.stderr || build.stdout);
  browser = await startDocsBrowser(executable, profile);
  await browser.send("Page.addScriptToEvaluateOnNewDocument", { source: setupSerial });
  const evaluate = browser.evaluate;
  const until = browser.until;
  const click = selector => evaluate(`document.querySelector(${JSON.stringify(selector)}).click()`);
  const headerVisible = () => evaluate("getComputedStyle(document.querySelector('.ht-serial-status')).display !== 'none'");
  const navigate = async (page, selector) => {
    if (selector) await click(selector);
    else await evaluate(`Array.from(document.querySelectorAll('.md-sidebar--primary a')).find(a => a.href === ${JSON.stringify(base + page)}).click()`);
    await until(`window.finishedNavigation === ${JSON.stringify(base + page)} && document.querySelector('h1')`, `Navigate to ${page}`);
    assert.equal(await evaluate("window.navigationToken"), "retained", "Documentation navigation reloaded the document");
  };

  await browser.send("Page.navigate", { url: base + "faq/" });
  await until("!!document.querySelector('.ht-serial-status')", "Global status on ordinary documentation page");
  assert.equal(await headerVisible(), false, "Ordinary documentation must not show an idle USB badge");
  await evaluate("window.navigationToken = 'retained'; void document$.subscribe(() => window.finishedNavigation = location.href)");
  await navigate("device-logs/");
  await until("!document.querySelector('[data-log-connect]').disabled", "Logger loaded after navigation");
  const idleLogger = await browser.send("Page.captureScreenshot");
  fs.writeFileSync(path.join(artifacts, "logger-idle.png"), Buffer.from(idleLogger.data, "base64"));
  await evaluate("window.logRoot = document.querySelector('[data-device-logs]')");
  await click("[data-log-connect]");
  await until("fixture.opened && document.querySelector('.ht-serial-status').textContent === 'Logs active'", "Connected status");
  assert.equal(await headerVisible(), true, "Active capture needs a visible return link");
  await evaluate("fixture.emit('Before navigation\\n')");
  await until("logRoot.querySelector('[data-log-output]').textContent.includes('Before navigation')", "Initial output");
  await navigate("faq/");
  await evaluate("fixture.emit('Captured on another page\\n')");
  assert.equal(await evaluate("logRoot.querySelector('[data-log-output]').textContent.includes('Captured on another page')"), false, "Offscreen log must not repaint");
  await evaluate("history.back()");
  await until(`window.finishedNavigation === ${JSON.stringify(base + "device-logs/")}`, "Browser back navigation");
  await evaluate("history.forward()");
  await until(`window.finishedNavigation === ${JSON.stringify(base + "faq/")}`, "Browser forward navigation");
  assert.equal(await evaluate("window.navigationToken === 'retained' && fixture.requests === 1 && fixture.closes === 0"), true);
  await navigate("installer/");
  await until("document.querySelector('#installer-device').options.length === 2", "Installer loaded");
  assert.equal(await evaluate("fixture.closes"), 0, "Visiting the installer stopped capture");
  await evaluate(`
    window.installerRoot = document.querySelector('[data-hometiles-installer]');
    const select = installerRoot.querySelector('#installer-device');
    select.value = '${device.key}'; select.dispatchEvent(new Event('change'));
    const source = installerRoot.querySelector('#installer-firmware-source');
    source.value = 'local'; source.dispatchEvent(new Event('change'));
    const bytes = new Uint8Array(512); const view = new DataView(bytes.buffer);
    bytes[0] = 0xe9; view.setUint32(32, 0xabcd5432, true); view.setUint32(288, 0x44565034, true);
    bytes.set(new TextEncoder().encode('${device.key}'), 324);
    const files = new DataTransfer(); files.items.add(new File([bytes], 'test-update.bin'));
    installerRoot.querySelector('#installer-firmware-file').files = files.files;
    installerRoot.querySelector('#installer-firmware-file').dispatchEvent(new Event('change'));
    installerRoot.querySelector('#installer-exact-hardware').click();
  `);
  await until("!document.querySelector('#installer-flash').disabled", "Flash can hand off the logger");
  await navigate("device-logs/", ".ht-serial-status");
  await until("document.querySelector('[data-device-logs]') === logRoot && logRoot.querySelector('[data-log-output]').textContent.includes('Captured on another page')", "Capture retained");
  await navigate("installer/");
  assert.equal(await evaluate("document.querySelector('[data-hometiles-installer]') === installerRoot && document.querySelector('#installer-firmware-file').files[0].name === 'test-update.bin'"), true, "Installer controls and file selection were lost");
  assert.equal(indexRequests, 1, "Revisiting installer reloaded the release or recreated its session");
  await evaluate("fixture.cancelChooser = true");
  await click("#installer-flash");
  await until("!document.querySelector('#installer-flash').disabled && fixture.requests === 2", "Cancelled flash chooser");
  assert.equal(await evaluate("fixture.opened && fixture.closes === 0 && document.querySelector('.ht-serial-status').textContent === 'Logs active'"), true);
  await evaluate("fixture.cancelChooser = false");
  await click("#installer-flash");
  await until("!!fixture.finishWrite", "Simulated update reached flash write");
  assert.equal(await evaluate("document.querySelector('.ht-serial-status').textContent"), "Flashing · 48%");
  assert.equal(await headerVisible(), true, "Active flash progress must be visible");
  assert.deepEqual(await evaluate("fixture.events.slice(0, 5)"), ["log-open", "log-cancel", "log-cancelled", "log-close", "flash-open"]);
  await navigate("device-logs/");
  assert.equal(await evaluate("document.querySelector('[data-log-connect]').disabled"), true, "Logger competed with active flashing");
  await navigate("faq/");
  await evaluate("fixture.finishWrite()");
  await until("document.querySelector('.ht-serial-status').textContent === 'Flash complete'", "Flash finished on another page");
  assert.equal(await headerVisible(), false, "Completion must hide the inactive USB badge");
  await navigate("installer/");
  await until("document.querySelector('#installer-progress').value === 100 && document.querySelector('#installer-log-output').textContent.includes('Simulated flash write')", "Retained flash progress and log");
  assert.equal(await evaluate("fixture.writes"), 2, "Expected inactive app and OTA selection writes only");
  assert.equal(await evaluate("fixture.events.at(-1)"), "flash-close");
  assert.equal(await evaluate("localStorage.getItem('hometiles.webInstaller.lastRun.v1')"), null, "A successful flash must clear its persistent checkpoint");
  const completedFlash = JSON.stringify({ version: 1, deviceKey: device.key, mode: "update",
    busy: false, mutationStarted: true, recoveryRequired: false, progress: 100,
    phase: "Complete", message: "Update complete. Settings were preserved. Please restart the device manually.", kind: "success" });
  await navigate("faq/");
  await navigate("installer/");
  assert.equal(await evaluate("document.querySelector('#installer-phase').textContent"), "Ready", "Leaving an already viewed completion must reset it");
  assert.equal(await evaluate("document.querySelector('#installer-progress-panel').hidden && document.querySelector('#installer-log-panel').hidden"), true);
  assert.notEqual(await evaluate("document.querySelector('.ht-serial-status').textContent"), "Flash complete");

  // Preparing the next operation also clears a fresh result without navigation.
  await evaluate("fixture.finishWrite = null");
  await click("#installer-flash");
  await until("!!fixture.finishWrite", "Second simulated update");
  await evaluate("fixture.finishWrite()");
  await until("document.querySelector('#installer-phase').textContent === 'Complete'", "Fresh completion stays visible");
  await evaluate("{ const source = document.querySelector('#installer-firmware-source'); source.value = 'published'; source.dispatchEvent(new Event('change')); }");
  assert.equal(await evaluate("document.querySelector('#installer-phase').textContent"), "Ready", "Changing firmware must clear the previous completion");
  assert.equal(await evaluate("document.querySelector('#installer-progress-panel').hidden && document.querySelector('#installer-log-panel').hidden"), true);
  await evaluate("{ const source = document.querySelector('#installer-firmware-source'); source.value = 'local'; source.dispatchEvent(new Event('change')); }");

  // A new failed attempt must not erase, write or leave a stale busy status.
  await evaluate("fixture.wrongChip = true; fixture.finishWrite = null");
  await click("#installer-flash");
  await until("document.querySelector('.ht-serial-status').dataset.kind === 'error' && !document.querySelector('#installer-flash').disabled", "Flash error status");
  assert.equal(await headerVisible(), false, "A finished failure is shown on the installer, not as idle USB activity");
  assert.equal(await evaluate("fixture.writes"), 4);
  await navigate("device-logs/");
  await click("[data-log-connect]");
  await until("fixture.opened", "Reconnect after flasher releases USB");
  await evaluate("fixture.emit('After flashing\\n')");
  await evaluate(`const legacy = document.createElement('a'); legacy.href = ${JSON.stringify(base + "flashing/")}; document.body.append(legacy); legacy.click();`);
  await until(`window.finishedNavigation === ${JSON.stringify(base + "installer/#manual-flashing")}`, "Legacy flashing redirect");
  assert.equal(await evaluate("window.navigationToken === 'retained' && fixture.opened"), true, "Legacy redirect interrupted capture");
  await navigate("faq/");
  await evaluate("document.querySelector('[data-md-component=search-query]').focus()");
  await browser.send("Input.insertText", { text: "serial" });
  await browser.send("Input.dispatchKeyEvent", { type: "keyUp", key: "l", code: "KeyL", windowsVirtualKeyCode: 76 });
  await until("!!document.querySelector('.md-search-result__link[href*=\"device-logs\"]')", "Documentation search results");
  await click('.md-search-result__link[href*="device-logs"]');
  await until("document.querySelector('[data-device-logs]') === logRoot", "Search result navigation");
  assert.equal(await evaluate("window.navigationToken === 'retained' && fixture.opened"), true, "Search result interrupted capture");
  await navigate("");
  await until("!!document.querySelector('.ht-device-info')", "Overview loaded");
  await click(".ht-device-info");
  await until("!!document.querySelector(':popover-open')", "Device support popover after instant navigation");
  await navigate("faq/");
  await navigate("");
  await click(".ht-device-info");
  await until("!!document.querySelector(':popover-open')", "Revisited device support popover");
  await click(".ht-device-note-close");
  const screenshot = await browser.send("Page.captureScreenshot");
  fs.writeFileSync(path.join(artifacts, "desktop.png"), Buffer.from(screenshot.data, "base64"));
  await browser.send("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 1, mobile: false });
  assert.equal(await evaluate("(() => { const r = document.querySelector('.ht-serial-status').getBoundingClientRect(); return r.left >= 0 && r.right <= innerWidth; })()"), true, "USB status clipped on narrow screens");
  const mobile = await browser.send("Page.captureScreenshot");
  fs.writeFileSync(path.join(artifacts, "mobile.png"), Buffer.from(mobile.data, "base64"));
  await browser.send("Emulation.clearDeviceMetricsOverride");
  await navigate("device-logs/", ".ht-serial-status");
  await until("document.querySelector('[data-log-output]').textContent.includes('After flashing')", "Final capture");
  const snapshot = await evaluate("document.querySelector('[data-log-output]').textContent");
  await evaluate(`localStorage.setItem('hometiles.webInstaller.lastRun.v1', ${JSON.stringify(completedFlash)})`);
  await browser.send("Page.reload");
  await until("!!document.querySelector('[data-log-connect]') && !document.querySelector('[data-log-connect]').disabled", "Reload logger");
  assert.equal(await evaluate("document.querySelector('[data-log-output]').textContent"), snapshot, "Full reload lost captured output");
  assert.equal(await evaluate("fixture.requests"), 0, "Reload must not reconnect automatically");
  assert.equal(await headerVisible(), false, "A restored log is not an active USB connection");
  await evaluate("window.navigationToken = 'retained'; void document$.subscribe(() => window.finishedNavigation = location.href)");
  await navigate("installer/");
  await until("document.querySelector('#installer-phase')?.textContent === 'Ready'", "Old completion discarded after reload");
  assert.equal(await evaluate("document.querySelector('#installer-progress-panel').hidden && document.querySelector('#installer-log-panel').hidden"), true, "Old completion must not restore progress or a synthetic flash log");
  assert.equal(await evaluate("localStorage.getItem('hometiles.webInstaller.lastRun.v1')"), null, "Legacy successful results must be removed");
  assert.equal(await evaluate("document.querySelector('.ht-serial-status').textContent"), "USB disconnected", "An old saved flash result must not become current USB activity");
  const readyInstaller = await browser.send("Page.captureScreenshot");
  fs.writeFileSync(path.join(artifacts, "installer-ready.png"), Buffer.from(readyInstaller.data, "base64"));

  // Successful-result cleanup must preserve interrupted factory recovery.
  const interruptedFlash = JSON.stringify({ ...JSON.parse(completedFlash), mode: "factory", busy: true, progress: 48, kind: "info", phase: "Writing firmware" });
  await evaluate(`localStorage.setItem('hometiles.webInstaller.lastRun.v1', ${JSON.stringify(interruptedFlash)})`);
  await browser.send("Page.reload");
  await until("document.querySelector('#installer-phase')?.textContent === 'Recovery required'", "Interrupted factory checkpoint restored");
  assert.equal(await evaluate("JSON.parse(localStorage.getItem('hometiles.webInstaller.lastRun.v1')).recoveryRequired"), true);
  await browser.send("Page.reload");
  await until("document.querySelector('#installer-phase')?.textContent === 'Recovery required'", "Recovery survives another reload");
  assert.deepEqual(browser.errors, [], "Unexpected browser runtime errors");

  // A CDN failure must leave instant navigation usable and explain the tool's
  // failure again when its page is revisited, without duplicate initializers.
  failEsptool = true;
  await browser.send("Page.navigate", { url: base + "installer/" });
  await until("document.querySelector('#installer-status')?.textContent.includes('Unable to load')", "Unavailable esptool feedback");
  await evaluate("window.navigationToken = 'retained'; void document$.subscribe(() => window.finishedNavigation = location.href)");
  await navigate("faq/");
  await navigate("installer/");
  await until("document.querySelector('#installer-status')?.textContent.includes('Unable to load')", "Failure feedback after returning");
  console.log("Real MkDocs navigation, retained USB capture, flash handoff/progress/errors, header, reload and CDN failure passed.");
} finally {
  await browser?.close();
  await new Promise(resolve => server.close(resolve));
  // This is the freshly created browser profile beneath our test artifact path.
  assert.ok(path.resolve(profile).startsWith(path.resolve(artifacts) + path.sep));
  fs.rmSync(profile, { recursive: true, force: true, maxRetries: 20, retryDelay: 100 });
}
