#pragma once

#include <Arduino.h>

// The Web Admin entity picker searches through the Bridge, and the panel
// declares every entity its tiles use, like an ESPHome device names the Home
// Assistant states it needs (docs-dev/command-encryption.md, "Entity
// search"). Only for a panel paired with a Bridge that announces
// "entity_search": both travel sealed on the command channel
// ({base}/cmnd/entities and {base}/cmnd/tiles), never plain. With a Web Admin
// password on the panel the Bridge searches every Home Assistant entity and
// serves the declared ones; otherwise only the released entities. Loop task
// only, like the command channel.
namespace entity_search {

// Paired with a Bridge that searches.
bool available();

// Sends a search for one picker list (/api/entity_options key) from match
// `offset` on (the picker's next page) and returns its id; 0 when not
// available or for an unknown list.
uint32_t start(const String& query, const String& list, uint32_t offset = 0);

// The answer of `id` as {"full":bool,"more":bool,"r":[...]} once every part
// arrived; false while pending, for an older id or after a newer search.
bool result(uint32_t id, String& json);

// A sealed "entities" data message from the Bridge.
void handleAnswer(const uint8_t* body, size_t length);

// The tiles' entities or the Web Admin password may have changed: declare
// again shortly (sent only when the declaration differs from the one the
// Bridge acknowledged).
void scheduleTilesReport();

// A sealed "tiles" data message: the Bridge acknowledges a declaration.
void handleDeclarationAck(const uint8_t* body, size_t length);

// Loop: sends a due declaration once the encrypted session exists, again on
// every new session, and again while the Bridge has not acknowledged it.
void service();

}  // namespace entity_search
