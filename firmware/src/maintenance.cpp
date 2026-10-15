// SPDX-License-Identifier: GPL-3.0-only
#include "maintenance.h"

#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <qrcode.h>

#include "display.h"
#include "settings.h"

namespace maintenance {
namespace {
constexpr uint8_t REFRESH_BUTTON = 5;
constexpr uint8_t NEXT_BUTTON = 3;
constexpr uint8_t PREVIOUS_BUTTON = 4;
constexpr uint32_t HOLD_MS = 2000;
constexpr uint32_t LIFETIME_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t RESTART_DELAY_MS = 1800;

String randomHex(size_t bytes) {
  constexpr char hex[] = "0123456789abcdef";
  String result;
  result.reserve(bytes * 2);
  for (size_t i = 0; i < bytes; ++i) {
    const uint8_t value = esp_random() & 0xff;
    result += hex[value >> 4];
    result += hex[value & 15];
  }
  return result;
}

String randomPin() {
  char value[7];
  snprintf(value, sizeof(value), "%06lu",
           static_cast<unsigned long>(esp_random() % 1000000UL));
  return String(value);
}

bool constantEquals(const String& left, const String& right) {
  if (left.length() != right.length()) return false;
  uint8_t difference = 0;
  for (size_t i = 0; i < left.length(); ++i) difference |= left[i] ^ right[i];
  return difference == 0;
}

class PackedCanvas : public Adafruit_GFX {
 public:
  explicit PackedCanvas(uint8_t* pixels)
      : Adafruit_GFX(DISPLAY_WIDTH, DISPLAY_HEIGHT), data(pixels) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT || color > 5) return;
    const size_t offset = size_t(y) * DISPLAY_WIDTH / 2 + x / 2;
    if (x & 1) data[offset] = (data[offset] & 0xf0) | color;
    else data[offset] = (data[offset] & 0x0f) | (color << 4);
  }
 private:
  uint8_t* data;
};

bool showScreen(const IPAddress& address, const String& pin) {
  auto* frame = static_cast<uint8_t*>(heap_caps_malloc(
      DISPLAY_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frame) return false;
  memset(frame, 0x11, DISPLAY_FRAME_BYTES);
  PackedCanvas canvas(frame);
  canvas.fillRoundRect(85, 190, 1030, 1220, 30, 1);
  canvas.drawRoundRect(85, 190, 1030, 1220, 30, 0);
  canvas.setTextColor(0);
  canvas.setFont(&FreeMonoBold18pt7b);
  canvas.setTextSize(2);
  canvas.setCursor(180, 325);
  canvas.print("Maintenance mode");
  canvas.setTextSize(1);
  canvas.setCursor(150, 430);
  canvas.print("Open this address on the same Wi-Fi:");
  const String url = String("http://") + address.toString() + "/";
  canvas.setCursor(150, 500);
  canvas.print(url);
  canvas.setCursor(150, 600);
  canvas.print("Access code:");
  canvas.setTextSize(2);
  canvas.setCursor(420, 700);
  canvas.print(pin);
  canvas.setTextSize(1);
  canvas.setCursor(150, 1330);
  canvas.print("Closes automatically after 10 minutes.");

  const String qrUrl = url + "?pin=" + pin;
  constexpr uint8_t version = 7;
  uint8_t qrBytes[1200];
  bool qrReady = qrcode_getBufferSize(version) <= sizeof(qrBytes);
  QRCode qr;
  if (qrReady) qrReady = qrcode_initText(
      &qr, qrBytes, version, ECC_MEDIUM, qrUrl.c_str()) == 0;
  if (qrReady) {
    constexpr int scale = 10, quiet = 4, x0 = 335, y0 = 770;
    const int size = (qr.size + 2 * quiet) * scale;
    canvas.fillRect(x0, y0, size, size, 1);
    for (uint8_t y = 0; y < qr.size; ++y)
      for (uint8_t x = 0; x < qr.size; ++x)
        if (qrcode_getModule(&qr, x, y))
          canvas.fillRect(x0 + (x + quiet) * scale,
                          y0 + (y + quiet) * scale, scale, scale, 0);
  }
  const bool okay = showPackedFrame(frame, DISPLAY_FRAME_BYTES);
  free(frame);
  return okay;
}

constexpr char LOGIN_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Front-page display maintenance</title><style>
*{box-sizing:border-box}body{margin:0;background:#f4f1ea;color:#202421;font:16px/1.5 system-ui,sans-serif}main{max-width:480px;margin:auto;padding:48px 22px}section{background:#fff;border:1px solid #d8d4ca;border-radius:18px;padding:25px}h1{font:32px Georgia,serif;margin:0 0 10px}p{color:#656b65}label{display:block;font-weight:650;margin:22px 0 7px}input,button{width:100%;min-height:48px;border-radius:10px;font:16px system-ui}input{border:1px solid #bbb;padding:10px}button{border:0;background:#223f39;color:#fff;margin-top:18px;font-weight:650}.error{color:#923f32}</style></head><body><main><section><h1>Display maintenance</h1><p>Enter the six-digit code shown on the display.</p>{{ERROR}}<form method="post" action="/login"><label for="pin">Access code</label><input id="pin" name="pin" inputmode="numeric" pattern="[0-9]{6}" maxlength="6" value="{{PIN}}" required autofocus><button type="submit">Continue</button></form></section></main></body></html>)HTML";

constexpr char ADMIN_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Front-page display maintenance</title><style>
*{box-sizing:border-box}body{margin:0;background:#f4f1ea;color:#202421;font:16px/1.5 system-ui,sans-serif}main{max-width:560px;margin:auto;padding:36px 20px 60px}section{background:#fff;border:1px solid #d8d4ca;border-radius:18px;padding:24px;margin:16px 0}h1{font:34px Georgia,serif;margin:0 0 5px}h2{font:23px Georgia,serif;margin:0 0 8px}p{color:#656b65}input[type=file]{width:100%;padding:12px;border:1px dashed #aaa;border-radius:10px}button{width:100%;min-height:46px;border:0;border-radius:10px;background:#223f39;color:#fff;font:650 15px system-ui;margin-top:13px}.secondary{background:#59635e}.danger{background:#8c3f32}.meta{font-size:13px;color:#777}</style></head><body><main><h1>Front-page display</h1><p class="meta">Firmware {{VERSION}} · Battery {{BATTERY}}</p><section><h2>Install firmware</h2><p>Select a firmware <code>.bin</code> built for this E1004. The device verifies the image, installs it into the inactive slot, and restarts.</p><form method="post" action="/firmware" enctype="multipart/form-data"><input type="hidden" name="csrf" value="{{CSRF}}"><input type="file" name="firmware" accept=".bin,application/octet-stream" required><button type="submit">Install and restart</button></form></section><section><h2>Content</h2><p>Ask the device to check its configured source now. If there is no newer edition, the current page stays on screen.</p><form method="post" action="/check"><input type="hidden" name="csrf" value="{{CSRF}}"><button type="submit">Check now</button></form></section><section><h2>Finish</h2><form method="post" action="/exit"><input type="hidden" name="csrf" value="{{CSRF}}"><button class="secondary" type="submit">Exit maintenance</button></form><form method="post" action="/restart"><input type="hidden" name="csrf" value="{{CSRF}}"><button class="danger" type="submit">Restart device</button></form></section></main></body></html>)HTML";

class Portal {
 public:
  WebServer web{80};
  String pin = randomPin();
  String session = randomHex(16);
  String csrf = randomHex(16);
  String version;
  String battery;
  Result result = Result::Exit;
  bool finished = false;
  bool restartPending = false;
  uint32_t restartAt = 0;
  bool uploadAllowed = false;
  bool uploadStarted = false;
  bool uploadOkay = false;

  explicit Portal(const char* firmwareVersion, int batteryPercent)
      : version(firmwareVersion ? firmwareVersion : "unknown"),
        battery(batteryPercent < 0 ? "unknown" : String(batteryPercent) + "%") {}

  void headers() {
    web.sendHeader("Cache-Control", "no-store");
    web.sendHeader("X-Content-Type-Options", "nosniff");
    web.sendHeader("X-Frame-Options", "DENY");
    web.sendHeader("Referrer-Policy", "no-referrer");
  }

  bool authenticated() {
    const String cookie = web.header("Cookie");
    return cookie.indexOf(String("nyt_session=") + session) >= 0;
  }

  bool sameOrigin() {
    const String origin = web.header("Origin");
    const String referer = web.header("Referer");
    const String host = web.hostHeader();
    const String expected = String("http://") + host;
    return (origin.isEmpty() || origin == expected) &&
           (referer.isEmpty() || referer.startsWith(expected + "/"));
  }

  bool writeAllowed() {
    if (!authenticated() || !sameOrigin() || !constantEquals(web.arg("csrf"), csrf)) {
      headers();
      web.send(403, "text/plain", "This maintenance session is not authorized. Reload the page.");
      return false;
    }
    return true;
  }

  void loginPage(bool error = false) {
    String page(FPSTR(LOGIN_PAGE));
    page.replace("{{ERROR}}", error ? "<p class=\"error\">That code did not match.</p>" : "");
    String supplied = web.arg("pin");
    if (supplied.length() != 6) supplied = "";
    page.replace("{{PIN}}", supplied);
    headers();
    web.sendHeader("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
    web.send(200, "text/html; charset=utf-8", page);
  }

  void adminPage() {
    String page(FPSTR(ADMIN_PAGE));
    page.replace("{{VERSION}}", version);
    page.replace("{{BATTERY}}", battery);
    page.replace("{{CSRF}}", csrf);
    headers();
    web.sendHeader("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
    web.send(200, "text/html; charset=utf-8", page);
  }

  void begin() {
    const char* collected[] = {"Cookie", "Origin", "Referer"};
    web.collectHeaders(collected, 3);
    web.on("/", HTTP_GET, [this]() {
      if (authenticated()) adminPage();
      else loginPage();
    });
    web.on("/login", HTTP_POST, [this]() {
      if (!constantEquals(web.arg("pin"), pin)) { loginPage(true); return; }
      headers();
      web.sendHeader("Set-Cookie", "nyt_session=" + session + "; Path=/; HttpOnly; SameSite=Strict");
      web.sendHeader("Location", "/");
      web.send(303, "text/plain", "Continue");
    });
    web.on("/check", HTTP_POST, [this]() {
      if (!writeAllowed()) return;
      result = Result::CheckNow;
      finished = true;
      headers();
      web.send(200, "text/html; charset=utf-8", "<h1>Checking now</h1><p>You can close this page.</p>");
    });
    web.on("/exit", HTTP_POST, [this]() {
      if (!writeAllowed()) return;
      finished = true;
      headers();
      web.send(200, "text/html; charset=utf-8", "<h1>Maintenance closed</h1><p>The saved page is being restored.</p>");
    });
    web.on("/restart", HTTP_POST, [this]() {
      if (!writeAllowed()) return;
      headers();
      web.send(200, "text/html; charset=utf-8", "<h1>Restarting</h1><p>You can close this page.</p>");
      restartPending = true;
      restartAt = millis();
    });
    web.on("/firmware", HTTP_POST,
      [this]() {
        if (!uploadAllowed) {
          headers();
          web.send(403, "text/plain", "This firmware upload was not authorized.");
          return;
        }
        headers();
        if (!uploadStarted || !uploadOkay || Update.hasError()) {
          web.send(400, "text/plain", "Firmware install failed. The existing firmware is still active.");
          return;
        }
        web.send(200, "text/html; charset=utf-8", "<h1>Firmware installed</h1><p>The display is restarting into the new version.</p>");
        restartPending = true;
        restartAt = millis();
      },
      [this]() {
        HTTPUpload& upload = web.upload();
        if (upload.status == UPLOAD_FILE_START) {
          uploadAllowed = authenticated() && sameOrigin() &&
                          constantEquals(web.arg("csrf"), csrf);
          uploadStarted = uploadAllowed;
          uploadOkay = uploadAllowed && Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
          if (!uploadOkay && uploadAllowed) Update.printError(Serial0);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (uploadOkay && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            uploadOkay = false;
            Update.printError(Serial0);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (uploadOkay) uploadOkay = Update.end(true);
          if (!uploadOkay) Update.printError(Serial0);
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
          Update.abort();
          uploadOkay = false;
        }
      });
    web.onNotFound([this]() {
      headers();
      web.sendHeader("Location", "/");
      web.send(302, "text/plain", "Open maintenance");
    });
    web.begin();
  }
};
}  // namespace

bool requested() {
  for (const uint8_t pin : {REFRESH_BUTTON, NEXT_BUTTON, PREVIOUS_BUTTON})
    pinMode(pin, INPUT_PULLUP);
  if (digitalRead(REFRESH_BUTTON) != LOW ||
      digitalRead(NEXT_BUTTON) != LOW ||
      digitalRead(PREVIOUS_BUTTON) != LOW) return false;
  const uint32_t started = millis();
  while (millis() - started < HOLD_MS) {
    if (digitalRead(REFRESH_BUTTON) != LOW ||
        digitalRead(NEXT_BUTTON) != LOW ||
        digitalRead(PREVIOUS_BUTTON) != LOW) return false;
    delay(10);
  }
  return true;
}

Result run(const provisioning::Credentials& credentials, const char* version,
           int batteryPercent) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.begin(credentials.ssid.c_str(), credentials.password.c_str());
  const uint32_t connectingAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - connectingAt < WIFI_CONNECT_TIMEOUT_MS) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    Serial0.println("[maintenance] Could not join saved Wi-Fi.");
    WiFi.mode(WIFI_OFF);
    return Result::Exit;
  }

  Portal portal(version, batteryPercent);
  Serial0.printf("[maintenance] Open http://%s/ and enter %s.\n",
                 WiFi.localIP().toString().c_str(), portal.pin.c_str());
  showScreen(WiFi.localIP(), portal.pin);
  portal.begin();
  const uint32_t started = millis();
  while (!portal.finished && millis() - started < LIFETIME_MS) {
    portal.web.handleClient();
    if (portal.restartPending && millis() - portal.restartAt >= RESTART_DELAY_MS) {
      portal.web.stop();
      Serial0.println("[maintenance] Restarting.");
      Serial0.flush();
      ESP.restart();
    }
    delay(2);
  }
  portal.web.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return portal.result;
}
}  // namespace maintenance
