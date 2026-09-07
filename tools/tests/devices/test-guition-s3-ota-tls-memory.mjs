import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/core/firmware/github_update.cpp'), 'utf8');
const functions = cppFunctionDefinitions(source);
const fn = name => {
  const result = functions.find(item => item.name === name);
  assert.ok(result, name);
  return result.source;
};
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(command => spawnSync(command, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: OTA TLS memory test needs a native C++ compiler'); process.exit(0); }
const allocator = source.slice(source.indexOf('void* checkTlsInternalCalloc('), source.indexOf('struct ParsedHttpsUrl'));
const cpp = String.raw`
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>
class String : public std::string {
 public:
  using std::string::string; using std::string::operator=; using std::string::operator+=;
  String() = default; String(const std::string& value):std::string(value) {}
  template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0> String(T v):std::string(std::to_string(v)) {}
  bool startsWith(const String& v) const { return rfind(v, 0) == 0; }
  int indexOf(char c, size_t from=0) const { auto p=find(c,from); return p==npos?-1:int(p); }
  int lastIndexOf(char c) const { auto p=rfind(c); return p==npos?-1:int(p); }
  String substring(size_t from, size_t to=npos) const { return substr(from,to==npos?npos:to-from); }
  void toLowerCase() { for(char& c:*this) c=std::tolower(static_cast<unsigned char>(c)); }
  void trim() { auto a=find_first_not_of(" \r\n\t"), b=find_last_not_of(" \r\n\t"); *this=a==npos?"":substr(a,b-a+1); }
};
template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
String operator+(const String& a, T b) { return std::string(a)+std::to_string(b); }
constexpr int MALLOC_CAP_INTERNAL=1, MALLOC_CAP_SPIRAM=2, MALLOC_CAP_8BIT=4, MALLOC_CAP_DMA=8;
struct Allocation { size_t bytes; bool external; };
std::map<void*, Allocation> allocations;
size_t internal_free, external_free, largest_internal, external_allocations;
void* heap_caps_calloc(size_t count, size_t size, int caps) {
  const size_t bytes=count*size; const bool external=caps&MALLOC_CAP_SPIRAM;
  size_t& free=external?external_free:internal_free;
  if(bytes>free || (!external && bytes>largest_internal)) return nullptr;
  void* p=std::calloc(count,size); assert(p); allocations[p]={bytes,external}; free-=bytes;
  if(external) ++external_allocations; return p;
}
void heap_caps_free(void* p) { if(!p)return; auto a=allocations.at(p); (a.external?external_free:internal_free)+=a.bytes; allocations.erase(p); std::free(p); }
void* coreCalloc(size_t n,size_t s) { return heap_caps_calloc(n,s,MALLOC_CAP_INTERNAL); }
auto tls_calloc=&coreCalloc; auto tls_free=&heap_caps_free;
int mbedtls_platform_set_calloc_free(void*(*alloc)(size_t,size_t),void(*release)(void*)) { tls_calloc=alloc; tls_free=release; return 0; }
uint32_t clock_ms=0; uint32_t millis(){ return clock_ms; } void delay(uint32_t n){ clock_ms+=n; } void yield(){++clock_ms;}
struct SerialStub { template<class... T> void printf(const char*,T...){} } Serial;
String diagnostics;
void installDiagLine(const String& line){ diagnostics+=line; diagnostics+='\n'; }
String memSnapshotLine(){ return "memory snapshot"; }
constexpr size_t kSocketRxBufferBytes=4096,kMaxHttpLineLen=4096;
constexpr uint32_t kConnectTimeoutMs=10000,kReadTimeoutMs=20000,kReadPaceMs=2;
constexpr int SOL_SOCKET=1,SO_RCVBUF=2;
std::vector<String> responses; size_t connection_index=0;
bool force_connect_failure=false;
class NetworkClientSecure {
  std::vector<void*> blocks; String response; size_t pos=0; bool online=false;
 public:
  ~NetworkClientSecure(){ stop(); }
  void setInsecure(){} void setTimeout(int){}
  bool connect(const char*,uint16_t,uint32_t) {
    if(force_connect_failure)return false;
    // Simulate TLS allocations under bounded/fragmented internal-memory pressure.
    // These sizes exercise the allocation contract, not a measured TLS transcript.
    for(size_t bytes:{size_t(16384),size_t(16384),size_t(20000)}) {
      void* p=tls_calloc(1,bytes); if(!p){stop();return false;} blocks.push_back(p);
    }
    assert(connection_index<responses.size()); response=responses[connection_index++]; online=true; return true;
  }
  int lastError(char* buf,size_t size){ std::snprintf(buf,size,"TLS allocation failed");return -32512; }
  int fd(){return 1;} void setSocketOption(int,int,void*,size_t){}
  template<class... T> void printf(const char*,T...){} void print(const char*){}
  int available(){ return int(response.size()-pos); }
  int read(){return pos<response.size()?response[pos++]:-1;}
  int read(uint8_t* out,size_t n){n=std::min(n,response.size()-pos);std::memcpy(out,response.data()+pos,n);pos+=n;return int(n);}
  bool connected(){return online;}
  void stop(){for(void* p:blocks)tls_free(p);blocks.clear();online=false;}
};
${allocator}
struct ParsedHttpsUrl { String host,path; uint16_t port=443; };
${['parseHttpsUrl','readHttpLine','parseStatusCode','parseSizeT','parseContentRangeTotal','resolveRedirectUrl'].map(fn).join('\n')}
using RangeDataFn=bool(*)(const uint8_t*,size_t,void*);
${fn('fetchHttpRange')}
bool collect(const uint8_t* p,size_t n,void* ctx){static_cast<std::string*>(ctx)->append(reinterpret_cast<const char*>(p),n);return true;}
void reset(size_t free,size_t largest,size_t external=4*1024*1024) {
  assert(allocations.empty()); internal_free=free;largest_internal=largest;external_free=external;
  external_allocations=0;connection_index=0;force_connect_failure=false;diagnostics="";
  responses={"HTTP/1.1 302 Found\r\nLocation: https://release-assets.githubusercontent.com/image\r\n\r\n",
    "HTTP/1.1 206 Partial Content\r\nContent-Length: 4\r\nContent-Range: bytes 0-3/400\r\n\r\nDATA"};
}
bool transfer(String& error){uint8_t buf[4];size_t total=0;std::string body;String final_url;
  bool ok=fetchHttpRange("https://github.com/release",0,3,buf,sizeof(buf),collect,&body,total,error,&final_url);
  if(ok){assert(body=="DATA"&&total==400&&final_url=="https://release-assets.githubusercontent.com/image");}
  assert(allocations.empty()); assert(tls_calloc==coreCalloc||tls_calloc==checkTlsInternalCalloc);return ok;
}
int main(){String error;
  reset(128*1024,64*1024); assert(transfer(error));assert(external_allocations==0);
  reset(48*1024,30*1024);
#if defined(DEVICE_ESP32_S3_RGB_480)
  assert(transfer(error));assert(external_allocations>0);assert(internal_free==48*1024);
  reset(64*1024,8*1024);assert(transfer(error));assert(external_allocations==6);
#else
  assert(!transfer(error));assert(external_allocations==0);
#endif
  reset(8*1024,8*1024,0);assert(!transfer(error));assert(error=="connect failed: github.com");
#if defined(DEVICE_ESP32_S3_RGB_480)
  assert(diagnostics.find("-32512")!=String::npos);
#endif
  reset(128*1024,64*1024);responses={"HTTP/1.1 404 Not Found\r\n\r\n"};assert(!transfer(error));assert(error=="HTTP 404");
  for(int i=0;i<20;++i){reset(128*1024,64*1024);assert(transfer(error));}
  std::cout<<"OTA range allocation, redirects, pressure, exhaustion, cleanup and HTTP errors passed\n";
}
`;
const out = path.join(root, 'build/tests/guition-s3-ota-tls-memory');
fs.mkdirSync(out,{recursive:true});
const cppPath = path.join(out,'test.cpp');fs.writeFileSync(cppPath,cpp);
const deviceSelect = fs.readFileSync(path.join(root, 'src/devices/device_select.h'), 'utf8');
const familyGuard = deviceSelect.match(/#if[^#]+#define DEVICE_ESP32_S3_RGB_480/)[0];
const s3Targets = ['DEVICE_GUITION_ESP32_4848S040', 'DEVICE_WAVESHARE_S3_TOUCH_LCD_4', 'DEVICE_WAVESHARE_S3_TOUCH_LCD_4B'];
for (const define of s3Targets) assert.ok(familyGuard.includes(`defined(${define})`));
for (const target of [...s3Targets,'p4']) {
  const binary = path.join(out,target+(process.platform==='win32'?'.exe':''));
  const flags = target==='p4'?[]:[`-D${target}`];
  fs.writeFileSync(cppPath, familyGuard+'\n#endif\n'+cpp);
  let result=spawnSync(compiler,['-std=c++17',...flags,cppPath,'-o',binary],{encoding:'utf8'});
  assert.equal(result.status,0,result.stdout+result.stderr);
  result=spawnSync(binary,[],{encoding:'utf8'});
  assert.equal(result.status,0,target+': '+result.stdout+result.stderr);
  console.log(target+': '+result.stdout.trim());
}
