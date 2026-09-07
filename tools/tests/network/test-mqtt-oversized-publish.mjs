import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/network/vendor/pubsubclient/PubSubClient.cpp'), 'utf8');
const functions = cppFunctionDefinitions(source);
const extract = name => { const fn = functions.find(f => f.name === name); assert(fn, name); return fn.source; };
const out = path.join(root, 'build/tests/mqtt-oversized-publish');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include "src/network/mqtt/mqtt_packet_safety.h"
using boolean = bool;
constexpr int MQTT_CONNECTION_TIMEOUT=-4, MQTT_MALFORMED_PACKET=-5, MQTT_PACKET_TOO_LARGE=-6;
constexpr uint8_t MQTTPUBLISH=0x30, MQTTQOS0=0, MQTTQOS1=2, MQTTPUBACK=0x40, MQTTPINGREQ=0xc0, MQTTPINGRESP=0xd0;
uint32_t ticks=100;
uint32_t millis(){return ticks;}
void vTaskDelay(int){++ticks;}
struct Logger {template<class... T> void printf(const char*,T...) {}} Serial;
struct Socket {
 std::vector<uint8_t> input, output; size_t cursor=0; bool closed=false;
 void stop(){closed=true;} bool available(){return cursor<input.size();}
 size_t write(const uint8_t* p,size_t n){output.insert(output.end(),p,p+n);return n;}
};
struct Stream {void write(uint8_t){}};
struct PubSubClient {
 Socket socket;Socket* _client=&socket; Stream* stream=nullptr;
 std::vector<uint8_t> storage; uint8_t* buffer=nullptr;
 uint16_t bufferSize=0,receiveBufferSize=0,keepAlive=15,socketTimeout=15;
 uint32_t droppedPublishCount=0,lastInActivity=100,lastOutActivity=100;
 bool pingOutstanding=false,allocationFails=false;int _state=0;uint32_t byteDelay=0;
 std::function<void(char*,uint8_t*,unsigned int)> callback;
 PubSubClient(uint16_t capacity){setBufferSize(capacity);lastInActivity=lastOutActivity=ticks;}
 bool connected(){return !socket.closed;}
 bool setBufferSize(uint16_t n){
   if(allocationFails)return false;
   std::vector<uint8_t> next(n+16,0xa5);
   if(buffer)std::copy(buffer,buffer+std::min(n,bufferSize),next.begin()+8);
   storage.swap(next);buffer=storage.data()+8;bufferSize=n;return true;
 }
 void checkGuards(){for(int i=0;i<8;++i){assert(storage[i]==0xa5);assert(storage[bufferSize+8+i]==0xa5);}}
 bool readByte(uint8_t* p){ticks+=byteDelay;if(!socket.available())return false;*p=socket.input[socket.cursor++];return true;}
 bool readByte(uint8_t* p,uint16_t* index){if(!readByte(p+*index))return false;++*index;return true;}
 uint32_t readPacket(uint8_t*);
 bool loop();
};
` + extract('PubSubClient::readPacket') + '\n' + extract('PubSubClient::loop') + String.raw`
std::vector<uint8_t> publish(size_t payload,bool qos1=false,bool retained=true){
 std::string topic="hometiles/test/bridge/apply";
 size_t remaining=2+topic.size()+(qos1?2:0)+payload;
 std::vector<uint8_t> p{static_cast<uint8_t>(0x30|(qos1?2:0)|(retained?1:0))};
 do{uint8_t digit=remaining%128;remaining/=128;p.push_back(digit|(remaining?128:0));}while(remaining);
 p.push_back(topic.size()>>8);p.push_back(topic.size()&255);p.insert(p.end(),topic.begin(),topic.end());
 if(qos1){p.push_back(0x12);p.push_back(0x34);}
 for(size_t i=0;i<payload;++i)p.push_back(static_cast<uint8_t>(i%251));
 return p;
}
void expectDelivery(PubSubClient& c,size_t bytes){
 int calls=0;c.callback=[&](char* topic,uint8_t* p,unsigned int n){
  ++calls;assert(std::string(topic)=="hometiles/test/bridge/apply");assert(n==bytes);
  assert(c.bufferSize>=bytes+std::strlen(topic));
  for(size_t i=0;i<bytes;++i)assert(p[i]==i%251);
 };
 assert(c.loop());assert(c.connected());assert(c.socket.cursor==c.socket.input.size());
 assert(calls==1);c.checkGuards();c.callback=nullptr;
}
int main(){
 // Receive the entire config/state regardless of the transient window or QoS.
 for(uint16_t initial:{16384,24576,32768})for(bool qos1:{false,true}){
  PubSubClient c(initial);c.socket.input=publish(20000,qos1);expectDelivery(c,20000);
  if(qos1)assert((c.socket.output==std::vector<uint8_t>{0x40,2,0x12,0x34}));
  else assert(c.socket.output.empty());
 }
 for(size_t bytes:{32720U,32768U,48000U,65500U}){
  PubSubClient c(16384);c.socket.input=publish(bytes,true);expectDelivery(c,bytes);
  assert(c.receiveBufferSize>=c.socket.input.size());assert(c.bufferSize<=65535);
 }
 // Fresh connections and repeated retained delivery cannot form a close loop.
 for(int reconnect=0;reconnect<4;++reconnect){
  PubSubClient c(16384);c.socket.input=publish(48000);expectDelivery(c,48000);
  const auto capacity=c.bufferSize;
  c.socket.input=publish(48000);c.socket.cursor=0;expectDelivery(c,48000);
  assert(c.bufferSize==capacity);
 }
 // Oversize/allocation failure is bounded, consumes exactly one packet, sends
 // PUBACK for QoS 1, and leaves the following PUBLISH/PINGRESP synchronized.
 for(bool qos1:{false,true})for(bool allocationFails:{false,true}){
  PubSubClient c(16384);c.allocationFails=allocationFails;
  const auto first=publish(allocationFails?20000:70000,qos1);
  c.socket.input=first;auto next=publish(5);c.socket.input.insert(c.socket.input.end(),next.begin(),next.end());
  int calls=0;c.callback=[&](char*,uint8_t*,unsigned int n){++calls;assert(n==5);};
  assert(c.loop());assert(c.connected());assert(calls==0);assert(c.socket.cursor==first.size());
  assert(c.droppedPublishCount==1);
  if(qos1)assert((c.socket.output==std::vector<uint8_t>{0x40,2,0x12,0x34}));
  assert(c.loop());assert(calls==1);c.checkGuards();
  c.socket.input={0xd0,0};c.socket.cursor=0;c.pingOutstanding=true;
  assert(c.loop());assert(!c.pingOutstanding);
 }
 // Historical malformed topic, invalid length encoding, QoS 2 and truncation
 // must still close without invoking the callback or touching buffer guards.
 std::vector<std::vector<uint8_t>> malformed{
  {0x30,0x32,0x36,0x2d}, {0x30,0x80,0x80,0x80,0x80,0x00},
  {0x34,5,0,1,'a',0,1}, {0x30,2,0,0}
 };
 for(const auto& packet:malformed){
  PubSubClient c(16384);c.socket.input=packet;
  c.callback=[](char*,uint8_t*,unsigned int){assert(false);};
  assert(!c.loop());assert(c._state==MQTT_MALFORMED_PACKET);c.checkGuards();
 }
 {PubSubClient c(16384);c.socket.input=publish(20000);c.socket.input.resize(9000);
  assert(!c.loop());assert(c._state==MQTT_CONNECTION_TIMEOUT);c.checkGuards();}
 {PubSubClient c(16384);c.socket.input=publish(20000);c.byteDelay=100;
  assert(!c.loop());assert(c._state==MQTT_CONNECTION_TIMEOUT);assert(c.socket.cursor<200);}
 {PubSubClient c(16384);c.socket.input=publish(300000);
  assert(!c.loop());assert(c._state==MQTT_PACKET_TOO_LARGE);assert(c.socket.cursor<10);c.checkGuards();}
 std::cout<<"Actual MQTT parser: bounded receive growth, QoS, retained replay, discard, malformed input and timeouts passed\n";
}
`;
const sourcePath=path.join(out,'test.cpp'), binary=path.join(out,process.platform==='win32'?'test.exe':'test');
fs.writeFileSync(sourcePath,cpp);
const compiler=[process.env.CXX,'clang++','g++','c++'].filter(Boolean).find(c=>spawnSync(c,['--version']).status===0);
assert(compiler,'C++ compiler is required for MQTT regressions');
let result=spawnSync(compiler,['-std=c++17','-Wall','-Wextra','-Werror','-I',root,sourcePath,'-o',binary],{encoding:'utf8'});
assert.equal(result.status,0,result.stdout+result.stderr);
result=spawnSync(binary,[],{encoding:'utf8',timeout:30000});
assert.equal(result.status,0,result.stdout+result.stderr);
console.log(result.stdout.trim());

const manager=fs.readFileSync(path.join(root,'src/network/network_manager.cpp'),'utf8');
const callbacks=manager.match(/setCallback\(\[this\][\s\S]*?\}\);/g);
assert.equal(callbacks.length,2);
for(const callback of callbacks)assert.match(callback,/mqtt_buffer_size = mqtt_client.getBufferSize\(\);[\s\S]*mqttCallback\(topic, payload, length\)/,
 'The callback queue must validate against the actual grown buffer, including reconfiguration');
assert.match(manager,/mqtt_receive_buffer_floor > configured \? mqtt_receive_buffer_floor : configured/,
 'Buffer housekeeping must not shrink immediately after receive growth');
