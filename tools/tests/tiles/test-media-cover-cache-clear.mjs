// Issue #16: a player without artwork (for example a TV source) gets an
// explicit empty entity_picture from the Bridge. The entity cache carries the
// old cover block into payloads without pixels; it must not do so for that
// payload, or a grid reload brings the old cover back.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const merge = cppFunctionDefinitions(read('src/ui/tabs/tiles/tab_tiles_unified.cpp'))
  .find(f => f.name === 'merge_cached_cover_fields');
assert.ok(merge, 'tab_tiles_unified defines merge_cached_cover_fields');

const arduino = String.raw`
#pragma once
#include <cstring>
#include <string>
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string& s) : std::string(s) {}
  String(const char* s, size_t n) : std::string(s, n) {}
  int indexOf(const char* s) const { const size_t p = find(s); return p == npos ? -1 : int(p); }
  int lastIndexOf(char c) const { const size_t p = rfind(c); return p == npos ? -1 : int(p); }
  String substring(int a, int b) const { return String(std::string::substr(a, b - a)); }
  void trim() {
    const size_t a = find_first_not_of(" \r\n\t");
    if (a == npos) { clear(); return; }
    *this = String(std::string::substr(a, find_last_not_of(" \r\n\t") - a + 1));
  }
};
`;

const harness = String.raw`
#include "Arduino.h"
#include "src/types/media/artwork_payload.h"
#include <cstdio>

${merge.source}

static int fail(const char* what) { std::printf("FAIL %s\n", what); return 1; }

int main() {
  const String cached = R"({"state":"playing","media_title":"Song","entity_picture":"https://cdn/a.jpg","entity_picture_data":"QUJD","entity_picture_mime":"image/jpeg","entity_picture_bytes":3})";

  // state_fast and position ticks keep the cover of the same song.
  String fast = R"({"state":"playing","media_title":"Song","entity_picture":"https://cdn/a.jpg"})";
  if (!merge_cached_cover_fields(cached, fast) || fast.indexOf("\"entity_picture_data\":\"QUJD\"") < 0)
    return fail("URL-only payload keeps the cached cover");

  // Between two songs the Bridge leaves the picture out: keep the cover.
  String gap = R"({"state":"playing","media_title":"Next"})";
  if (!merge_cached_cover_fields(cached, gap) || gap.indexOf("\"entity_picture_data\"") < 0)
    return fail("payload without artwork fields keeps the cached cover");

  // A source without artwork clears the cover for good.
  for (const char* cleared : {
         R"({"state":"playing","source":"TV","entity_picture":""})",
         R"({"state":"playing","source":"TV","entity_picture":null})",
         R"({"state":"playing","source":"TV","media_image_url":""})",
         R"({"state":"off","entity_picture":" "})"}) {
    String payload = cleared;
    if (merge_cached_cover_fields(cached, payload) || payload != cleared)
      return fail(cleared);
  }

  // The renderer reads entity_picture first; a URL there wins.
  String url = R"({"state":"playing","entity_picture":"https://cdn/b.jpg","media_image_url":""})";
  if (!merge_cached_cover_fields(cached, url)) return fail("entity_picture URL keeps merging");
  std::printf("ok\n");
  return 0;
}
`;

const output = compileAndRun({label: 'Media cover cache clear', harness, files: {'Arduino.h': arduino}});
if (output !== null) {
  assert.equal(output.trim(), 'ok');
  console.log('Media cover cache keeps covers across updates and drops them on an explicit empty picture');
}
