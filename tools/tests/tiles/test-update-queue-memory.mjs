// Drained tile update queues must release their payload allocations, including
// Arduino String capacity; empty text alone leaves the scarce heap occupied.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {repoRoot, readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const compiler = ['clang++', 'g++'].find(c => spawnSync(c, ['--version']).status === 0);
const core = [process.env.ARDUINO_ESP32_CORE,
  path.join(os.homedir(), 'AppData/Local/Arduino15/packages/esp32/hardware/esp32/3.3.7'),
  path.join(os.homedir(), '.arduino15/packages/esp32/hardware/esp32/3.3.7')]
  .filter(Boolean).map(p => path.join(p, 'cores/esp32'))
  .find(p => fs.existsSync(path.join(p, 'WString.cpp')));
if (!compiler || !core) {
  console.log('SKIP: Update queue memory test needs a C++ compiler and Arduino ESP32 3.3.7');
  process.exit(0);
}
const renderer = readRepoFile('src/tiles/runtime/tile_renderer.cpp');
const cover = readRepoFile('src/types/cover/renderer.cpp');
const strings = fs.readFileSync(path.join(core, 'WString.cpp'), 'utf8');
function definition(source, signature) {
  const start = source.indexOf(signature);
  assert.ok(start >= 0, signature);
  const masked = maskCpp(source);
  let end = masked.indexOf('{', start) + 1, depth = 1;
  for (; depth && end < masked.length; ++end) {
    if (masked[end] === '{') ++depth;
    if (masked[end] === '}') --depth;
  }
  assert.equal(depth, 0, signature);
  return source.slice(start, end);
}
const stringImplementation = [
  'String::String(const char *cstr)', 'String::String(const String &value)',
  'String::String(String &&rval)', 'String::~String()',
  'inline void String::init(void)', 'void String::invalidate(void)',
  'bool String::reserve(', 'bool String::changeBuffer(', 'String &String::copy(',
  'void String::move(', 'String &String::operator=(const String &rhs)',
  'String &String::operator=(String &&rval)', 'String &String::operator=(const char *cstr)',
  'bool String::equals(const char *cstr)', 'bool String::equalsIgnoreCase(',
  'void String::remove(unsigned int index)', 'void String::remove(unsigned int index, unsigned int count)',
].map(s => definition(strings, s)).join('\n');
const out = path.join(repoRoot, 'build/tests/update-queue-memory');
fs.mkdirSync(out, {recursive: true});
fs.writeFileSync(path.join(out, 'pgmspace.h'), '#pragma once\n#define PSTR(s) (s)\n');
const queueTypes = ['weather', 'media', 'climate', 'cover'];
const queueSources = queueTypes.map(type => {
  const text = type === 'cover' ? cover : renderer;
  const structName = `${type[0].toUpperCase()}${type.slice(1)}Update`;
  const declaration = definition(text, `struct ${structName} {`) + ';';
  const names = {weather: 'WEATHER_QUEUE_SIZE', media: 'MEDIA_QUEUE_SIZE', climate: 'CLIMATE_QUEUE_SIZE', cover: 'kQueueSize'};
  const size = text.match(new RegExp(`(?:static )?(?:const|constexpr) uint8_t ${names[type]} = (\\d+);`));
  assert.ok(size, type);
  const queue = type === 'cover' ? 'g_queue' : `g_${type}_queue`;
  const head = type === 'cover' ? 'g_queue_head' : `g_${type}_head`;
  const tail = type === 'cover' ? 'g_queue_tail' : `g_${type}_tail`;
  const consume = type === 'media' ? 'process_media_state_updates' : `process_${type}_update_queue`;
  const apply = type === 'cover' ? 'apply_state' : `update_${type}_tile_state`;
  return `namespace ${type} {
    ${declaration}
    constexpr uint8_t ${names[type]} = ${size[1]};
    ${structName} ${queue}[${names[type]}];
    uint8_t ${head}=0, ${tail}=0;
    uint32_t g_${type}_overflow_count=0;
    void ${apply}(GridType grid, uint8_t index, const char* payload) { record(grid,index,payload); }
    ${definition(text, `void queue_${type}_tile_update(`)}
    ${definition(text, `${type === 'media' ? 'static ' : ''}void ${consume}(`)}
    void enqueue(uint8_t i, const char* p) { queue_${type}_tile_update(GridType::TAB0,i,p); }
    void consume(uint8_t n=0) { ${consume}(n); }
  }`;
}).join('\n');
const source = `
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <map>
#include <string>
#include <vector>
#include "WString.h"
static std::map<void*,size_t> allocations;
void tracked_free(void* p) { allocations.erase(p); std::free(p); }
void* tracked_realloc(void* p,size_t n) {
  void* next=std::realloc(p,n);
  if(next) { allocations.erase(p); allocations[next]=n; }
  return next;
}
size_t retained() { size_t n=0; for(const auto& a:allocations)n+=a.second; return n; }
#define free tracked_free
#define realloc tracked_realloc
${stringImplementation}
#undef free
#undef realloc
enum class GridType : uint8_t { TAB0, TAB1 };
constexpr uint8_t TILES_PER_GRID=35;
struct SerialStub { void println(const char*) {} } Serial;
struct Received { uint8_t index; std::string payload; };
std::vector<Received> received;
void record(GridType grid,uint8_t index,const char* payload) {
  assert(grid==GridType::TAB0 && payload);
  received.push_back({index,payload});
}
${queueSources}
struct SwitchState { bool valid=true; };
struct Tile { int type=5; String sensor_entity="light.room"; } tile;
constexpr int TILE_SWITCH=5;
uint32_t generation=1;
uint32_t switch_layout_generation(GridType) { return generation; }
const Tile* tile_renderer_get_tile_config(GridType,uint8_t) { return &tile; }
SwitchState parse_switch_payload(const char* p) { assert(p && *p);return {}; }
bool switch_state_has_update(const SwitchState& s) { return s.valid; }
void apply_switch_tile_state(GridType g,uint8_t i,const SwitchState&) { record(g,i,"switch"); }
${definition(renderer, 'struct SwitchUpdate {')};
constexpr uint8_t SWITCH_QUEUE_SIZE=32;
SwitchUpdate g_switch_queue[SWITCH_QUEUE_SIZE];
uint8_t g_switch_head=0,g_switch_tail=0;
uint32_t g_switch_overflow_count=0;
${definition(renderer, 'static void enqueue_switch_update(')}
${definition(renderer, 'void process_switch_update_queue(')}
void expect_empty(const char* label) {
  if(retained()!=0) { fprintf(stderr,"%s: drained queue retains %zu payload bytes\\n",label,retained()); std::exit(1); }
}
int main(int argc,char** argv) {
  assert(argc==2);
  const std::string mode=argv[1];
  // Characterize the actual pinned Arduino allocator behavior, not std::string.
  { String s(std::string(15318,'w').c_str()); const size_t bytes=retained();
    s=""; assert(retained()==bytes); s=String(); assert(retained()==bytes);
    s=static_cast<const char*>(nullptr); assert(retained()==0);
  }
  if(mode=="switch") {
    const std::string payload(2048,'s'),entity(80,'e');
    String id(entity.c_str());
    enqueue_switch_update(GridType::TAB0,7,id,false,payload.c_str());
    id=static_cast<const char*>(nullptr);
    process_switch_update_queue(1);
    assert(received.size()==1 && retained()>0);
    process_switch_update_queue(0);
    assert(received.size()==3);
    expect_empty("switch fully consumed");
    enqueue_switch_update(GridType::TAB0,1,String(),false,payload.c_str());
    ++generation;
    process_switch_update_queue(0);
    assert(received.size()==3);
    expect_empty("switch stale layout");
    for(uint8_t i=0;i<34;++i) {
      enqueue_switch_update(GridType::TAB0,uint64_t{1}<<i,String(entity.c_str()),false,payload.c_str());
    }
    process_switch_update_queue(0);
    assert(received.size()==34);
    expect_empty("switch overflow drain");
    return 0;
  }
  using Enqueue=void(*)(uint8_t,const char*);
  using Consume=void(*)(uint8_t);
  Enqueue enqueue=nullptr;Consume consume=nullptr;
  ${queueTypes.map(type => `if(mode=="${type}"){enqueue=${type}::enqueue;consume=${type}::consume;}`).join('\n')}
  assert(enqueue && consume);
  const size_t bytes=mode=="weather"?15318:mode=="media"?5000:2048;
  const std::string first(bytes,'a'),latest(bytes,'b');
  // Real cached-grid return behavior: enqueue then drain repeatedly.
  for(int n=0;n<64;++n) {
    received.clear();enqueue(1,first.c_str());enqueue(1,latest.c_str());
    enqueue(2,first.c_str());consume(1);
    assert(received.size()==1 && received[0].index==1 && received[0].payload==latest);
    consume(0);
    assert(received.size()==2 && received[1].index==2 && received[1].payload==first);
    expect_empty(mode.c_str());
  }
  // Overflow drops the oldest payload and must not leave a stranded buffer.
  received.clear();
  for(uint8_t i=0;i<30;++i)enqueue(i,first.c_str());
  consume(0);assert(!received.empty());expect_empty("overflow drain");
}
`;
fs.writeFileSync(path.join(out, 'test.cpp'), source);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
const compile = spawnSync(compiler, ['-std=c++17', '-D__GXX_EXPERIMENTAL_CXX0X__', '-O0', '-I', out, '-I', core, path.join(out, 'test.cpp'), '-o', binary], {encoding:'utf8'});
assert.equal(compile.status, 0, compile.stdout + compile.stderr);
let failures = 0;
for (const type of [...queueTypes, 'switch']) {
  const run = spawnSync(binary, [type], {encoding:'utf8'});
  if (run.status !== 0) { ++failures; console.error(run.stdout + run.stderr); }
}
assert.equal(failures, 0, 'Drained tile queues must return all payload memory');
console.log('Tile update queues release Arduino String storage after drain/overflow: PASS');
