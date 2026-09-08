#pragma once

// Main-loop only. Service one pending media state/result between normal idle
// tile batches; no new network requests or transport policy is introduced.
void process_idle_media_updates();
