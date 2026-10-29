#include "src/types/power_flow/web_handler.h"

#include "src/types/power_flow/flow_model.h"

namespace {

// The node pickers, each one line of Tile::flow_entities (power_flow::Role).
const char* const kNodeFields[power_flow::kRoleCount] = {
    "flow_pv_entity", "flow_grid_entity", "flow_battery_entity", "flow_battery_charge_entity", "flow_home_entity"};

struct OptionField {
  const char* name;
  uint8_t bit;
};
const OptionField kOptionFields[] = {{"flow_header", kFlowHeaderBit},
                                     {"flow_no_card", kFlowNoCardBit},
                                     {"flow_grid_invert", kFlowGridInvertBit},
                                     {"flow_battery_invert", kFlowBatteryInvertBit}};

bool arg_on(WebServer& server, const char* name) {
  const String value = server.arg(name);
  return value == "1" || value.equalsIgnoreCase("true") || value.equalsIgnoreCase("on");
}

}  // namespace

// A partial request keeps what it does not name: the whole record
// (flow_entities, copy/paste and import), single node pickers, options.
void apply_power_flow_fields_from_request(WebServer& server, Tile& tile) {
  // A tile turned into an Power flow drops what the former type kept.
  if (tile.sensor_entity.length()) tile.sensor_entity = "";
  tile.sensor_unit = "";
  tile.sensor_display_mode = 0;
  tile.key_modifier = 0;
  tile.key_code &= kFlowOptionBits;

  if (server.hasArg("flow_entities")) tile.flow_entities = server.arg("flow_entities");
  bool picked = false;
  for (const char* field : kNodeFields) picked = picked || server.hasArg(field);
  if (picked) {
    const String before = tile.flow_entities;
    String record;
    for (size_t role = 0; role < kFlowEntityLinesMax; ++role) {
      if (role) record += '\n';
      record += role < power_flow::kRoleCount && server.hasArg(kNodeFields[role])
                    ? server.arg(kNodeFields[role])
                    : flowEntityAt(before, role);
    }
    tile.flow_entities = record;
  }
  normalizeFlowEntities(tile);

  for (const OptionField& option : kOptionFields) {
    if (!server.hasArg(option.name)) continue;
    if (arg_on(server, option.name)) {
      tile.key_code |= option.bit;
    } else {
      tile.key_code &= static_cast<uint8_t>(~option.bit);
    }
  }
}
