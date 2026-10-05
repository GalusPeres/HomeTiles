// Optional Web Admin password: executes the production login/session core
// (src/web/server/auth/web_admin_auth_core.h) on the host and checks that every
// route is registered behind the password gate.
import assert from 'node:assert/strict';
import {createHmac, pbkdf2Sync} from 'node:crypto';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const salt = Buffer.from('00112233445566778899aabbccddeeff', 'hex');
const password = 'correct horse battery';
const key = pbkdf2Sync(Buffer.from(password, 'utf8'), salt, 10000, 32, 'sha256');
const cArray = bytes => `{${Array.from(bytes).join(',')}}`;

const harness = String.raw`
#include <cstdio>
#include <cstring>
#include "src/web/server/auth/web_admin_auth_core.h"

using namespace web_admin_auth;

static uint32_t g_state = 0x12345678u;
static void fake_random(uint8_t* out, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    g_state ^= g_state << 13;
    g_state ^= g_state >> 17;
    g_state ^= g_state << 5;
    out[i] = static_cast<uint8_t>(g_state >> 24);
  }
}

static void hex(const char* tag, const uint8_t* data, size_t length) {
  char text[129];
  ht_crypto::hexEncode(data, length, text, sizeof(text));
  std::printf("%s %s\n", tag, text);
}

#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

int main() {
  Credential credential;
  credential.enabled = true;
  const uint8_t salt[kSaltSize] = SALT;
  const uint8_t key[kKeySize] = KEY;
  std::memcpy(credential.salt, salt, kSaltSize);
  credential.iterations = 10000;
  std::memcpy(credential.key, key, kKeySize);
  static Tables tables;
  clearTables(tables);
  uint32_t now = 0xfffff000u;  // Close to the millis() wrap on purpose.

  // Successful login: proof = HMAC(key, nonce); single-use nonce.
  uint8_t nonce[kNonceSize], proof[kProofSize], session[kTokenSize], csrf[kTokenSize], server_proof[kProofSize];
  uint32_t retry = 0;
  issueNonce(tables, now, fake_random, nonce);
  hex("nonce", nonce, kNonceSize);
  computeProof(credential, nonce, proof);
  hex("proof", proof, kProofSize);
  CHECK(login(credential, tables, now + 10, fake_random, nonce, proof, session, csrf, server_proof, &retry) == LoginResult::Success);
  hex("server", server_proof, kProofSize);
  CHECK(login(credential, tables, now + 20, fake_random, nonce, proof, session, csrf, server_proof, &retry) == LoginResult::UnknownNonce);

  // The session authorises reads; changes also need the CSRF token.
  CHECK(checkAccess(credential, tables, now + 30, session, nullptr, false) == AccessResult::Allowed);
  CHECK(checkAccess(credential, tables, now + 30, session, nullptr, true) == AccessResult::CsrfMismatch);
  uint8_t wrong[kTokenSize];
  std::memcpy(wrong, csrf, kTokenSize);
  wrong[3] ^= 1;
  CHECK(checkAccess(credential, tables, now + 30, session, wrong, true) == AccessResult::CsrfMismatch);
  CHECK(checkAccess(credential, tables, now + 30, session, csrf, true) == AccessResult::Allowed);
  CHECK(checkAccess(credential, tables, now + 30, nullptr, csrf, false) == AccessResult::Unauthenticated);
  std::memcpy(wrong, session, kTokenSize);
  wrong[0] ^= 0x80;
  CHECK(checkAccess(credential, tables, now + 30, wrong, csrf, false) == AccessResult::Unauthenticated);

  // Cookie parsing: exact 32 hex digits of ht_session only.
  char session_hex[kTokenHexSize];
  ht_crypto::hexEncode(session, kTokenSize, session_hex, sizeof(session_hex));
  char cookie[160];
  std::snprintf(cookie, sizeof(cookie), "theme=dark; ht_session=%s; other=1", session_hex);
  uint8_t parsed[kTokenSize];
  CHECK(parseSessionCookie(cookie, parsed) && std::memcmp(parsed, session, kTokenSize) == 0);
  std::snprintf(cookie, sizeof(cookie), "ht_session=%s", session_hex);
  CHECK(parseSessionCookie(cookie, parsed));
  std::snprintf(cookie, sizeof(cookie), "xht_session=%s", session_hex);
  CHECK(!parseSessionCookie(cookie, parsed));
  std::snprintf(cookie, sizeof(cookie), "ht_session=%sa", session_hex);
  CHECK(!parseSessionCookie(cookie, parsed));
  CHECK(!parseSessionCookie("ht_session=", parsed));
  CHECK(!parseSessionCookie(nullptr, parsed));

  // No idle timeout; a session ends after 30 days. Only the id hash is kept.
  uint8_t id_hash[ht_crypto::kSha256Size];
  ht_crypto::sha256(session, kTokenSize, id_hash);
  CHECK(std::memcmp(tables.sessions[0].id_hash, id_hash, sizeof(id_hash)) == 0);
  CHECK(std::memcmp(tables.sessions[0].id_hash, session, kTokenSize) != 0);
  CHECK(checkAccess(credential, tables, now + 30 + 7UL * 24UL * 3600UL * 1000UL, session, nullptr, false) == AccessResult::Allowed);
  CHECK(checkAccess(credential, tables, now + 30 + kSessionMaxMs, session, nullptr, false) == AccessResult::Unauthenticated);

  // Expired nonce (60 s) is rejected and not counted as a guess.
  issueNonce(tables, now, fake_random, nonce);
  computeProof(credential, nonce, proof);
  CHECK(login(credential, tables, now + kNonceLifetimeMs, fake_random, nonce, proof, session, csrf, server_proof, &retry) == LoginResult::UnknownNonce);
  CHECK(tables.failures == 0);

  // Wrong proofs: three free attempts, then a doubling lockout up to 5 min.
  uint32_t t = now + 100000;
  uint32_t expected_lock[] = {0, 0, 1000, 2000, 4000, 8000, 16000, 32000, 64000, 128000, 256000, 300000, 300000};
  for (unsigned attempt = 0; attempt < sizeof(expected_lock) / sizeof(expected_lock[0]); ++attempt) {
    issueNonce(tables, t, fake_random, nonce);
    computeProof(credential, nonce, proof);
    proof[0] ^= 1;
    LoginResult result = login(credential, tables, t, fake_random, nonce, proof, session, csrf, server_proof, &retry);
    CHECK(result == LoginResult::InvalidProof);
    CHECK(retry == expected_lock[attempt]);
    // During the lockout even the right password is refused without checking.
    if (retry) {
      issueNonce(tables, t + 1, fake_random, nonce);
      computeProof(credential, nonce, proof);
      CHECK(login(credential, tables, t + 1, fake_random, nonce, proof, session, csrf, server_proof, &retry) == LoginResult::Locked);
      CHECK(retry == expected_lock[attempt] - 1);
      t += expected_lock[attempt];
    }
  }
  // After the lockout the right password works and resets the counter.
  issueNonce(tables, t, fake_random, nonce);
  computeProof(credential, nonce, proof);
  CHECK(login(credential, tables, t, fake_random, nonce, proof, session, csrf, server_proof, &retry) == LoginResult::Success);
  CHECK(tables.failures == 0 && !tables.locked);

  // Only four outstanding nonces: the oldest one is replaced.
  uint8_t first[kNonceSize];
  issueNonce(tables, t, fake_random, first);
  for (int i = 0; i < 4; ++i) issueNonce(tables, t + 1 + i, fake_random, nonce);
  computeProof(credential, first, proof);
  CHECK(login(credential, tables, t + 10, fake_random, first, proof, session, csrf, server_proof, &retry) == LoginResult::UnknownNonce);

  // Up to four sessions; a fifth login ends the least recently used one.
  uint8_t sessions[5][kTokenSize];
  for (int i = 0; i < 5; ++i) {
    issueNonce(tables, t + 100 + i, fake_random, nonce);
    computeProof(credential, nonce, proof);
    CHECK(login(credential, tables, t + 100 + i, fake_random, nonce, proof, sessions[i], csrf, server_proof, &retry) == LoginResult::Success);
  }
  CHECK(checkAccess(credential, tables, t + 200, sessions[0], nullptr, false) == AccessResult::Unauthenticated);
  CHECK(checkAccess(credential, tables, t + 200, sessions[4], nullptr, false) == AccessResult::Allowed);
  CHECK(endSession(tables, sessions[4]));
  CHECK(checkAccess(credential, tables, t + 201, sessions[4], nullptr, false) == AccessResult::Unauthenticated);

  // Stored sessions survive a restart: only sessions with a wall-clock expiry
  // are written, and a restored one ends by the clock.
  const uint32_t epoch = 1800000000u;
  findSession(tables, t + 202, sessions[3])->expires_epoch = epoch + kSessionMaxSeconds;
  findSession(tables, t + 202, sessions[2])->expires_epoch = epoch + 10;
  SessionRecord stored = makeSessionRecord(tables);
  CHECK(stored.count == 2);
  CHECK(sizeof(SessionRecord) == 220);
  Tables rebooted;
  clearTables(rebooted);
  CHECK(applySessionRecord(stored, rebooted, 5, 0) == 2);  // clock not set yet
  CHECK(checkAccess(credential, rebooted, 6, sessions[3], nullptr, false) == AccessResult::Allowed);
  CHECK(checkAccess(credential, rebooted, 6, sessions[1], nullptr, false) == AccessResult::Unauthenticated);
  CHECK(expireSessions(rebooted, epoch + 10));
  CHECK(checkAccess(credential, rebooted, 7, sessions[2], nullptr, false) == AccessResult::Unauthenticated);
  CHECK(checkAccess(credential, rebooted, 7, sessions[3], nullptr, false) == AccessResult::Allowed);
  clearTables(rebooted);
  CHECK(applySessionRecord(stored, rebooted, 5, epoch + kSessionMaxSeconds) == 0);  // all expired
  clearTables(rebooted);
  CHECK(applySessionRecord(stored, rebooted, 5, epoch - 5) == 1);  // more than 30 days ahead
  stored.entries[0].csrf[0] ^= 1;
  clearTables(rebooted);
  CHECK(applySessionRecord(stored, rebooted, 5, 0) == 0);  // checksum

  // Disabled protection: everything is allowed, login reports Disabled.
  Credential off;
  CHECK(checkAccess(off, tables, t, nullptr, nullptr, true) == AccessResult::Allowed);
  CHECK(login(off, tables, t, fake_random, nonce, proof, session, csrf, server_proof, &retry) == LoginResult::Disabled);

  // JSON body fields of the auth endpoints.
  char field[80];
  CHECK(jsonStringField("{\"nonce\" : \"abcd\", \"proof\":\"ef01\"}", "proof", field, sizeof(field)) && std::strcmp(field, "ef01") == 0);
  CHECK(!jsonStringField("{\"nonce\":\"ab\\\"cd\"}", "nonce", field, sizeof(field)));
  CHECK(!jsonStringField("{\"nonce\":12}", "nonce", field, sizeof(field)));
  CHECK(jsonTrueField("{\"disable\": true}", "disable"));
  CHECK(!jsonTrueField("{\"disable\": false}", "disable"));
  uint32_t number = 0;
  CHECK(jsonUintField("{\"salt\":\"ab\",\"iter\" : 300000,\"key\":\"cd\"}", "iter", &number) && number == 300000);
  CHECK(jsonUintField("{\"iter\":4294967295}", "iter", &number) && number == 4294967295u);
  CHECK(jsonUintField("{\"iter\":0}", "iter", &number) && number == 0);
  CHECK(!jsonUintField("{\"iter\":4294967296}", "iter", &number));
  CHECK(!jsonUintField("{\"iter\":12345678901}", "iter", &number));
  CHECK(!jsonUintField("{\"iter\":0300000}", "iter", &number));
  CHECK(!jsonUintField("{\"iter\":-1}", "iter", &number));
  CHECK(!jsonUintField("{\"iter\":1e5}", "iter", &number));
  CHECK(!jsonUintField("{\"iter\":1.5}", "iter", &number));
  CHECK(!jsonUintField("{\"iter\":\"300000\"}", "iter", &number));
  CHECK(!jsonUintField("{\"v\":12abc}", "v", &number));
  CHECK(!jsonUintField("{\"key\":\"ab\"}", "iter", &number));
  CHECK(!validIterations(9999) && validIterations(10000) && validIterations(1000000) && !validIterations(1000001));

  // Stored record: round trip, checksum, version and iteration validation.
  CHECK(sizeof(CredentialRecord) == 64);
  CredentialRecord record = makeRecord(credential);
  Credential restored;
  CHECK(applyRecord(record, restored) && restored.enabled && restored.iterations == 10000 &&
        std::memcmp(restored.key, key, kKeySize) == 0);
  record.key[5] ^= 1;
  CHECK(!applyRecord(record, restored));
  record = makeRecord(credential);
  record.version = 1;  // The former single SHA-256 record.
  record.checksum = recordChecksum(record);
  CHECK(!applyRecord(record, restored));
  const uint32_t out_of_range[] = {0u, 9999u, 1000001u};
  for (uint32_t iterations : out_of_range) {
    Credential odd = credential;
    odd.iterations = iterations;
    record = makeRecord(odd);
    CHECK(!applyRecord(record, restored));
  }
  std::printf("ok\n");
  return 0;
}
`.replace('SALT', cArray(salt)).replace('KEY', cArray(key));

const stdout = compileAndRun({
  label: 'Web Admin login core harness',
  harness,
  sources: ['src/core/security/ht_crypto.cpp']
});
if (stdout !== null) {
  const values = Object.fromEntries(stdout.trim().split('\n')
    .filter(line => line.includes(' ')).map(line => line.split(' ')));
  assert.equal(values.ok, undefined);
  assert.ok(stdout.trim().endsWith('ok'), stdout);
  const nonce = Buffer.from(values.nonce, 'hex');
  const proof = createHmac('sha256', key).update(nonce).digest();
  assert.equal(values.proof, proof.toString('hex'), 'proof = HMAC-SHA256(PBKDF2(password, salt, iter), nonce)');
  const serverProof = createHmac('sha256', key)
    .update(Buffer.concat([Buffer.from('HomeTiles-Web-Admin-server-v1'), nonce, proof])).digest('hex');
  assert.equal(values.server, serverProof, 'server proof binds label, nonce and client proof');
  console.log('Web Admin login core: proofs, nonces, lockout, sessions and CSRF passed');
}

// Route contract: only the login page, auth endpoints and static assets are
// reachable without the password gate.
const routes = readRepoFile('src/web/server/web_admin.cpp');
const registrations = [...routes.matchAll(/server\.on\(\s*([^,]+),([\s\S]*?)\);\n/g)];
assert.ok(registrations.length > 60, 'every route registration is parsed');
const publicRoutes = new Set([
  '"/"', '"/assets/inter-4.1-regular.woff2"', '"/assets/inter-4.1-semibold.woff2"',
  'adminCssAssetPath()', 'authJsAssetPath()', '"/api/auth/challenge"',
  '"/api/auth/login"', '"/api/auth/logout"', '"/api/auth/password"',
  // The Bridge address for the direct link: gated by the Pair button on the
  // panel instead of the password (docs-dev/bridge-link.md, Setup).
  '"/api/link"'
]);
{
  const handlers = readRepoFile('src/web/server/handlers/web_admin_handlers.cpp');
  const linkSetup = handlers.slice(handlers.indexOf('void WebAdminServer::handleLinkSetup() {'));
  assert.match(linkSetup, /^void WebAdminServer::handleLinkSetup\(\) \{\s*if \(!command_channel::linkWindowOpen\(\)\) \{\s*sendJsonError\(server, 403,/,
    '/api/link answers only within the two minutes after Pair was pressed on the panel');
}
for (const [, path, rest] of registrations) {
  const route = path.trim();
  if (publicRoutes.has(route)) continue;
  assert.match(rest, /guarded(?:Upload|Raw|UploadDone)?\(/, `${route} must pass the password gate`);
  if (/HTTP_POST,\s*\n?\s*guardedUploadDone/.test(rest)) {
    assert.match(rest, /guarded(Upload|Raw)\(/, `${route} upload chunks must be gated`);
  }
}
for (const route of ['"/mqtt"', '"/restart"', '"/api/ota/upload"', '"/api/files/upload"',
  '"/api/screenshot/download"', '"/api/files/download"', 'adminJsAssetPath()']) {
  assert.ok(registrations.some(([, path, rest]) => path.trim() === route && /guarded/.test(rest)),
    `${route} is protected`);
}
assert.match(routes, /"Cookie",\s*\n\s*"X-HomeTiles-CSRF"/, 'cookie and CSRF headers are collected');
assert.match(routes, /getLoginPage\(\)/, 'the root page falls back to the login page');

const handlers = readRepoFile('src/web/server/handlers/web_admin_auth_handlers.cpp');
assert.match(handlers, /cookie \+= "; Path=\/; Max-Age=";\s*cookie \+= String\(web_admin_auth::kSessionMaxSeconds\);\s*cookie \+= "; HttpOnly; SameSite=Lax";/,
  'the session cookie lasts 30 days and survives a link from Home Assistant');
assert.doesNotMatch(handlers, /SameSite=Strict/);
assert.match(handlers, /"Retry-After"/, 'lockout reports Retry-After');
assert.ok(handlers.includes('json += "\\",\\"iter\\":";\n  json += iterations;'),
  'the challenge returns the stored PBKDF2 iterations');
assert.match(handlers, /readUintArg\(server, body, "iter", &iterations\) \|\|\s*!web_admin_auth::validIterations\(iterations\)\) \{\s*ht_crypto::secureZero\(key, sizeof\(key\)\);\s*sendJsonError\(server, 400/,
  'setting a password requires an iteration count in range');
assert.doesNotMatch(handlers, /Serial\.printf?\([^;]*(proof|key|session_hex|csrf_hex)/,
  'secrets are never logged');
const module = readRepoFile('src/web/server/auth/web_admin_auth.cpp');
assert.match(module, /MALLOC_CAP_SPIRAM/, 'login state prefers PSRAM');
assert.match(module, /Fail closed/, 'a damaged record keeps Web Admin locked');

// The Settings section never submits the plain password with the /mqtt form.
const section = readRepoFile('src/web/server/render/web_admin_security_html.cpp');
for (const id of ['web_auth_password', 'web_auth_password_repeat']) {
  const input = section.match(new RegExp(`<input[^>]*id="${id}"[^>]*>`));
  assert.ok(input, `${id} exists`);
  assert.doesNotMatch(input[0], /\sname=/, `${id} has no name attribute`);
}
// Display text comes from the central i18n tables.
for (const key of ['web_auth_login_title', 'web_auth_password_label', 'web_auth_login_button',
  'web_auth_wrong_password', 'web_auth_wait_fmt', 'web_auth_forgot', 'web_auth_section']) {
  assert.match(section, new RegExp(`tr\\.${key}`), `${key} is rendered from i18n`);
}
// The password section opens the Settings form, above the network settings,
// and the header badge shows in red or green whether a password is set.
const adminHtml = readRepoFile('src/web/server/render/web_admin_html.cpp');
const settingsForm = adminHtml.slice(adminHtml.indexOf('<form id="admin_settings_form"'));
assert.ok(settingsForm.indexOf('appendWebAdminPasswordSettingsHtml(html, tr);') <
  settingsForm.indexOf('tr.admin_settings_wifi'), 'the password section comes before Wi-Fi');
assert.equal((adminHtml.match(/appendWebAdminPasswordSettingsHtml\(html, tr\);/g) || []).length, 1,
  'the password section is rendered once');
const brandLinks = adminHtml.slice(adminHtml.indexOf('<div class="brand-links">'),
  adminHtml.indexOf('<!-- Tab Navigation -->'));
assert.match(brandLinks, /appendWebAdminPasswordBadgeHtml\(html, tr\);/);
assert.match(brandLinks, /href="https:\/\/buymeacoffee\.com\/galusperes"/);
// A small support line under the links: "Support me with Stars or Buy Me a
// Coffee", with translated words around the two links.
const support = adminHtml.slice(adminHtml.indexOf('<div class="brand-support">'),
  adminHtml.indexOf('<!-- Tab Navigation -->'));
for (const key of ['web_support_prefix', 'web_support_stars', 'web_support_or']) {
  assert.match(support, new RegExp(`appendHtmlEscaped\\(html, String\\(tr\\.${key}\\)\\);`), `${key} comes from i18n`);
}
assert.match(support, /class="brand-star" href="https:\/\/github\.com\/GalusPeres\/HomeTiles"[^>]*><i class="mdi mdi-star">/);
assert.match(support, /class="brand-coffee" href="https:\/\/buymeacoffee\.com\/galusperes"/);
const i18nSource = readRepoFile('src/core/i18n/i18n.cpp');
assert.match(i18nSource, /"Kein Passwort",\s*"Unterstütze mich mit",\s*"Stars",\s*"oder"[,}]/);
assert.match(i18nSource, /"No password",\s*"Support me with",\s*"Stars",\s*"or"[,}]/);
assert.match(i18nSource, /"Aucun mot de passe",\s*"Soutenez-moi avec",\s*"des étoiles",\s*"ou"[,}]/);
assert.match(readRepoFile('src/web/assets/admin.css'), /\.brand-support \{[^}]*font-size:12px;/,
  'the support line stays small');
const badge = section.slice(section.indexOf('void appendWebAdminPasswordBadgeHtml('));
assert.match(badge, /enabled \? "is-on" : "is-off"/);
assert.match(badge, /mdi-shield-lock/);
assert.match(badge, /enabled \? tr\.web_auth_badge_on : tr\.web_auth_badge_off/);
const adminCss = readRepoFile('src/web/assets/admin.css');
assert.match(adminCss, /\.brand-security\.is-on[^{]*\{ color:#51cf66;/);
assert.match(adminCss, /\.brand-security\.is-off[^{]*\{ color:#ff6b6b;/);
console.log('Web Admin route gate, cookie flags and settings markup passed');
