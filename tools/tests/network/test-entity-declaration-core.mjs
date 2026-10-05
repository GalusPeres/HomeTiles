// The panel's entity declaration (src/network/bridge/entity_declaration_core.h)
// runs on the host: the same tiles give the same version in any order, every
// part is valid JSON within the sealed size limit and carries the fields
// HomeTiles Bridge entity_search.parse_declaration expects, and values that
// are no entity ids never reach the Bridge.
import assert from 'node:assert/strict';

import {compileAndRun} from '../../lib/cpp-host.mjs';

const harness = String.raw`
#include <cstdio>
#include <string>
#include "src/network/bridge/entity_declaration_core.h"

using entity_declaration::Declaration;

static void print_parts(const char* tag, const Declaration& declaration, uint32_t version, bool web_auth) {
  size_t dropped = 0;
  for (const std::string& part : declaration.parts(version, web_auth, &dropped)) {
    std::printf("%s %s\n", tag, part.c_str());
  }
  std::printf("%s-dropped %u\n", tag, static_cast<unsigned>(dropped));
}

int main() {
  Declaration a;
  a.add("selects", "select.Netzteil");
  a.add("sensors", "sensor.kitchen");
  a.add("selects", "select.netzteil");
  a.add("sensors", "not an entity");
  a.add("sensors", "sensor.a\"b");
  a.add("sensors", "sensor.");
  a.add(nullptr, "sensor.x");
  a.add("sensors", "");
  Declaration b;
  b.add("sensors", "sensor.kitchen");
  b.add("selects", "select.netzteil");
  Declaration c;
  c.add("sensors", "sensor.kitchen");
  std::printf("count %u %u\n", static_cast<unsigned>(a.count()), static_cast<unsigned>(a.dropped()));
  std::printf("order %d\n", a.version(true) == b.version(true) ? 1 : 0);
  std::printf("auth %d\n", a.version(true) != a.version(false) ? 1 : 0);
  std::printf("differs %d\n", a.version(true) != c.version(true) ? 1 : 0);
  std::printf("version %u\n", static_cast<unsigned>(a.version(true)));
  print_parts("a", a, a.version(true), true);
  print_parts("empty", Declaration(), Declaration().version(false), false);

  // 250 entities with long ids (about 45 characters, more than most panels use).
  Declaration many;
  const char* lists[] = {"sensors", "switches", "binary_sensors"};
  const char* domains[] = {"sensor", "light", "binary_sensor"};
  for (int i = 0; i < 250; ++i) {
    const std::string id = std::string(domains[i % 3]) + ".entity_with_a_rather_long_object_id_" + std::to_string(i);
    many.add(lists[i % 3], id.c_str());
  }
  std::printf("many %u %u\n", static_cast<unsigned>(many.count()), static_cast<unsigned>(many.dropped()));
  print_parts("m", many, many.version(false), false);

  Declaration capped;
  for (int i = 0; i < 400; ++i) {
    const std::string id = "sensor.s" + std::to_string(i);
    capped.add("sensors", id.c_str());
  }
  std::printf("capped %u %u\n", static_cast<unsigned>(capped.count()), static_cast<unsigned>(capped.dropped()));
  print_parts("c", capped, capped.version(true), true);

  Declaration huge;
  for (int i = 0; i < 300; ++i) {
    const std::string id = "sensor." + std::string(200, 'x') + std::to_string(i);
    huge.add("sensors", id.c_str());
  }
  print_parts("h", huge, 7, true);
  return 0;
}
`;

const output = compileAndRun({label: 'Entity declaration core', harness});
if (output !== null) {
  const lines = output.trim().split('\n');
  const value = tag => lines.find(line => line.startsWith(tag + ' '))?.slice(tag.length + 1);
  const parts = tag => lines.filter(line => line.startsWith(tag + ' ')).map(line => JSON.parse(line.slice(tag.length + 1)));

  assert.equal(value('count'), '2 3', 'two entities; a sentence, a quote and a bare domain are left out');
  assert.equal(value('order'), '1', 'the same entities give the same version in any order');
  assert.equal(value('auth'), '1', 'the password claim is part of the version');
  assert.equal(value('differs'), '1', 'other entities, another version');

  // Every part: valid JSON with the fields the Bridge parses, within the limit.
  const checkParts = (tag, version, webAuth) => {
    const list = parts(tag);
    const raw = lines.filter(line => line.startsWith(tag + ' ')).map(line => line.slice(tag.length + 1));
    assert.ok(list.length >= 1 && list.length <= 8, `${tag}: 1 to 8 parts`);
    raw.forEach(text => assert.ok(Buffer.byteLength(text) <= 1800, `${tag}: ${Buffer.byteLength(text)} bytes`));
    list.forEach((part, index) => {
      assert.deepEqual(Object.keys(part), ['v', 'p', 'n', 'lists', 'web_auth']);
      assert.deepEqual([part.v, part.p, part.n, part.web_auth], [version, index, list.length, webAuth]);
      for (const ids of Object.values(part.lists)) assert.ok(Array.isArray(ids) && ids.length > 0);
    });
    return list;
  };

  const a = checkParts('a', Number(value('version')), true);
  assert.deepEqual(a[0].lists, {selects: ['select.netzteil'], sensors: ['sensor.kitchen']}, 'lower case, once');
  assert.deepEqual(checkParts('empty', 0, false)[0].lists, {}, 'no tiles: an empty declaration clears the Bridge');

  assert.equal(value('many'), '250 0');
  const many = checkParts('m', parts('m')[0].v, false);
  assert.ok(many.length > 1, 'split into parts');
  const declared = many.flatMap(part => Object.values(part.lists).flat());
  assert.equal(declared.length, 250);
  assert.equal(new Set(declared).size, 250, 'every entity exactly once across the parts');
  assert.equal(value('m-dropped'), '0', '250 long entity ids fit the eight parts');

  assert.equal(value('capped'), '300 100', 'at most 300 entities (entity_search.py MAX_PANEL_ENTITIES)');
  const capped = checkParts('c', parts('c')[0].v, true);
  assert.equal(new Set(capped.flatMap(part => part.lists.sensors)).size, 300);
  assert.equal(value('c-dropped'), '0');

  const huge = checkParts('h', 7, true);
  assert.equal(huge.length, 8, 'never more than eight parts');
  const kept = huge.flatMap(part => part.lists.sensors || []).length;
  assert.equal(kept + Number(value('h-dropped')), 300, 'what does not fit is counted, never cut into broken JSON');

  console.log('Entity declaration core: version, parts and entity ids pass');
}
