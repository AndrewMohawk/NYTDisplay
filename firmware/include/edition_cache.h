#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace edition_cache {
constexpr size_t MAX_EDITIONS = 5;

bool begin();
bool save(const String& edition, const uint8_t* frame, size_t length);
bool load(const String& edition, uint8_t* frame, size_t length);
bool contains(const String& edition);
bool latest(String& edition);
bool adjacent(const String& current, int direction, String& edition);
}  // namespace edition_cache
