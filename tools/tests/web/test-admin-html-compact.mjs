// The Web Admin page is built from R"html(...)html" literals in flash. Their
// indentation cost about 7.9 KB without changing the page (HTML ignores it;
// newlines stay as separators). Keep the literals unindented so the saving
// lasts: a line inside them must not start with spaces or tabs.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

for (const file of ['src/web/server/render/web_admin_html.cpp',
                    'src/web/server/render/web_admin_security_html.cpp']) {
  const source = readRepoFile(file).replace(/\r\n?/g, '\n');
  const literals = [...source.matchAll(/R"html\(([\s\S]*?)\)html"/g)].map(match => match[1]);
  assert.ok(literals.length > 20, `${file} builds its HTML from raw literals`);
  const indented = literals.flatMap(body => body.split('\n').slice(1))
    .filter(line => /^[ \t]+\S/.test(line));
  assert.deepEqual(indented.slice(0, 5), [], `${file}: HTML lines start without indentation`);
  // Whitespace-sensitive elements would show removed spaces; there are none
  // across lines.
  assert.doesNotMatch(literals.join('\n'), /<pre\b|<textarea[^>]*>[^<]*\n/, `${file}: no multi-line pre or textarea`);
}
console.log('Web Admin HTML literals stay unindented.');
