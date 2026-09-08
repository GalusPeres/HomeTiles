#pragma once

#include "src/types/media/widgets.h"

// Update only when metadata or tile geometry changes, never on each frame.
void set_media_cover_text_layout(MediaTileWidgets& widgets, bool cover_visible);
