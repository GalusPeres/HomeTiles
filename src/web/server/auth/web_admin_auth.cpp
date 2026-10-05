#include "src/web/server/auth/web_admin_auth.h"

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <time.h>

#include "src/core/config/batched_nvs_write.h"
#include "src/core/security/secure_random.h"
#include "src/devices/device.h"

namespace web_admin_auth {

namespace {

constexpr const char* kNamespace = "tab5_config";
// NVS keys are limited to 15 characters.
constexpr const char* kRecordKey = "web_auth";
constexpr const char* kSessionsKey = "web_sessions";

bool g_loaded = false;
Credential g_credential;
// Allocated only while a password is set; PSRAM keeps the ~0.3 KiB of login
// state out of the internal RAM that S3 and P4 network paths depend on.
Tables* g_tables = nullptr;
// True while NVS holds a session record, so a logout without stored sessions
// does not touch the flash.
bool g_sessions_stored = false;

void fillRandom(uint8_t* out, size_t length) {
  secure_random::fill(out, length);
}

Tables* tables() {
  if (g_tables) return g_tables;
  void* block = heap_caps_calloc(1, sizeof(Tables),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!block) block = heap_caps_calloc(1, sizeof(Tables), MALLOC_CAP_8BIT);
  g_tables = static_cast<Tables*>(block);
  if (!g_tables) Serial.println("[WebAuth] Could not allocate login state");
  return g_tables;
}

void releaseTables() {
  if (!g_tables) return;
  clearTables(*g_tables);
  heap_caps_free(g_tables);
  g_tables = nullptr;
}

bool writeRecord(const Credential* credential) {
  Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.println("[WebAuth] Could not open preferences");
    return false;
  }
  bool written = true;
  if (credential) {
    CredentialRecord record = makeRecord(*credential);
    written = prefs.putBytes(kRecordKey, &record, sizeof(record)) == sizeof(record);
    ht_crypto::secureZero(&record, sizeof(record));
  } else {
    prefs.remove(kRecordKey);
  }
  const bool committed = BatchedNvsWrite::finish(prefs);
  return written && committed;
}

// Wall-clock seconds, or 0 while SNTP has not set the clock.
uint32_t epochNow() {
  const time_t now = time(nullptr);
  return now >= static_cast<time_t>(kMinValidEpoch) ? static_cast<uint32_t>(now) : 0;
}

// Stores the sessions that have an expiry, or removes the record when there
// is none. Called on login, logout and password changes only.
void writeSessions() {
  SessionRecord record{};
  if (g_tables) record = makeSessionRecord(*g_tables);
  if (!record.count && !g_sessions_stored) return;
  Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    ht_crypto::secureZero(&record, sizeof(record));
    Serial.println("[WebAuth] Could not open preferences");
    return;
  }
  bool written = true;
  if (record.count) {
    written = prefs.putBytes(kSessionsKey, &record, sizeof(record)) == sizeof(record);
  } else {
    prefs.remove(kSessionsKey);
  }
  if (BatchedNvsWrite::finish(prefs) && written) {
    g_sessions_stored = record.count != 0;
  } else {
    Serial.println("[WebAuth] Could not store the sessions");
  }
  ht_crypto::secureZero(&record, sizeof(record));
}

void loadSessions() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  SessionRecord record{};
  const size_t length = prefs.getBytesLength(kSessionsKey);
  const bool read = length == sizeof(record) &&
                    prefs.getBytes(kSessionsKey, &record, sizeof(record)) == sizeof(record);
  prefs.end();
  Tables* state = read ? tables() : nullptr;
  const size_t restored = state ? applySessionRecord(record, *state, millis(), epochNow()) : 0;
  ht_crypto::secureZero(&record, sizeof(record));
  if (restored) {
    Serial.printf("[WebAuth] %u session(s) kept across the restart\n",
                  static_cast<unsigned>(restored));
  }
}

}  // namespace

void begin() {
  if (g_loaded) return;
  g_loaded = true;
  g_credential = Credential();

  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  // Known even without a valid password, so setting or removing one always
  // deletes sessions of an earlier password.
  g_sessions_stored = prefs.getBytesLength(kSessionsKey) != 0;
  const size_t length = prefs.getBytesLength(kRecordKey);
  if (length == 0) {
    prefs.end();
    return;
  }
  CredentialRecord record{};
  const bool read = length == sizeof(record) &&
                    prefs.getBytes(kRecordKey, &record, sizeof(record)) ==
                        sizeof(record);
  prefs.end();
  if (read && applyRecord(record, g_credential)) {
    ht_crypto::secureZero(&record, sizeof(record));
    Serial.println("[WebAuth] Web Admin password is enabled");
    loadSessions();
    return;
  }
  ht_crypto::secureZero(&record, sizeof(record));
  // Fail closed: a damaged record keeps Web Admin locked with a key nobody
  // knows. The password can be removed in the device Settings.
  g_credential.enabled = true;
  fillRandom(g_credential.salt, kSaltSize);
  g_credential.iterations = kMinIterations;
  fillRandom(g_credential.key, kKeySize);
  Serial.println("[WebAuth] Stored password record is invalid; Web Admin stays "
                 "locked until the password is reset on the device");
}

namespace {
constexpr uint32_t kFirstPasswordWindowMs = 120000;
uint32_t g_first_password_since = 0;
bool g_first_password_open = false;
}  // namespace

void allowFirstPassword() {
  g_first_password_since = millis();
  g_first_password_open = true;
}

bool firstPasswordAllowed() {
  if (g_first_password_open &&
      static_cast<uint32_t>(millis() - g_first_password_since) >= kFirstPasswordWindowMs) {
    g_first_password_open = false;
  }
  return g_first_password_open;
}

bool enabled() {
  begin();
  return g_credential.enabled;
}

bool setCredential(const uint8_t salt[kSaltSize], uint32_t iterations,
                   const uint8_t key[kKeySize]) {
  begin();
  if (!salt || !key || !validIterations(iterations)) return false;
  Credential updated;
  updated.enabled = true;
  memcpy(updated.salt, salt, kSaltSize);
  updated.iterations = iterations;
  memcpy(updated.key, key, kKeySize);
  const bool saved = writeRecord(&updated);
  if (saved) {
    // Set: another first password needs a new tap on the panel.
    g_first_password_open = false;
    g_credential = updated;
    if (g_tables) clearTables(*g_tables);
    writeSessions();
    Serial.println("[WebAuth] Web Admin password set; all sessions ended");
  } else {
    Serial.println("[WebAuth] Could not save the Web Admin password");
  }
  ht_crypto::secureZero(&updated, sizeof(updated));
  return saved;
}

bool clearCredential() {
  begin();
  if (!writeRecord(nullptr)) {
    Serial.println("[WebAuth] Could not remove the Web Admin password");
    return false;
  }
  ht_crypto::secureZero(&g_credential, sizeof(g_credential));
  g_credential = Credential();
  releaseTables();
  writeSessions();
  Serial.println("[WebAuth] Web Admin password removed");
  return true;
}

bool challenge(uint8_t nonce_out[kNonceSize], uint8_t salt_out[kSaltSize],
               uint32_t* iterations_out) {
  begin();
  if (!g_credential.enabled || !iterations_out) return false;
  Tables* state = tables();
  if (!state) return false;
  issueNonce(*state, millis(), fillRandom, nonce_out);
  memcpy(salt_out, g_credential.salt, kSaltSize);
  *iterations_out = g_credential.iterations;
  return true;
}

LoginResult attemptLogin(const uint8_t nonce[kNonceSize],
                         const uint8_t proof[kProofSize],
                         char session_hex[kTokenHexSize],
                         char csrf_hex[kTokenHexSize],
                         char server_proof_hex[kProofSize * 2 + 1],
                         uint32_t* retry_after_ms) {
  begin();
  if (retry_after_ms) *retry_after_ms = 0;
  if (!g_credential.enabled) return LoginResult::Disabled;
  Tables* state = tables();
  if (!state) return LoginResult::UnknownNonce;

  uint8_t session_id[kTokenSize];
  uint8_t csrf[kTokenSize];
  uint8_t server_proof[kProofSize];
  const LoginResult result =
      login(g_credential, *state, millis(), fillRandom, nonce, proof,
            session_id, csrf, server_proof, retry_after_ms);
  if (result == LoginResult::Success) {
    // With a set clock the session survives restarts until its 30 days end;
    // the record is rewritten anyway, as the login may have replaced one.
    const uint32_t epoch = epochNow();
    SessionSlot* session = findSession(*state, millis(), session_id);
    if (session && epoch) session->expires_epoch = epoch + kSessionMaxSeconds;
    writeSessions();
    ht_crypto::hexEncode(session_id, kTokenSize, session_hex, kTokenHexSize);
    ht_crypto::hexEncode(csrf, kTokenSize, csrf_hex, kTokenHexSize);
    ht_crypto::hexEncode(server_proof, kProofSize, server_proof_hex,
                         kProofSize * 2 + 1);
    Serial.println("[WebAuth] Login succeeded");
  } else if (result == LoginResult::InvalidProof) {
    Serial.printf("[WebAuth] Login failed (%u consecutive, lockout %lu ms)\n",
                  static_cast<unsigned>(state->failures),
                  static_cast<unsigned long>(retry_after_ms ? *retry_after_ms : 0));
  }
  ht_crypto::secureZero(session_id, sizeof(session_id));
  ht_crypto::secureZero(csrf, sizeof(csrf));
  ht_crypto::secureZero(server_proof, sizeof(server_proof));
  return result;
}

AccessResult checkRequest(const char* cookie_header, const char* csrf_header,
                          bool mutating) {
  begin();
  if (!g_credential.enabled) return AccessResult::Allowed;
  Tables* state = tables();
  if (!state) return AccessResult::Unauthenticated;
  uint8_t session_id[kTokenSize];
  uint8_t csrf[kTokenSize];
  const bool has_session = parseSessionCookie(cookie_header, session_id);
  const bool has_csrf =
      csrf_header &&
      ht_crypto::hexDecode(csrf_header, strlen(csrf_header), csrf, kTokenSize);
  if (expireSessions(*state, epochNow())) writeSessions();
  const AccessResult result =
      checkAccess(g_credential, *state, millis(),
                  has_session ? session_id : nullptr,
                  has_csrf ? csrf : nullptr, mutating);
  ht_crypto::secureZero(session_id, sizeof(session_id));
  ht_crypto::secureZero(csrf, sizeof(csrf));
  return result;
}

bool csrfForCookie(const char* cookie_header, char csrf_hex[kTokenHexSize]) {
  if (csrf_hex) csrf_hex[0] = '\0';
  begin();
  if (!g_credential.enabled || !g_tables || !csrf_hex) return false;
  uint8_t session_id[kTokenSize];
  if (!parseSessionCookie(cookie_header, session_id)) return false;
  SessionSlot* session = findSession(*g_tables, millis(), session_id);
  ht_crypto::secureZero(session_id, sizeof(session_id));
  if (!session) return false;
  return ht_crypto::hexEncode(session->csrf, kTokenSize, csrf_hex,
                              kTokenHexSize);
}

bool storedSecretsHidden() {
  return enabled();
}

void logout(const char* cookie_header) {
  begin();
  if (!g_tables) return;
  uint8_t session_id[kTokenSize];
  if (parseSessionCookie(cookie_header, session_id) &&
      endSession(*g_tables, session_id)) {
    writeSessions();
  }
  ht_crypto::secureZero(session_id, sizeof(session_id));
}

}  // namespace web_admin_auth
