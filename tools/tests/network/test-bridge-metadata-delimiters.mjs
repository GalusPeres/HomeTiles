import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/network/bridge/ha_bridge_config.cpp'), 'utf8');
const functions = cppFunctionDefinitions(source);
const fn = name => functions.find(f => f.name === name)?.source ?? '';
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) {
  console.log('SKIP: Bridge metadata parsing needs a host C++ compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/bridge-metadata-delimiters');
fs.mkdirSync(out, {recursive: true});
const fixture = (state, name = 'View') => JSON.stringify({
  editable_meta: [
    {entity_id: 'number.volume', name: 'Volume', state: '100', icon: 'mdi:volume-high'},
    {entity_id: 'select.view', name, state, icon: 'mdi:view-dashboard'},
    {entity_id: 'time.end', name: 'End time', state: '17:00:00', icon: 'mdi:clock-end'},
  ],
  sensor_meta: [{entity_id: 'sensor.temperature', name: 'Temperature', icon: 'mdi:thermometer'}],
});
const fixtures = ['Home', 'Home / Lighting [f:1]', 'Home / Desk [t:13]', 'Mode } [a]', 'Mode \\" [t:2]']
  .map(state => fixture(state));
fixtures.push(fixture('Home', 'View [office]'));
const cpp = `
#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>
#include <string>
class String : public std::string {
public:
 using std::string::string; using std::string::operator=;
 String()=default; String(const std::string& value):std::string(value){}
 int indexOf(char c,int start=0)const{auto p=find(c,std::max(0,start));return p==npos?-1:static_cast<int>(p);}
 int indexOf(const String& s,int start=0)const{auto p=find(s,std::max(0,start));return p==npos?-1:static_cast<int>(p);}
 String substring(int a,int b)const{return substr(a,b-a);}
 char charAt(int i)const{return at(i);}
 void trim(){auto a=find_first_not_of(" ");if(a==npos){clear();return;}*this=substr(a,find_last_not_of(" ")-a+1);}
};
// The fixtures use ASCII names and icons; escape decoding is tested separately.
String decodeJsonEscapes(const String& value){return value;}
void upsertKeyValueMap(String& text,const String& key,const String& value){if(text.length())text+='\\n';text+=key+"="+value;}
${['findMatchingJsonObjectEnd','findMatchingJsonArrayEnd','extractStringField','parseEntityNameSection','parseEntityIconSection','parseIconMetaSections'].map(fn).join('\n')}
int main(){
 for(const char* payload:{${fixtures.map(f => 'R"fixture(' + f + ')fixture"').join(',')}}){
  String icons,names;parseIconMetaSections(payload,icons);parseEntityNameSection(payload,"editable_meta",names);
  for(const char* entry:{"number.volume=mdi:volume-high","select.view=mdi:view-dashboard","time.end=mdi:clock-end","sensor.temperature=mdi:thermometer"}){
   if(icons.find(entry)==String::npos){std::cerr<<"Missing icon: "<<entry<<" after metadata: "<<payload<<"\\n";return 1;}
  }
  assert(names.find("time.end=End time")!=String::npos);
 }
 String icons;parseIconMetaSections(R"({"sensor_meta":[]})",icons);assert(icons.empty());
 parseIconMetaSections(R"({"editable_meta":[{"entity_id":"select.view","state":"[t:13]","icon":"mdi:view-dashboard"})",icons);
 assert(icons.empty()&&"A truncated array must not replace complete metadata");
 std::cout<<"Bridge metadata: Home, target IDs, brackets, braces, escaped quotes, following entities and truncated arrays passed\\n";
}
`;
const cppPath = path.join(out, 'test.cpp');
fs.writeFileSync(cppPath, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(compiler, ['-std=c++17', cppPath, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log(result.stdout.trim());
