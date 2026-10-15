#pragma once
#include <Arduino.h>

namespace provisioning {
struct Credentials {
  String ssid;
  String password;
};

enum class Result { Connected, TimedOut };

// The welcome screen must call these before run(), then draw a Wi-Fi QR using
// the returned actual values. The random WPA2 password is stable across boots.
String setupSsid();
String setupPassword();
constexpr const char* PORTAL_URL = "http://192.168.4.1";

bool load(Credentials& credentials);
// GPIO4 and GPIO5 must both be held; releasing either before 2 seconds cancels.
bool setupRequested();

// Starts a WPA2 AP and phone portal. A candidate network is tested in AP+STA
// mode before its credentials atomically replace the saved JSON NVS record.
// Connected leaves STA connected. TimedOut leaves Wi-Fi off. Maximum 15 min.
Result run();
} // namespace provisioning
