import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const root = resolve(fileURLToPath(new URL('../../..', import.meta.url)));
const read = path => readFileSync(resolve(root, path), 'utf8');
function definition(source, signature) {
  const start = source.indexOf(signature);
  assert(start >= 0, signature);
  let end = source.indexOf('{', start) + 1, depth = 1;
  while (depth) {
    if (source[end] === '{') depth++;
    if (source[end] === '}') depth--;
    end++;
    assert(end < source.length, signature);
  }
  return source.slice(start, end);
}
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
const renderer = read('src/types/switch/renderer.cpp');
const switchHtml = read('src/types/switch/web_html.cpp');
const switchChoiceStart = switchHtml.indexOf('struct SwitchChoice {');
const switchChoiceStruct = switchHtml.slice(switchChoiceStart, switchHtml.indexOf('};', switchChoiceStart) + 2);
const strings = read('src/core/i18n/i18n.cpp');
const tables = [...strings.matchAll(/static const Strings kStrings(?:De|En|Fr) = \{[\s\S]*?\};/g)].map(match => match[0]);
assert.equal(tables.length, 3);
const harness = String.raw`
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include "src/network/bridge/control_contract.h"
struct String:std::string {
  using std::string::string;
  String()=default;
  String(const std::string& value):std::string(value){}
  size_t length() const { return size(); }
};
struct SerialType { template<typename...T> void printf(const char*,T...){} } Serial;
enum class TopicKey { SCENE_CMND, SWITCH_CMND, LIGHT_CMND };
struct Topics { const char* topic(TopicKey key) {return key==TopicKey::SCENE_CMND?"scene":key==TopicKey::LIGHT_CMND?"light":"switch";} } mqttTopics;
struct Message { std::string topic,payload; bool retained; };
struct Network {
  bool connected=true; std::vector<Message> messages;
  bool isMqttConnected(){return connected;}
  bool mqttEnqueuePublish(const char* topic,const char* payload,bool retain) {messages.push_back({topic,payload,retain});return true;}
} networkManager;
struct Bridge { std::map<std::string,String> actions; String findSceneEntity(const char* alias) {return actions[alias];} } haBridgeConfig;
int local_commands=0;
bool handle_local_switch_command(const char* entity,const char*) {if(std::strcmp(entity,"switch.local")==0){++local_commands;return true;}return false;}
${definition(mqtt, 'void mqttPublishScene(')}
${definition(mqtt, 'void mqttPublishSwitchCommand(')}
enum GridType {ROOT};
struct SwitchState {bool available=true,has_state=false,is_on=false;};
SwitchState state;
struct SwitchBarView {};SwitchBarView bar_view;
struct SwitchEventData {String entity_id;GridType grid_type=ROOT;uint8_t index=0;SwitchBarView* view=nullptr;};
SwitchState get_switch_state(GridType,uint8_t){return state;}
void update_switch_tile_state(GridType,uint8_t,const char* value){state.is_on=std::strcmp(value,"on")==0;}
void hold_toggle(SwitchEventData*,bool){}
${definition(renderer, 'void toggle_switch_tile(')}
namespace i18n {
${definition(read('src/core/i18n/i18n.h'), 'struct Strings')} ;
${tables.join('\n')}
const Strings& strings(const char* language) {return std::strcmp(language,"de")==0?kStringsDe:std::strcmp(language,"fr")==0?kStringsFr:kStringsEn;}
}
struct Config { const char* language="en"; } config;
struct ConfigManager { const Config& getConfig()const{return config;} } configManager;
struct SceneOption { String alias,entity; };
String humanizeIdentifier(const String& value,bool){return value;}
void appendHtmlEscaped(String& output,const String& value) {output+=value;}
${switchChoiceStruct}
${definition(switchHtml, 'void append_switch_choice(')}
${definition(switchHtml, 'void append_switch_fields_html(')}
${definition(read('src/types/scene/web_html.cpp'), 'void append_scene_fields_html(')}
int main(){
  for(const char* domain:{"light","switch","input_boolean","automation","fan","humidifier","remote","siren"}){
    String entity=std::string(domain)+".desk";
    assert(ha_control::supportsSwitchTile(entity.c_str()));
    SwitchEventData data;data.entity_id=entity;
    for(bool widget:{false,true}){
      // Header layouts (with a bar view) switch optimistically; the icon
      // button sends toggle. Both send exactly one command.
      data.view=widget?&bar_view:nullptr;state={true,true,false};
      const size_t before=networkManager.messages.size();
      toggle_switch_tile(&data);
      assert(networkManager.messages.size()==before+1);
      const auto& msg=networkManager.messages.back();
      assert(msg.topic==(std::strcmp(domain,"light")==0?"light":"switch"));
      assert(!msg.retained&&msg.payload.find(entity)!=std::string::npos);
      state.available=false;toggle_switch_tile(&data);
      assert(networkManager.messages.size()==before+1);
    }
  }
  for(const char* domain:{"scene","script","button","input_button"}){
    String entity=std::string(domain)+".desk";
    assert(ha_control::supportsSceneTile(entity.c_str()));
    haBridgeConfig.actions[domain]=entity;
    mqttPublishScene(domain);
    assert(networkManager.messages.back().topic=="scene");
    assert(networkManager.messages.back().payload==domain&&!networkManager.messages.back().retained);
  }
  size_t before=networkManager.messages.size();
  for(const char* entity:{"binary_sensor.test","lock.door","vacuum.robot","alarm_control_panel.home","event.button","switch",""}){
    assert(!ha_control::supportsSwitchTile(entity));
    assert(!ha_control::supportsSceneTile(entity));
    mqttPublishSwitchCommand(entity,"on");
  }
  haBridgeConfig.actions["invalid"]="lock.door";mqttPublishScene("invalid");
  assert(networkManager.messages.size()==before);
  networkManager.connected=false;
  mqttPublishSwitchCommand("automation.desk","on");mqttPublishScene("button");
  assert(networkManager.messages.size()==before);
  mqttPublishSwitchCommand("switch.local","on");assert(local_commands==1);
  for(const char* language:{"de","en","fr"}){
    config.language=language;String html;
    append_switch_fields_html(html,"folder3",{"automation.desk","input_boolean.guest","fan.office"});
    append_scene_fields_html(html,"folder3",{{"desk","button.desk"},{"desk2","input_button.desk"}});
    const auto& tr=i18n::strings(language);
    assert(html.find(tr.switch_light)!=std::string::npos&&html.find(tr.scene_label)!=std::string::npos);
    assert(html.find("folder3_switch_entity")!=std::string::npos&&html.find("folder3_scene_alias")!=std::string::npos);
    assert(html.find("automation.desk")!=std::string::npos&&html.find("input_button.desk")!=std::string::npos);
    // Layout, value size and popup as one line of choices like Tile color.
    for(const char* field:{"folder3_switch_style_choices","folder3_switch_value_font_choices","folder3_switch_popup_open_mode_choices"})
      assert(html.find(field)!=std::string::npos);
    for(const char* label:{tr.switch_layout_automatic,tr.switch_layout_dimmer,tr.switch_layout_switch,tr.switch_icon_button,tr.short_press,tr.long_press})
      assert(html.find(std::string(">")+label+"</button>")!=std::string::npos);
  }
}
`;
const folder = resolve(root, 'build/host-compatible-controls');
mkdirSync(folder, {recursive: true});
const source = resolve(folder, 'test.cpp'), binary = resolve(folder, process.platform === 'win32' ? 'test.exe' : 'test');
writeFileSync(source, harness);
const compiler = [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean).find(command => spawnSync(command, ['--version']).status === 0);
assert(compiler, 'Native C++ compiler is required');
let result = spawnSync(compiler, ['-std=c++17', '-I', root, source, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
for (const file of ['src/types/switch/renderer.cpp', 'src/tiles/runtime/tile_renderer.cpp']) {
  assert(read(file).includes('init.available = state.available;'));
}
const popup = read('src/ui/popups/light/light_popup.cpp');
assert(popup.includes('ctx->available = init.available;'));
assert(popup.includes('const bool next_available = init.available;'));
assert(popup.includes('mqttPublishSwitchCommand(ctx->entity_id.c_str(), ctx->is_on ? "on" : "off")'));
console.log('Compatible control routing, availability, offline and translated editor tests passed.');
