#pragma once

#include <Arduino.h>

// The Web Admin entity picker searches through the Bridge, and the panel
// reports the entities its tiles use beyond the Bridge's released lists
// (docs-dev/command-encryption.md, "Entity search"). Only for a panel paired
// with a Bridge that announces "entity_search": both travel sealed on the
// command channel ({base}/cmnd/entities and {base}/cmnd/tiles), never plain.
// With a Web Admin password on the panel the Bridge searches every Home
// Assistant entity and serves the reported ones; otherwise only the released
// entities. Loop task only, like the command channel.
namespace entity_search {

// Paired with a Bridge that searches.
bool available();

// Sends a search for one picker list (/api/entity_options key) and returns
// its id; 0 when not available or for an unknown list.
uint32_t start(const String& query, const String& list);

// The answer of `id` as {"full":bool,"more":bool,"r":[...]} once every part
// arrived; false while pending, for an older id or after a newer search.
bool result(uint32_t id, String& json);

// A sealed "entities" data message from the Bridge.
void handleAnswer(const uint8_t* body, size_t length);

// The tiles' entities or the Web Admin password may have changed: report
// again shortly.
void scheduleTilesReport();

// Loop: sends a due report once the encrypted session exists, and again
// after every new session.
void service();

}  // namespace entity_search
