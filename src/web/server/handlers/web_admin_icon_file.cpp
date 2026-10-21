#include "src/web/server/web_admin.h"

#include <lwip/sockets.h>

#include "src/tiles/icons/mdi_icon_store.h"
#include "src/web/server/handlers/web_admin_handler_utils.h"

using namespace web_admin_handlers;

// The System page's Icons card (src/web/admin/firmware/icon-file.js).

namespace {

// An error of the running upload; the done handler answers with it.
String g_icon_upload_error;
bool g_icon_upload_started = false;

}  // namespace

// "Update" on the card: the panel downloads its icon file now; the loop
// takes the request and restarts the panel afterwards.
void WebAdminServer::handleIconFileDownload() {
  mdi_icon_store::request_download();
  Serial.println("[Icons] Download requested in the Web Admin");
  server.send(200, "application/json", "{\"success\":true}");
}

// The raw body of an uploaded icon file goes into PSRAM; the header is
// checked as soon as it arrived, the bitmaps' CRC at the end.
void WebAdminServer::handleIconFileUpload() {
  HTTPRaw& raw = server.raw();
  if (raw.status == RAW_START) {
    // A small receive window, like the firmware upload: the default lets the
    // browser send 64 KB ahead into the internal DMA buffers.
    int receive_buffer_bytes = 8 * 1024;
    server.client().setSocketOption(SOL_SOCKET, SO_RCVBUF, &receive_buffer_bytes,
                                    sizeof(receive_buffer_bytes));
    g_icon_upload_error = "";
    g_icon_upload_started = true;
    if (!mdi_icon_store::upload_begin(g_icon_upload_error)) {
      Serial.printf("[Icons] Upload refused: %s\n", g_icon_upload_error.c_str());
    }
    return;
  }
  if (raw.status == RAW_WRITE) {
    if (g_icon_upload_error.length()) return;
    if (!mdi_icon_store::upload_write(raw.buf, raw.currentSize, g_icon_upload_error)) {
      Serial.printf("[Icons] Upload refused: %s\n", g_icon_upload_error.c_str());
      mdi_icon_store::upload_discard();
    }
    return;
  }
  if (raw.status == RAW_ABORTED) {
    g_icon_upload_error = "icon upload aborted";
    mdi_icon_store::upload_discard();
    Serial.println("[Icons] Upload aborted");
    return;
  }
  if (raw.status == RAW_END) {
    if (g_icon_upload_error.length()) return;
    if (!mdi_icon_store::upload_finish(g_icon_upload_error)) {
      Serial.printf("[Icons] Upload refused: %s\n", g_icon_upload_error.c_str());
      mdi_icon_store::upload_discard();
      return;
    }
    Serial.printf("[Icons] Upload of %u bytes complete, installing\n",
                  static_cast<unsigned>(raw.totalSize));
  }
}

void WebAdminServer::handleIconFileUploadDone() {
  if (!g_icon_upload_started) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"No icon upload started\"}");
    return;
  }
  g_icon_upload_started = false;
  if (g_icon_upload_error.length()) {
    String json = "{\"success\":false,\"error\":\"";
    appendJsonEscaped(json, g_icon_upload_error);
    json += "\"}";
    g_icon_upload_error = "";
    server.send(400, "application/json", json);
    return;
  }
  // The loop installs the file and restarts the panel.
  server.send(200, "application/json", "{\"success\":true}");
}
