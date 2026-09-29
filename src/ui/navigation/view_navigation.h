#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Main-loop only, like the existing folder and popup services.
void viewNavigationConnected();
void viewNavigationService();
bool viewNavigationHandleMessage(const char* topic, const char* payload, size_t length);
void viewNavigationSource(lv_obj_t* source);
void viewNavigationPopupShown(lv_obj_t* overlay, const char* entity);
void viewNavigationClosePopups();
// A popup reads its tile's colors and options once, when it opens. A grid
// reload (a Web Admin save) replaces the tiles, so the reload asks for the
// tile of the visible popup first and reopens that popup from the new tile
// afterwards, like a local tap; the popup then shows the tile's new colors.
uint16_t viewNavigationVisiblePopupTile();
void viewNavigationReopenPopup(uint16_t view_id);
