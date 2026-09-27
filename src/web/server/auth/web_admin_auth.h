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

// Stores a new salt/key pair (enabling or changing the password). Every
// existing session and pending login challenge ends.
bool setCredential(const uint8_t salt[kSaltSize], const uint8_t key[kKeySize]);

// Removes the password, from Web Admin or from the device Settings.
bool clearCredential();

// Issues a single-use login nonce and returns the stored salt.
bool challenge(uint8_t nonce_out[kNonceSize], uint8_t salt_out[kSaltSize]);

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

}  // namespace web_admin_auth
