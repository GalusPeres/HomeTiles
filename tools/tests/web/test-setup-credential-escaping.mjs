import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
function productionFunction(file, name) {
  const definitions = cppFunctionDefinitions(fs.readFileSync(path.join(root, file), 'utf8'));
  const definition = definitions.find(item => item.name === name);
  assert.ok(definition, `Missing production function: ${name}`);
  return definition.source;
}
const source = `
#include <iostream>
#include <string>
#include <cstdlib>
struct String : std::string {
  using std::string::string;
  String(const std::string& value) : std::string(value) {}
  char charAt(size_t index) const { return at(index); }
};
struct DeviceConfig { String wifi_ssid; String wifi_pass; String language; };
namespace web_admin_auth {
bool hidden = false;
bool storedSecretsHidden() { return hidden; }
}
namespace i18n {
struct Strings { const char* ap_wifi_keep_password_hint; };
const Strings& strings(const String&) {
  static const Strings strings{"Saved password hidden"};
  return strings;
}
}
struct ConfigManager {
  DeviceConfig config;
  const DeviceConfig& getConfig() const { return config; }
} configManager;
namespace Device { const char* displayName() { return "HomeTiles test panel"; } }
void appendWebFontFaceStyles(String&) {}
struct WebConfigServer { String getConfigPage(); };
${productionFunction('src/web/server/web_admin_utils.cpp', 'appendHtmlEscaped')}
${productionFunction('src/web/setup/web_config.cpp', 'WebConfigServer::getConfigPage')}
String fromHex(const char* input) {
  String result;
  while (*input) {
    const char byte[] = {input[0], input[1], 0};
    result += static_cast<char>(std::strtoul(byte, nullptr, 16));
    input += 2;
  }
  return result;
}
int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) return 1;
  configManager.config.wifi_ssid = fromHex(argv[1]);
  configManager.config.wifi_pass = fromHex(argv[2]);
  web_admin_auth::hidden = argc == 4;
  std::cout << WebConfigServer().getConfigPage();
}
`;
const compiler = [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version'], {encoding: 'utf8'}).status === 0);
if (!compiler) {
  console.log('SKIP: Setup credential escaping needs a C++ compiler');
  process.exit(0);
}
const buildRoot = path.join(root, 'build', 'tests');
fs.mkdirSync(buildRoot, {recursive: true});
const temporary = fs.mkdtempSync(path.join(buildRoot, 'setup-escaping-'));
try {
  const cpp = path.join(temporary, 'test.cpp');
  const executable = path.join(temporary, process.platform === 'win32' ? 'test.exe' : 'test');
  fs.writeFileSync(cpp, source);
  const compile = spawnSync(compiler,
    ['-std=c++17', '-Wall', '-Wextra', '-Werror', cpp, '-o', executable], {encoding: 'utf8'});
  assert.equal(compile.status, 0, compile.stdout + compile.stderr);
  for (const [ssid, password] of [
    ['Home network', ''],
    ['Room " data-injected="yes', '<x>&" data-injected="yes'],
    ['Réseau & Gäste', "Apostrophe's & < >"],
  ]) {
    // Hex transport avoids Windows argv code-page conversion of UTF-8 fixtures.
    const run = spawnSync(executable, [ssid, password].map(value => Buffer.from(value).toString('hex')),
      {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stderr);
    const expected = JSON.stringify({ssid, password}).replace(/</g, '\\u003c');
    // Parse the real generated page, rather than duplicating its rendering logic.
    const check = `<script>
      try {
        const expected = ${expected};
        const form = document.querySelector('form');
        const data = new FormData(form);
        if (data.get('wifi_ssid') !== expected.ssid || data.get('wifi_pass') !== expected.password)
          throw new Error('Saved credentials did not round-trip through form attributes');
        if (document.querySelector('[data-injected]') || form.querySelector('x'))
          throw new Error('Credential text became HTML markup');
        document.body.dataset.result = 'pass';
      } catch (error) {
        document.body.dataset.result = 'fail';
        document.body.append(error.message);
      }
    </script>`;
    runDomHarness({label: 'Setup credentials round trip',
      html: run.stdout.replace('</body>', check + '</body>'),
      tmpPrefix: 'hometiles-setup-escaping-'});
  }
  // With a Web Admin password the portal never renders the stored password.
  const secret = 'portal-secret-9';
  const hidden = spawnSync(executable,
    [...['Home network', secret].map(value => Buffer.from(value).toString('hex')), 'hidden'],
    {encoding: 'utf8'});
  assert.equal(hidden.status, 0, hidden.stderr);
  assert.ok(!hidden.stdout.includes(secret), 'the hidden password is not in the page');
  assert.match(hidden.stdout, /id="wifi_pass" name="wifi_pass" placeholder="Password" value="">/);
  assert.ok(hidden.stdout.includes('Saved password hidden'), 'the keep-password hint is shown');
} finally {
  assert.ok(path.resolve(temporary).startsWith(path.resolve(buildRoot) + path.sep));
  fs.rmSync(temporary, {recursive: true, force: true});
}
