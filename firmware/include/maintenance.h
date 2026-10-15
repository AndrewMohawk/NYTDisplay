#pragma once

#include "provisioning.h"

namespace maintenance {
enum class Result { Exit, CheckNow };

// Hold all three face buttons (next, previous, and refresh) together for two
// seconds while the device wakes. No recessed RESET or BOOT control is needed.
bool requested();

// Joins the saved Wi-Fi and serves a physically gated, time-limited local
// maintenance page. Firmware upload requires an OTA partition layout.
Result run(const provisioning::Credentials& credentials, const char* version,
           int batteryPercent);
}  // namespace maintenance
