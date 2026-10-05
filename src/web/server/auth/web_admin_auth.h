#pragma once

#include <Arduino.h>

#include "src/web/server/auth/web_admin_auth_core.h"

// Optional Web Admin password. It is off by default; while it is off every
// page and endpoint behaves exactly as before. All functions run on the loop
// task (Web server handlers and device Settings), so no locking is needed.
namespace web_admin_auth {

// Loads the stored credential once; later calls return immediately.
void begin();
bool enabled();

// Stores a new salt, PBKDF2 iteration count and key (enabling or changing the
// password). Every existing session and pending login challenge ends. Counts
// outside kMinIterations..kMaxIterations are refused.
bool setCredential(const uint8_t salt[kSaltSize], uint32_t iterations,
                   const uint8_t key[kKeySize]);

// Removes the password, from Web Admin or from the device Settings.
bool clearCredential();

// The first password needs a tap on the panel: System > Security > Password
// opens a window of two minutes in which Web Admin may set it, like Pair for
// the Bridge address. Whoever sets it stands at the panel; the password then
// unlocks Lock, Alarm and the full entity search. A set password closes it.
void allowFirstPassword();
bool firstPasswordAllowed();

// Issues a single-use login nonce and returns the stored salt and iterations.
bool challenge(uint8_t nonce_out[kNonceSize], uint8_t salt_out[kSaltSize],
               uint32_t* iterations_out);

LoginResult attemptLogin(const uint8_t nonce[kNonceSize],
                         const uint8_t proof[kProofSize],
                         char session_hex[kTokenHexSize],
                         char csrf_hex[kTokenHexSize],
                         char server_proof_hex[kProofSize * 2 + 1],
                         uint32_t* retry_after_ms);

AccessResult checkRequest(const char* cookie_header, const char* csrf_header,
                          bool mutating);

// CSRF token of the session named by the Cookie header, for the admin page.
bool csrfForCookie(const char* cookie_header, char csrf_hex[kTokenHexSize]);

void logout(const char* cookie_header);

// While a password is set, stored Wi-Fi/MQTT passwords and PINs are never sent
// to a browser (pages, JSON replies, the setup portal); they can only be
// replaced with a new value.
bool storedSecretsHidden();

}  // namespace web_admin_auth
