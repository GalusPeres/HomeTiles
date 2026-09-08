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
