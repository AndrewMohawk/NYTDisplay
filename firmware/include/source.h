#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace source {
struct Image {
  uint8_t *data = nullptr;  // PSRAM allocation. Caller releases with free().
  size_t length = 0;
  String edition;
};

// Download only the requested YYYY-MM-DD edition. Never substitutes yesterday.
// Caller must release/reset a previous Image before reusing it. Output stays empty
// on failure. Requires working Wi-Fi, PSRAM and an NTP-synchronized system clock.
bool fetchDailyJpeg(const char *requestedISODate, Image &out);
const char *lastError();
}  // namespace source
