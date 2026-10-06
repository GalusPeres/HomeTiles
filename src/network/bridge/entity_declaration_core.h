#pragma once

// Pure core of the panel's entity declaration (entity_search.cpp; contract in
// docs-dev/command-encryption.md, "Entity search"): which entities the tiles
// use, the declaration's version and its sealed-size parts. No Arduino types,
// so tools/tests/network/test-entity-declaration-core.mjs runs it unchanged.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace entity_declaration {

constexpr size_t kMaxPartBytes = 1800;  // below command_channel's sealed body limit
constexpr size_t kMaxParts = 8;         // entity_search.py MAX_DECLARATION_PARTS
constexpr size_t kMaxEntities = 300;    // entity_search.py MAX_PANEL_ENTITIES
constexpr size_t kMaxEntityLength = 255;
// {"v":4294967295,"p":7,"n":8,"lists":{ ... ]},"own":true,"web_auth":false}
constexpr size_t kPartEnvelope = 80;

// A Home Assistant entity id: letters, digits and '_' around one '.'.
// Anything else names no entity, and none of these characters needs JSON
// escaping.
inline bool plainEntityId(const char* id) {
  if (!id) return false;
  size_t length = 0;
  size_t dots = 0;
  size_t dot_at = 0;
  for (const char* c = id; *c; ++c, ++length) {
    if (length >= kMaxEntityLength) return false;
    const char ch = *c;
    if (ch == '.') {
      ++dots;
      dot_at = length;
    } else if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                 ch == '_')) {
      return false;
    }
  }
  return dots == 1 && dot_at > 0 && dot_at + 1 < length;
}

inline uint32_t fnv1a(const char* text, uint32_t hash = 2166136261u) {
  for (const char* c = text; *c; ++c) {
    hash ^= static_cast<uint8_t>(*c);
    hash *= 16777619u;
  }
  return hash;
}

class Declaration {
 public:
  // Adds an entity of a picker list once, in lower case. Anything that is no
  // entity id, and entities beyond kMaxEntities, are left out and counted.
  void add(const char* list, const char* entity) {
    if (!list || !entity || !entity[0]) return;
    if (!plainEntityId(entity)) {
      ++dropped_;
      return;
    }
    std::string id(entity);
    for (char& ch : id) {
      if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    List* slot = nullptr;
    for (List& item : lists_) {
      if (std::strcmp(item.name, list) == 0) slot = &item;
    }
    if (slot) {
      for (const std::string& known : slot->ids) {
        if (known == id) return;
      }
    }
    if (count_ >= kMaxEntities) {
      ++dropped_;
      return;
    }
    if (!slot) {
      lists_.push_back(List{list, {}});
      slot = &lists_.back();
    }
    slot->ids.push_back(id);
    ++count_;
  }

  size_t count() const { return count_; }
  size_t dropped() const { return dropped_; }

  // The same entities and password claim give the same version in any
  // order: a sum of one hash per entity, so moving tiles changes nothing.
  uint32_t version(bool web_auth) const {
    uint32_t sum = web_auth ? 0x9e3779b9u : 0u;
    for (const List& item : lists_) {
      const uint32_t list_hash = fnv1a("|", fnv1a(item.name));
      for (const std::string& id : item.ids) sum += fnv1a(id.c_str(), list_hash);
    }
    return sum;
  }

  // {"v":version,"p":part,"n":parts,"lists":{list:[entity ids]},"own":true,
  // "web_auth":bool} in parts of at most kMaxPartBytes; what does not fit
  // kMaxParts parts is left out and counted in `dropped`. "own" tells the
  // Bridge that the declaration names everything the panel uses, so it gets
  // its own entry's releases and these entities instead of every panel's
  // releases.
  std::vector<std::string> parts(uint32_t version, bool web_auth, size_t* dropped) const {
    std::vector<std::string> lists(1);  // the "lists" object content of each part
    const char* open = nullptr;         // the list whose array is open in the last part
    for (const List& item : lists_) {
      for (const std::string& id : item.ids) {
        const size_t cost = id.size() + 3 + (open == item.name ? 0 : std::strlen(item.name) + 6);
        if (lists.back().size() + cost + kPartEnvelope > kMaxPartBytes) {
          if (lists.size() == kMaxParts) {
            if (dropped) ++*dropped;
            continue;
          }
          if (open) lists.back() += ']';
          lists.emplace_back();
          open = nullptr;
        }
        std::string& body = lists.back();
        if (open != item.name) {
          if (open) body += ']';
          if (!body.empty()) body += ',';
          body += '"';
          body += item.name;
          body += "\":[";
          open = item.name;
        } else {
          body += ',';
        }
        body += '"';
        body += id;
        body += '"';
      }
    }
    if (open) lists.back() += ']';
    std::vector<std::string> parts;
    for (size_t i = 0; i < lists.size(); ++i) {
      std::string part = "{\"v\":" + std::to_string(version) + ",\"p\":" + std::to_string(i) +
                         ",\"n\":" + std::to_string(lists.size()) + ",\"lists\":{" + lists[i] +
                         "},\"own\":true,\"web_auth\":" + (web_auth ? "true" : "false") + "}";
      parts.push_back(part);
    }
    return parts;
  }

 private:
  struct List {
    const char* name;  // a picker list name with static storage
    std::vector<std::string> ids;
  };
  std::vector<List> lists_;
  size_t count_ = 0;
  size_t dropped_ = 0;
};

}  // namespace entity_declaration
