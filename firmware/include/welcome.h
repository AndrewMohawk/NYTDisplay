#pragma once
#include <Arduino.h>
bool showWelcome(const String& ssid, const String& password);
bool showLoadingProgress(uint8_t completed, uint8_t total = 5);
