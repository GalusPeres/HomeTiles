#include "src/web/server/assets/web_admin_assets.h"

#include <algorithm>

#include "src/web/generated/admin_assets_meta.h"

namespace {

static const uint8_t kAdminCssGzip[] PROGMEM = {
#include "src/web/generated/admin_css_gzip.inc"
};

static const uint8_t kAdminJsGzip[] PROGMEM = {
#include "src/web/generated/admin_js_gzip.inc"
};

static const uint8_t kAuthJsGzip[] PROGMEM = {
#include "src/web/generated/auth_js_gzip.inc"
};

static_assert(
    sizeof(kAdminCssGzip) == web_admin_assets_generated::kAdminCssGzipSize,
    "Generated admin CSS size mismatch");
static_assert(
    sizeof(kAdminJsGzip) == web_admin_assets_generated::kAdminJsGzipSize,
    "Generated admin JavaScript size mismatch");
static_assert(
    sizeof(kAuthJsGzip) == web_admin_assets_generated::kAdminAuthJsGzipSize,
    "Generated auth JavaScript size mismatch");

struct GzipWebAsset {
  const uint8_t* data;
  size_t size;
  const char* content_type;
  const char* etag;
};

void sendGzipProgmemAsset(WebServer& server,
                          const GzipWebAsset& asset,
                          size_t chunk_size = 512) {
  if (chunk_size < 256) chunk_size = 256;

  server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  server.sendHeader("ETag", asset.etag);
  server.sendHeader("Vary", "Accept-Encoding");
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  // A 304 may carry the Content-Length the corresponding 200 response would
  // have had. WebServer otherwise emits an incorrect zero length here.
  server.setContentLength(asset.size);

  const String if_none_match = server.header("If-None-Match");
  if (if_none_match == asset.etag || if_none_match == "*") {
    server.send(304, asset.content_type, "");
    return;
  }

  server.send(200, asset.content_type, "");

  for (size_t offset = 0; offset < asset.size; offset += chunk_size) {
    const size_t length = std::min(chunk_size, asset.size - offset);
    server.sendContent_P(
        reinterpret_cast<PGM_P>(asset.data + offset), length);
    // Keep the proven ESP-Hosted pacing. Compression makes the total pause
    // much shorter without increasing SDIO pressure per write.
    delay(2);
    yield();
  }
}

}  // namespace

const char* adminCssAssetPath() {
  return web_admin_assets_generated::kAdminCssPath;
}

const char* adminJsAssetPath() {
  return web_admin_assets_generated::kAdminJsPath;
}

const char* authJsAssetPath() {
  return web_admin_assets_generated::kAdminAuthJsPath;
}

void sendAdminCssAsset(WebServer& server) {
  const GzipWebAsset asset{
      kAdminCssGzip,
      sizeof(kAdminCssGzip),
      web_admin_assets_generated::kAdminCssContentType,
      web_admin_assets_generated::kAdminCssEtag};
  sendGzipProgmemAsset(server, asset);
}

void sendAdminJsAsset(WebServer& server) {
  const GzipWebAsset asset{
      kAdminJsGzip,
      sizeof(kAdminJsGzip),
      web_admin_assets_generated::kAdminJsContentType,
      web_admin_assets_generated::kAdminJsEtag};
  sendGzipProgmemAsset(server, asset);
}

void sendAuthJsAsset(WebServer& server) {
  const GzipWebAsset asset{
      kAuthJsGzip,
      sizeof(kAuthJsGzip),
      web_admin_assets_generated::kAdminAuthJsContentType,
      web_admin_assets_generated::kAdminAuthJsEtag};
  sendGzipProgmemAsset(server, asset);
}
