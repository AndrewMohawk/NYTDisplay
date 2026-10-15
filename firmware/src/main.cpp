// SPDX-License-Identifier: GPL-3.0-only
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Preferences.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <time.h>
#include "display.h"
#include "edition_cache.h"
#include "jpeg_render.h"
#include "maintenance.h"
#include "provisioning.h"
#include "settings.h"
#include "source.h"
#include "welcome.h"

namespace {
constexpr int POWER_PIN = 12, SD_POWER_PIN = 16;
// E1004-specific front/rear button order. Other E10xx models use a different
// physical ordering for these same three GPIOs.
constexpr int REFRESH_BUTTON_PIN = 5, NEXT_BUTTON_PIN = 3, PREVIOUS_BUTTON_PIN = 4;
constexpr int BATTERY_ADC_PIN = 1, BATTERY_ENABLE_PIN = 21;
constexpr time_t MIN_VALID_TIME = 1735689600; // 2025-01-01
constexpr char RENDER_REVISION[] = "hybrid-photo-1-restored";
constexpr char WELCOME_REVISION[] = "nyt-display-setup-1";
constexpr char FIRMWARE_VERSION[] = "NYTDisplay-1.0.0";
Preferences state;
bool stateOpen = false;
bool cacheReady = false;
int batteryLevel = -1;
volatile bool synchronizedTime = false;

enum class WakeAction { Scheduled, Refresh, Previous, Next };

class PackedOverlay : public Adafruit_GFX {
 public:
  explicit PackedOverlay(uint8_t* pixels) : Adafruit_GFX(DISPLAY_WIDTH, DISPLAY_HEIGHT), data(pixels) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT || color > 5) return;
    size_t offset = size_t(y) * DISPLAY_WIDTH / 2 + x / 2;
    if (x & 1) data[offset] = (data[offset] & 0xF0) | color;
    else data[offset] = (data[offset] & 0x0F) | (color << 4);
  }
 private:
  uint8_t* data;
};

void timeNotification(struct timeval*) { synchronizedTime = true; }

void releasePowerHolds() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(static_cast<gpio_num_t>(POWER_PIN));
  gpio_hold_dis(static_cast<gpio_num_t>(SD_POWER_PIN));
  gpio_hold_dis(static_cast<gpio_num_t>(BATTERY_ENABLE_PIN));
  for (int pin : {REFRESH_BUTTON_PIN, NEXT_BUTTON_PIN, PREVIOUS_BUTTON_PIN})
    rtc_gpio_deinit(static_cast<gpio_num_t>(pin));
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, LOW);
  pinMode(SD_POWER_PIN, OUTPUT);
  digitalWrite(SD_POWER_PIN, LOW);
  pinMode(BATTERY_ENABLE_PIN, OUTPUT);
  digitalWrite(BATTERY_ENABLE_PIN, LOW);
}

[[noreturn]] void goToSleep(uint32_t seconds) {
  sleepPanel();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  digitalWrite(POWER_PIN, LOW);
  digitalWrite(SD_POWER_PIN, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(POWER_PIN));
  gpio_hold_en(static_cast<gpio_num_t>(SD_POWER_PIN));
  digitalWrite(BATTERY_ENABLE_PIN, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(BATTERY_ENABLE_PIN));
  gpio_deep_sleep_hold_en();
  for (int pin : {REFRESH_BUTTON_PIN, NEXT_BUTTON_PIN, PREVIOUS_BUTTON_PIN})
    pinMode(pin, INPUT_PULLUP);
  // Held buttons must not repeatedly wake the board immediately.
  uint32_t start = millis();
  while ((digitalRead(REFRESH_BUTTON_PIN) == LOW ||
          digitalRead(NEXT_BUTTON_PIN) == LOW ||
          digitalRead(PREVIOUS_BUTTON_PIN) == LOW) && millis() - start < 3000)
    delay(20);
  if (digitalRead(REFRESH_BUTTON_PIN) == HIGH &&
      digitalRead(NEXT_BUTTON_PIN) == HIGH &&
      digitalRead(PREVIOUS_BUTTON_PIN) == HIGH) {
    uint64_t wakeMask = 0;
    for (int pin : {REFRESH_BUTTON_PIN, NEXT_BUTTON_PIN, PREVIOUS_BUTTON_PIN}) {
      rtc_gpio_pullup_en(static_cast<gpio_num_t>(pin));
      rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(pin));
      wakeMask |= 1ULL << pin;
    }
    esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
  }
  if (seconds) {
    if (seconds < 60) seconds = 60;
    esp_sleep_enable_timer_wakeup(uint64_t(seconds) * 1000000ULL);
    Serial0.printf("[sleep] Next check in %lu seconds.\n", static_cast<unsigned long>(seconds));
  } else {
    Serial0.println("[sleep] Waiting for a button or RESET.");
  }
  if (stateOpen) state.end();
  Serial0.flush();
  delay(20);
  esp_deep_sleep_start();
  while (true) delay(1000);
}

WakeAction wakeAction() {
  for (int pin : {REFRESH_BUTTON_PIN, NEXT_BUTTON_PIN, PREVIOUS_BUTTON_PIN})
    pinMode(pin, INPUT_PULLUP);
  uint64_t status = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1
      ? esp_sleep_get_ext1_wakeup_status() : 0;
  if ((status & (1ULL << REFRESH_BUTTON_PIN)) || digitalRead(REFRESH_BUTTON_PIN) == LOW) {
    Serial0.println("[button] Refresh.");
    return WakeAction::Refresh;
  }
  if ((status & (1ULL << PREVIOUS_BUTTON_PIN)) || digitalRead(PREVIOUS_BUTTON_PIN) == LOW) {
    Serial0.println("[button] Previous edition.");
    return WakeAction::Previous;
  }
  if ((status & (1ULL << NEXT_BUTTON_PIN)) || digitalRead(NEXT_BUTTON_PIN) == LOW) {
    Serial0.println("[button] Next edition.");
    return WakeAction::Next;
  }
  return WakeAction::Scheduled;
}

int readBatteryLevel() {
  pinMode(BATTERY_ENABLE_PIN, OUTPUT);
  digitalWrite(BATTERY_ENABLE_PIN, HIGH);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  delay(20);
  uint32_t millivolts = 0;
  for (int i = 0; i < 16; ++i) millivolts += analogReadMilliVolts(BATTERY_ADC_PIN);
  digitalWrite(BATTERY_ENABLE_PIN, LOW);
  millivolts = millivolts * 2 / 16;  // Compensate the board's 1:2 divider.
  struct Point { uint16_t mv; uint8_t percent; };
  constexpr Point curve[] = {
    {3270,0},{3300,5},{3410,10},{3490,20},{3580,30},{3680,40},
    {3750,50},{3800,60},{3850,70},{3910,80},{3960,90},{4150,100}
  };
  if (millivolts < 2500 || millivolts > 4500) return -1;
  if (millivolts <= curve[0].mv) return 0;
  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
    if (millivolts <= curve[i].mv) {
      const uint32_t span = curve[i].mv - curve[i - 1].mv;
      const uint32_t part = millivolts - curve[i - 1].mv;
      const int percent = curve[i - 1].percent +
          int(part * (curve[i].percent - curve[i - 1].percent) / span);
      Serial0.printf("[battery] %lu mV, %d%%.\n",
                     static_cast<unsigned long>(millivolts), percent);
      return percent;
    }
  }
  Serial0.printf("[battery] %lu mV, 100%%.\n", static_cast<unsigned long>(millivolts));
  return 100;
}

void addBatteryIndicator(uint8_t* packed) {
  if (!packed) return;
  PackedOverlay canvas(packed);
  canvas.fillRect(1010, 8, 182, 42, 1);
  const uint16_t ink = batteryLevel >= 0 && batteryLevel <= 20 ? 4 : 0;
  canvas.drawRect(1016, 15, 43, 24, ink);
  canvas.fillRect(1059, 22, 5, 10, ink);
  if (batteryLevel >= 0)
    canvas.fillRect(1020, 19, 35 * batteryLevel / 100, 16, ink);
  canvas.setTextSize(3);
  canvas.setTextColor(ink, 1);
  canvas.setCursor(1073, 17);
  if (batteryLevel < 0) canvas.print("--%");
  else { canvas.print(batteryLevel); canvas.print('%'); }
}

uint32_t retryDelay() { return RETRY_SECONDS; }

bool connectWifi(const provisioning::Credentials& credentials) {
  if (WiFi.status() == WL_CONNECTED) return true; // provisioning already connected
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.begin(credentials.ssid.c_str(), credentials.password.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS)
    delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    Serial0.println("[wifi] Unavailable. Keeping the existing screen; hold right button at wake to change Wi-Fi.");
    return false;
  }
  return true;
}

bool syncClock() {
  synchronizedTime = false;
  sntp_set_time_sync_notification_cb(timeNotification);
  configTzTime(TIME_ZONE, NTP_SERVER_1, NTP_SERVER_2);
  uint32_t start = millis();
  while (!synchronizedTime && millis() - start < NTP_SYNC_TIMEOUT_MS) delay(100);
  if (!synchronizedTime || time(nullptr) < MIN_VALID_TIME) {
    Serial0.println("[time] NTP synchronization failed; existing image retained.");
    return false;
  }
  time_t now = time(nullptr);
  struct tm local{};
  localtime_r(&now, &local);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S %Z", &local);
  Serial0.printf("[time] NTP synchronized: %s\n", timestamp);
  return true;
}

bool drawNewspaper(source::Image& image, const String& edition, bool display = true) {
  auto* rgb = static_cast<uint16_t*>(heap_caps_malloc(
      size_t(DISPLAY_WIDTH) * DISPLAY_HEIGHT * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  bool decoded = rgb && raster::decodeJpegToRgb565(
      image.data, image.length, rgb, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  free(image.data);
  image.data = nullptr;
  if (!decoded) {
    free(rgb);
    Serial0.println("[image] Could not decode complete baseline JPEG; retaining screen.");
    return false;
  }
  auto* packed = static_cast<uint8_t*>(heap_caps_malloc(
      DISPLAY_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  bool ready = packed && raster::ditherRgb565ToPacked(rgb, packed, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  free(rgb);
  if (!ready) {
    free(packed);
    Serial0.println("[image] Not enough memory for six-color rendering; retaining screen.");
    return false;
  }
  const bool cached = cacheReady &&
                      edition_cache::save(edition, packed, DISPLAY_FRAME_BYTES);
  if (cacheReady && !cached)
    Serial0.println("[cache] Could not save this edition.");
  if (!display) {
    free(packed);
    return cached;
  }
  addBatteryIndicator(packed);
  // All download, JPEG decoding and dithering succeeded before touching panel.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  bool displayed = showPackedFrame(packed, DISPLAY_FRAME_BYTES);
  free(packed);
  return displayed;
}

String dateDaysAgo(time_t now, int days) {
  struct tm local{};
  localtime_r(&now, &local);
  local.tm_hour = 12;  // Noon avoids DST transitions around local midnight.
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_mday -= days;
  local.tm_isdst = -1;
  mktime(&local);
  char date[11];
  strftime(date, sizeof(date), "%Y-%m-%d", &local);
  return String(date);
}

bool backfillPreviousEditions(time_t now, uint8_t* completed = nullptr) {
  if (!cacheReady) return false;
  bool complete = true;
  for (int days = 1; days <= 4; ++days) {
    const String edition = dateDaysAgo(now, days);
    bool saved = edition_cache::contains(edition);
    if (!saved) {
      source::Image image;
      if (!source::fetchDailyJpeg(edition.c_str(), image)) {
        Serial0.printf("[cache] Backfill %s failed: %s\n",
                       edition.c_str(), source::lastError());
        if (image.data) free(image.data);
        complete = false;
        continue;
      }
      saved = drawNewspaper(image, edition, false);
      if (!saved) {
        Serial0.printf("[cache] Could not render backfill edition %s.\n", edition.c_str());
        complete = false;
        continue;
      }
    }
    if (completed) {
      ++*completed;
      Serial0.printf("[loading] %u of 5 editions saved.\n", *completed);
      if (!showLoadingProgress(*completed))
        Serial0.println("[loading] Could not advance the progress screen; continuing.");
    }
  }
  return complete;
}

bool showCachedEdition(const String& edition) {
  if (!cacheReady) return false;
  auto* packed = static_cast<uint8_t*>(heap_caps_malloc(
      DISPLAY_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!packed) return false;
  const bool loaded = edition_cache::load(edition, packed, DISPLAY_FRAME_BYTES);
  if (loaded) addBatteryIndicator(packed);
  const bool displayed = loaded && showPackedFrame(packed, DISPLAY_FRAME_BYTES);
  free(packed);
  if (displayed && stateOpen) {
    state.putString("shown", edition);
    state.remove("welcome_rev");
  }
  if (displayed) Serial0.printf("[cache] Displayed %s.\n", edition.c_str());
  return displayed;
}
}

void setup() {
  releasePowerHolds();
  Serial0.begin(115200, SERIAL_8N1, 44, 43);
  delay(200);
  Serial0.println("\nNYT front-page display / reTerminal E1004");
  setenv("TZ", TIME_ZONE, 1);
  tzset();
  if (!psramFound() || ESP.getPsramSize() < 8 * 1024 * 1024) {
    Serial0.println("[memory] This firmware requires the E1004's 8 MB OPI PSRAM.");
    goToSleep(RETRY_SECONDS);
  }
  const bool maintenanceRequested = maintenance::requested();
  WakeAction action = wakeAction();
  batteryLevel = readBatteryLevel();
  cacheReady = edition_cache::begin();
  if (!cacheReady) Serial0.println("[cache] Edition history disabled.");
  stateOpen = state.begin("nyt-paper", false);
  bool requestSetup = provisioning::setupRequested();
  provisioning::Credentials credentials;
  bool configured = provisioning::load(credentials);
  bool setupPending = stateOpen && state.getBool("setup_pending", false);
  bool initialSync = false;
  if (maintenanceRequested && configured) {
    if (stateOpen) state.remove("display_rev");
    const maintenance::Result result = maintenance::run(
        credentials, FIRMWARE_VERSION, batteryLevel);
    String cached = stateOpen ? state.getString("shown", "") : "";
    if (!edition_cache::contains(cached)) edition_cache::latest(cached);
    if (edition_cache::contains(cached)) showCachedEdition(cached);
    if (result == maintenance::Result::CheckNow) action = WakeAction::Refresh;
    else goToSleep(CHECK_INTERVAL_SECONDS);
  }
  // After a firmware/render update, immediately restore the newest cached page
  // before doing any network work. This also recovers a configured unit that
  // was accidentally left in the one-shot setup state during development.
  const String storedEditionState = stateOpen ? state.getString("edition", "") : "";
  if (configured && cacheReady && stateOpen &&
      storedEditionState.endsWith(String(":") + RENDER_REVISION) &&
      state.getString("display_rev", "") != RENDER_REVISION) {
    String cached = state.getString("shown", "");
    if (!edition_cache::contains(cached)) edition_cache::latest(cached);
    if (edition_cache::contains(cached) && showCachedEdition(cached)) {
      state.putString("display_rev", RENDER_REVISION);
      state.putBool("setup_pending", false);
      setupPending = false;
      Serial0.printf("[cache] Restored %s immediately after firmware update.\n",
                     cached.c_str());
    }
  }
  // A fresh device opens setup immediately, whether powered by USB or battery.
  if (!configured || requestSetup || setupPending) {
    if (stateOpen) state.putBool("setup_pending", true);
    bool welcomeOnScreen = stateOpen &&
        state.getString("welcome_rev", "") == WELCOME_REVISION;
    if (!welcomeOnScreen || requestSetup) {
      if (!showWelcome(provisioning::setupSsid(), provisioning::setupPassword())) {
        Serial0.println("[welcome] Could not show setup screen; serial output has recovery details.");
      } else if (stateOpen) {
        state.putString("welcome_rev", WELCOME_REVISION);
      }
    }
    if (provisioning::run() != provisioning::Result::Connected) goToSleep(0);
    if (!provisioning::load(credentials)) goToSleep(0);
    if (stateOpen) {
      state.putBool("setup_pending", false);
      state.putBool("backfill", true);
      state.putUChar("backfill_try", 0);
    }
    initialSync = true;
    if (!showLoadingProgress(0))
      Serial0.println("[loading] Could not display the loading screen; continuing.");
    else
      Serial0.println("[loading] Edition progress displayed; 0 of 5 editions saved.");
    action = WakeAction::Refresh;  // Immediately reveal the current edition.
  }
  if (action == WakeAction::Previous || action == WakeAction::Next) {
    String current = stateOpen ? state.getString("shown", "") : "";
    String target;
    const int direction = action == WakeAction::Previous ? -1 : 1;
    if (edition_cache::adjacent(current, direction, target)) {
      if (!showCachedEdition(target))
        Serial0.printf("[cache] Could not display cached edition %s.\n", target.c_str());
    } else {
      Serial0.println("[cache] No edition in that direction.");
    }
    goToSleep(CHECK_INTERVAL_SECONDS);
  }
  if (!connectWifi(credentials) || !syncClock()) goToSleep(retryDelay());
  time_t now = time(nullptr);
  struct tm local{};
  localtime_r(&now, &local);
  char today[11];
  strftime(today, sizeof(today), "%Y-%m-%d", &local);
  String editionState = String(today) + ":" + RENDER_REVISION;
  String displayedEdition = stateOpen ? state.getString("edition", "") : "";
  bool backfillPending = stateOpen && state.getBool("backfill", false);
  // Every wake uses fresh NTP time, but never redraws an edition already shown.
  bool welcomeOnScreen = stateOpen &&
      state.getString("welcome_rev", "") == WELCOME_REVISION;
  if (action != WakeAction::Refresh && displayedEdition == editionState &&
      !welcomeOnScreen && !backfillPending)
    goToSleep(CHECK_INTERVAL_SECONDS);
  if (action != WakeAction::Refresh && displayedEdition == editionState &&
      !welcomeOnScreen && backfillPending) {
    bool complete = backfillPreviousEditions(now);
    uint8_t attempts = stateOpen ? state.getUChar("backfill_try", 0) + 1 : 3;
    if (stateOpen) {
      state.putUChar("backfill_try", attempts);
      if (complete || attempts >= 3) state.putBool("backfill", false);
    }
    goToSleep(CHECK_INTERVAL_SECONDS);
  }
  source::Image image;
  if (!source::fetchDailyJpeg(today, image)) {
    Serial0.printf("[source] %s; existing image retained.\n", source::lastError());
    if (image.data) free(image.data);
    goToSleep(retryDelay());
  }
  if (image.edition != today) {
    Serial0.println("[source] Wrong edition date; retaining existing image.");
    free(image.data);
    goToSleep(retryDelay());
  }
  if (!drawNewspaper(image, today, !initialSync)) goToSleep(retryDelay());
  uint8_t initialCompleted = 0;
  if (initialSync) {
    initialCompleted = 1;
    Serial0.println("[loading] 1 of 5 editions saved.");
    if (!showLoadingProgress(initialCompleted))
      Serial0.println("[loading] Could not advance the progress screen; continuing.");
  }
  bool initialBackfillComplete = false;
  if (initialSync)
    initialBackfillComplete = backfillPreviousEditions(now, &initialCompleted);
  if (initialSync && !showCachedEdition(today)) {
    Serial0.println("[cache] Could not reveal today's cached edition.");
    goToSleep(retryDelay());
  }
  if (stateOpen) {
    if (state.putString("edition", editionState) != editionState.length())
      Serial0.println("[state] Could not store edition; next wake may refresh again.");
    state.remove("welcome_rev");
    state.putString("shown", today);
    state.putString("display_rev", RENDER_REVISION);
  }
  Serial0.printf("[done] %s displayed without an observed panel error.\n", today);
  if (initialSync && stateOpen) {
    uint8_t attempts = state.getUChar("backfill_try", 0) + 1;
    state.putUChar("backfill_try", attempts);
    if (initialBackfillComplete || attempts >= 3) state.putBool("backfill", false);
  } else if (stateOpen && state.getBool("backfill", false)) {
    if (connectWifi(credentials)) {
      bool complete = backfillPreviousEditions(now);
      uint8_t attempts = state.getUChar("backfill_try", 0) + 1;
      state.putUChar("backfill_try", attempts);
      if (complete || attempts >= 3) state.putBool("backfill", false);
    }
  }
  goToSleep(CHECK_INTERVAL_SECONDS);
}

void loop() {}
