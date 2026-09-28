#include "src/web/server/web_admin.h"
#include "src/web/server/auth/web_admin_auth.h"
#include "src/web/server/handlers/web_admin_handler_utils.h"

using namespace web_admin_handlers;

namespace {

// Status of the current upload request: an unauthorized upload is read and
// discarded chunk by chunk and never reaches the OTA, icon or file writer.
bool g_upload_authorized = false;

bool isMutatingMethod(HTTPMethod method) {
  return method != HTTP_GET && method != HTTP_HEAD;
}

void sendAuthError(WebServer& server, int code, const char* error,
                   const char* reason) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-HomeTiles-Auth", reason);
  String json = "{\"error\":\"";
  json += error;
  json += "\"}";
  server.send(code, "application/json", json);
}

// Reads a hex field from a JSON body or, for form posts, from the form field.
bool readHexArg(WebServer& server, const String& body, const char* key,
                uint8_t* out, size_t out_length) {
  char text[web_admin_auth::kNonceSize * 2 + 1] = {};
  if (out_length * 2 + 1 > sizeof(text)) return false;
  bool found = web_admin_auth::jsonStringField(body.c_str(), key, text,
                                               sizeof(text));
  if (!found && server.hasArg(key)) {
    const String value = server.arg(key);
    if (value.length() < sizeof(text)) {
      memcpy(text, value.c_str(), value.length());
      text[value.length()] = '\0';
      found = true;
    }
  }
  return found && ht_crypto::hexDecode(text, strlen(text), out, out_length);
}

// Reads an unsigned number from a JSON body or, for form posts, the form field.
bool readUintArg(WebServer& server, const String& body, const char* key,
                 uint32_t* out) {
  if (web_admin_auth::jsonUintField(body.c_str(), key, out)) return true;
  if (!server.hasArg(key)) return false;
  const String wrapped = String("{\"v\":") + server.arg(key) + "}";
  return web_admin_auth::jsonUintField(wrapped.c_str(), "v", out);
}

}  // namespace

bool WebAdminServer::authorizeRequest() {
  if (!web_admin_auth::enabled()) return true;
  const String cookie = server.header("Cookie");
  const String csrf = server.header("X-HomeTiles-CSRF");
  const web_admin_auth::AccessResult result = web_admin_auth::checkRequest(
      cookie.c_str(), csrf.c_str(), isMutatingMethod(server.method()));
  if (result == web_admin_auth::AccessResult::Allowed) return true;
  if (result == web_admin_auth::AccessResult::CsrfMismatch) {
    sendAuthError(server, 403, "csrf_invalid", "csrf");
  } else {
    sendAuthError(server, 401, "auth_required", "required");
  }
  return false;
}

bool WebAdminServer::authorizeUploadChunk(bool starting) {
  if (starting) {
    if (!web_admin_auth::enabled()) {
      g_upload_authorized = true;
    } else {
      const String cookie = server.header("Cookie");
      const String csrf = server.header("X-HomeTiles-CSRF");
      g_upload_authorized =
          web_admin_auth::checkRequest(cookie.c_str(), csrf.c_str(), true) ==
          web_admin_auth::AccessResult::Allowed;
      if (!g_upload_authorized) {
        Serial.println("[WebAuth] Upload rejected: not logged in");
      }
    }
  }
  return g_upload_authorized;
}

void WebAdminServer::finishUploadRequest() {
  g_upload_authorized = false;
}

void WebAdminServer::handleAuthChallenge() {
  server.sendHeader("Cache-Control", "no-store");
  uint8_t nonce[web_admin_auth::kNonceSize];
  uint8_t salt[web_admin_auth::kSaltSize];
  uint32_t iterations = 0;
  if (!web_admin_auth::challenge(nonce, salt, &iterations)) {
    server.send(200, "application/json", "{\"enabled\":false}");
    return;
  }
  char nonce_hex[web_admin_auth::kNonceSize * 2 + 1];
  char salt_hex[web_admin_auth::kSaltSize * 2 + 1];
  ht_crypto::hexEncode(nonce, sizeof(nonce), nonce_hex, sizeof(nonce_hex));
  ht_crypto::hexEncode(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
  String json = "{\"enabled\":true,\"salt\":\"";
  json += salt_hex;
  json += "\",\"nonce\":\"";
  json += nonce_hex;
  json += "\",\"iter\":";
  json += iterations;
  json += "}";
  server.send(200, "application/json", json);
}

void WebAdminServer::handleAuthLogin() {
  webAdminMarkActivity();
  if (!web_admin_auth::enabled()) {
    sendAuthError(server, 409, "auth_disabled", "disabled");
    return;
  }
  const String body = server.arg("plain");
  uint8_t nonce[web_admin_auth::kNonceSize];
  uint8_t proof[web_admin_auth::kProofSize];
  if (!readHexArg(server, body, "nonce", nonce, sizeof(nonce)) ||
      !readHexArg(server, body, "proof", proof, sizeof(proof))) {
    sendAuthError(server, 400, "invalid_request", "invalid");
    return;
  }

  char session_hex[web_admin_auth::kTokenHexSize];
  char csrf_hex[web_admin_auth::kTokenHexSize];
  char server_proof_hex[web_admin_auth::kProofSize * 2 + 1];
  uint32_t retry_after_ms = 0;
  const web_admin_auth::LoginResult result = web_admin_auth::attemptLogin(
      nonce, proof, session_hex, csrf_hex, server_proof_hex, &retry_after_ms);
  ht_crypto::secureZero(proof, sizeof(proof));

  switch (result) {
    case web_admin_auth::LoginResult::Success: {
      String cookie = web_admin_auth::kSessionCookieName;
      cookie += "=";
      cookie += session_hex;
      cookie += "; Path=/; HttpOnly; SameSite=Strict";
      server.sendHeader("Set-Cookie", cookie);
      server.sendHeader("Cache-Control", "no-store");
      String json = "{\"csrf\":\"";
      json += csrf_hex;
      json += "\",\"server_proof\":\"";
      json += server_proof_hex;
      json += "\"}";
      server.send(200, "application/json", json);
      ht_crypto::secureZero(session_hex, sizeof(session_hex));
      ht_crypto::secureZero(csrf_hex, sizeof(csrf_hex));
      return;
    }
    case web_admin_auth::LoginResult::Locked: {
      const uint32_t seconds = (retry_after_ms + 999U) / 1000U;
      server.sendHeader("Retry-After", String(seconds));
      server.sendHeader("Cache-Control", "no-store");
      server.send(429, "application/json",
                  String("{\"error\":\"too_many_attempts\",\"retry_after\":") +
                      seconds + "}");
      return;
    }
    case web_admin_auth::LoginResult::UnknownNonce:
      sendAuthError(server, 401, "challenge_expired", "expired");
      return;
    case web_admin_auth::LoginResult::Disabled:
      sendAuthError(server, 409, "auth_disabled", "disabled");
      return;
    case web_admin_auth::LoginResult::InvalidProof:
    default: {
      server.sendHeader("Cache-Control", "no-store");
      String json = "{\"error\":\"invalid_password\"";
      if (retry_after_ms) {
        json += ",\"retry_after\":";
        json += (retry_after_ms + 999U) / 1000U;
      }
      json += "}";
      server.send(401, "application/json", json);
      return;
    }
  }
}

void WebAdminServer::handleAuthLogout() {
  if (!authorizeRequest()) return;
  const String cookie = server.header("Cookie");
  web_admin_auth::logout(cookie.c_str());
  String expired = web_admin_auth::kSessionCookieName;
  expired += "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict";
  server.sendHeader("Set-Cookie", expired);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"ok\":true}");
}

void WebAdminServer::handleAuthPassword() {
  webAdminMarkActivity();
  if (web_admin_auth::enabled()) {
    if (!authorizeRequest()) return;
  } else if (!server.header("X-HomeTiles-CSRF").length()) {
    // Without a password there is no session yet. Requiring the custom header
    // still stops a foreign web page from setting a password: a cross-site
    // request with this header needs a CORS preflight the panel never allows.
    sendAuthError(server, 403, "csrf_invalid", "csrf");
    return;
  }

  const String body = server.arg("plain");
  if (web_admin_auth::jsonTrueField(body.c_str(), "disable") ||
      server.arg("disable") == "1") {
    if (!web_admin_auth::clearCredential()) {
      sendJsonError(server, 500, "Could not remove the password");
      return;
    }
    String expired = web_admin_auth::kSessionCookieName;
    expired += "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict";
    server.sendHeader("Set-Cookie", expired);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", "{\"ok\":true,\"enabled\":false}");
    return;
  }

  uint8_t salt[web_admin_auth::kSaltSize];
  uint8_t key[web_admin_auth::kKeySize];
  uint32_t iterations = 0;
  if (!readHexArg(server, body, "salt", salt, sizeof(salt)) ||
      !readHexArg(server, body, "key", key, sizeof(key)) ||
      !readUintArg(server, body, "iter", &iterations) ||
      !web_admin_auth::validIterations(iterations)) {
    ht_crypto::secureZero(key, sizeof(key));
    sendJsonError(server, 400, "Invalid password data");
    return;
  }
  const bool saved = web_admin_auth::setCredential(salt, iterations, key);
  ht_crypto::secureZero(key, sizeof(key));
  if (!saved) {
    sendJsonError(server, 500, "Could not save the password");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"ok\":true,\"enabled\":true}");
}
