#include "provisioning.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <esp_wifi.h>

namespace provisioning {
namespace {
constexpr char AP_SSID[] = "NYTDisplay";
constexpr char NVS_NAMESPACE[] = "nyt-wifi";
constexpr uint8_t NEXT_BUTTON = 3;
constexpr uint8_t PREVIOUS_BUTTON = 4;
constexpr uint32_t PORTAL_LIFETIME_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t JOIN_TIMEOUT_MS = 30000;
constexpr uint32_t SUCCESS_DISPLAY_MS = 15000;
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_MASK(255, 255, 255, 0);

String apPassword;

bool credentialsValid(const Credentials& value) {
  if (value.ssid.length() == 0 || value.ssid.length() > 32) return false;
  // Empty passwords are valid for an open home network. WPA passphrases are
  // 8-63 bytes. A 64-character raw PSK is deliberately not accepted by this UI.
  if (value.password.length() != 0 &&
      (value.password.length() < 8 || value.password.length() > 63)) return false;
  for (size_t i = 0; i < value.ssid.length(); ++i)
    if (value.ssid[i] == '\0' || value.ssid[i] == '\r' || value.ssid[i] == '\n') return false;
  for (size_t i = 0; i < value.password.length(); ++i)
    if (value.password[i] == '\0' || value.password[i] == '\r' || value.password[i] == '\n') return false;
  return true;
}

void clearPassword(String& value) {
  for (size_t i = 0; i < value.length(); ++i) value.setCharAt(i, '\0');
  value = "";
}

String randomHex() {
  uint8_t random[16];
  esp_fill_random(random, sizeof(random));
  constexpr char hex[] = "0123456789abcdef";
  String result;
  result.reserve(32);
  for (uint8_t value : random) {
    result += hex[value >> 4];
    result += hex[value & 15];
  }
  return result;
}

bool constantEquals(const String& left, const String& right) {
  if (left.length() != right.length()) return false;
  uint8_t difference = 0;
  for (size_t i = 0; i < left.length(); ++i) difference |= left[i] ^ right[i];
  return difference == 0;
}

bool save(const Credentials& credentials) {
  if (!credentialsValid(credentials)) return false;
  StaticJsonDocument<768> document;
  document["version"] = 1;
  document["ssid"] = credentials.ssid;
  document["password"] = credentials.password;
  String encoded;
  if (!serializeJson(document, encoded)) return false;
  Preferences storage;
  if (!storage.begin(NVS_NAMESPACE, false)) return false;
  // One key is the commit record: a reset cannot pair a new SSID with an old
  // password. Preferences' NVS commit is completed by putString().
  bool committed = storage.putString("credentials", encoded) == encoded.length();
  storage.end();
  clearPassword(encoded);
  return committed;
}

constexpr char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light"><title>Set up your front-page display</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#f8f4ec;color:#243c39;font:16px/1.5 system-ui,-apple-system,sans-serif}main{max-width:440px;margin:auto;padding:32px 23px 42px}.art{display:block;margin:auto;width:132px;height:118px}.eyebrow{text-align:center;text-transform:uppercase;letter-spacing:.19em;font-size:10px;font-weight:700;color:#7b8271;margin:13px 0}h1{font:38px/1.08 Georgia,serif;letter-spacing:-1.2px;text-align:center;margin:12px 0}h1 i{font-weight:normal}.intro{text-align:center;color:#6b7469;font-size:15px;margin:15px 0 29px}.card{background:#fffdf8;border:1px solid #e8e2d5;border-radius:20px;padding:22px;box-shadow:0 8px 28px #243c3908}h2{font:24px/1.25 Georgia,serif;margin:0 0 9px}.detail{font-size:13px;color:#747b70;margin:0 0 22px}label{display:block;font-size:12px;font-weight:650;margin:15px 0 7px}input,select{width:100%;min-height:46px;border:1px solid #dcded2;border-radius:10px;background:#fffefb;padding:10px 11px;font:16px system-ui;color:#243c39}input:focus,select:focus{outline:2px solid #8fbaac;outline-offset:2px}button{cursor:pointer}button.primary{width:100%;min-height:49px;margin-top:21px;border:0;border-radius:11px;background:#284f46;color:white;font:600 14px system-ui}button:disabled{opacity:.6;cursor:wait}.scan{border:0;background:none;color:#456f62;font:12px system-ui;padding:8px 0}.note{font-size:11px;line-height:1.6;color:#818577;margin:13px 0 0}#status{font-size:14px;line-height:1.55;margin:0}#status:empty{display:none}#status:not(:empty){padding:15px 0 0}.error{color:#944c38}.success{color:#246147}.footer{text-align:center;margin:24px 0 0;color:#929486;font-size:10px;letter-spacing:.11em;text-transform:uppercase}[hidden]{display:none!important}
</style></head><body><main>
<svg class="art" viewBox="0 0 132 118" role="img" aria-label="Newspaper front page"><g fill="none" stroke="#315b50" stroke-width="3"><rect x="24" y="8" width="84" height="102" rx="4" fill="#fffdf8"/><path d="M35 24h62M35 34h62M35 43h62M35 88h62M35 97h48"/><rect x="35" y="51" width="30" height="29" fill="#d8b565"/><path d="M72 53h25M72 62h25M72 71h25M72 80h25"/></g></svg>
<p class="eyebrow">Daily print front page</p><h1>Set up your<br><i>display</i></h1>
<p class="intro">Connect once to show the latest edition each day.</p>
<section class="card"><h2>Connect to Wi-Fi</h2><p class="detail">Choose a 2.4 GHz home network to retrieve the front page.</p>
<form id="setup" method="post" action="/connect" autocomplete="off">
<input type="hidden" name="csrf" value="{{CSRF}}">
<label for="networks">Wi-Fi network</label><select id="networks"><option value="">Finding nearby networks…</option><option value="__manual">Type a network name…</option></select>
<input id="ssid" name="ssid" type="hidden" maxlength="32" autocomplete="off" aria-label="Wi-Fi network name">
<button class="scan" id="rescan" type="button">Look for networks again</button>
<label for="password">Wi-Fi password</label><input id="password" name="password" type="password" maxlength="63" autocomplete="new-password" aria-describedby="password-note">
<p class="note" id="password-note">Leave blank only if your home network has no password. Your password stays on this device.</p>
<button class="primary" id="submit" type="submit">Finish setup</button>
</form><p id="status" role="status" aria-live="polite"></p></section>
<p class="footer">Front-page display for reTerminal E1004</p>
</main><script nonce="{{NONCE}}">
const form=document.getElementById('setup'),networks=document.getElementById('networks'),ssid=document.getElementById('ssid'),statusBox=document.getElementById('status');
let busy=false,scanTimer,pollTimer;
function message(text,error=false){statusBox.textContent=text;statusBox.className=error?'error':'success';}
function lock(value){busy=value;form.querySelectorAll('input,select,button').forEach(e=>e.disabled=value);}
networks.addEventListener('change',()=>{const manual=networks.value==='__manual';ssid.type=manual?'text':'hidden';ssid.required=manual;ssid.value=manual?'':networks.value;if(manual){ssid.placeholder='Your Wi-Fi network name';ssid.focus();}});
async function scan(){if(busy)return;try{const response=await fetch('/scan',{cache:'no-store'});if(!response.ok)throw Error();const data=await response.json();if(data.scanning){scanTimer=setTimeout(scan,1400);return;}const selected=ssid.value;networks.replaceChildren(new Option('Choose your home Wi-Fi',''));data.networks.forEach(n=>networks.add(new Option(n.ssid+(n.open?' (no password)':''),n.ssid)));networks.add(new Option('Type a network name…','__manual'));if(selected){networks.value=selected;if(networks.selectedIndex<0){networks.value='__manual';ssid.type='text';ssid.required=true;}}if(!data.networks.length)message('No networks found yet. You can type your Wi-Fi name.');}catch(_){message('If the list is taking a moment, choose “Type a network name”.');}}
document.getElementById('rescan').addEventListener('click',async()=>{if(busy)return;clearTimeout(scanTimer);try{await fetch('/rescan',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'csrf='+encodeURIComponent(form.elements.csrf.value)});scan();}catch(_){message('Please stay connected to NYTDisplay and try again.',true);}});
async function poll(){try{const response=await fetch('/status',{cache:'no-store'});if(!response.ok)throw Error();const data=await response.json();message(data.message,data.state==='error');if(data.state==='connected'){document.getElementById('password').value='';form.hidden=true;return;}if(data.state==='error'||data.state==='idle'){lock(false);return;}pollTimer=setTimeout(poll,1000);}catch(_){message('Still checking… If your phone switched networks, reconnect to NYTDisplay.');pollTimer=setTimeout(poll,1700);}}
form.addEventListener('submit',async event=>{event.preventDefault();if(busy)return;if(!ssid.value){message('Choose your home Wi-Fi, or type its name.',true);return;}const body=new URLSearchParams(new FormData(form));lock(true);clearTimeout(scanTimer);message('Checking your Wi-Fi… Keep this page open.');try{const response=await fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const data=await response.json();if(!response.ok){message(data.message||'Please try again.',true);lock(false);return;}poll();}catch(_){message('Your phone may be changing networks. Reconnect to NYTDisplay if needed.');pollTimer=setTimeout(poll,1500);}});
scan();
</script></body></html>)HTML";

class Portal {
 public:
  WebServer web{80};
  DNSServer dns;
  String session;
  String scanResults = "[]";
  String state = "idle";
  String statusMessage = "Choose your home Wi-Fi to get started.";
  Credentials candidate;
  uint32_t joinStarted = 0;
  uint32_t connectedAt = 0;
  bool startJoin = false;
  bool scanning = false;
  bool scanRequested = true;
  bool connected = false;

  bool localClient() {
    IPAddress remote = web.client().remoteIP();
    return web.client().localIP() == AP_IP &&
           remote[0] == 192 && remote[1] == 168 && remote[2] == 4 &&
           remote[3] >= 2 && remote[3] <= 254;
  }

  bool canonicalHost() {
    const String host = web.hostHeader();
    return host == "192.168.4.1" || host == "192.168.4.1:80";
  }

  void commonHeaders() {
    web.sendHeader("Cache-Control", "no-store");
    web.sendHeader("X-Content-Type-Options", "nosniff");
    web.sendHeader("X-Frame-Options", "DENY");
    web.sendHeader("Referrer-Policy", "no-referrer");
  }

  void json(int status, const String& currentState, const String& message) {
    StaticJsonDocument<512> document;
    document["state"] = currentState;
    document["message"] = message;
    String body;
    serializeJson(document, body);
    commonHeaders();
    web.send(status, "application/json", body);
  }

  bool readAllowed(bool redirect = false) {
    if (!localClient()) { json(403, "error", "Connect to the display’s setup Wi-Fi first."); return false; }
    if (!canonicalHost()) {
      if (redirect) {
        commonHeaders();
        web.sendHeader("Location", String(PORTAL_URL) + "/");
        web.send(302, "text/plain", "Open the display setup page.");
      } else json(403, "error", "Open http://192.168.4.1 to continue.");
      return false;
    }
    return true;
  }

  bool writeAllowed() {
    if (!readAllowed()) return false;
    String origin = web.header("Origin");
    String referer = web.header("Referer");
    bool originOk = origin.isEmpty() || origin == PORTAL_URL || origin == String(PORTAL_URL) + ":80";
    bool refererOk = referer.isEmpty() || referer.startsWith(String(PORTAL_URL) + "/") || referer.startsWith(String(PORTAL_URL) + ":80/");
    if (!originOk || !refererOk || !constantEquals(web.arg("csrf"), session)) {
      json(403, "error", "This setup page expired. Reload it and try again.");
      return false;
    }
    return true;
  }

  void begin() {
    session = randomHex();
    const char* headers[] = {"Origin", "Referer"};
    web.collectHeaders(headers, 2);
    web.on("/", HTTP_GET, [this]() {
      if (!readAllowed(true)) return;
      String page(FPSTR(PAGE));
      page.replace("{{CSRF}}", session);
      page.replace("{{NONCE}}", session);
      commonHeaders();
      web.sendHeader("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; img-src 'self' data:; script-src 'nonce-" + session + "'; connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
      web.send(200, "text/html; charset=utf-8", page);
    });
    web.on("/scan", HTTP_GET, [this]() {
      if (!readAllowed()) return;
      commonHeaders();
      web.send(200, "application/json", String("{\"scanning\":") + (scanning || scanRequested ? "true" : "false") + ",\"networks\":" + scanResults + "}");
    });
    web.on("/rescan", HTTP_POST, [this]() {
      if (!writeAllowed()) return;
      if (state == "connecting" || connected) { json(409, state, statusMessage); return; }
      scanRequested = true;
      json(202, "scanning", "Looking for nearby networks.");
    });
    web.on("/status", HTTP_GET, [this]() {
      if (readAllowed()) json(200, state, statusMessage);
    });
    web.on("/connect", HTTP_POST, [this]() {
      if (!writeAllowed()) return;
      if (state == "connecting" || connected) { json(409, state, statusMessage); return; }
      Credentials proposed{web.arg("ssid"), web.arg("password")};
      if (!credentialsValid(proposed)) {
        clearPassword(proposed.password);
        json(400, "error", "Enter a Wi-Fi name (up to 32 bytes) and an 8–63 character password, or leave the password blank for an open network.");
        return;
      }
      candidate = proposed;
      clearPassword(proposed.password);
      state = "connecting";
      statusMessage = "Checking your Wi-Fi… Keep this page open.";
      startJoin = true;
      json(202, state, statusMessage);
    });
    web.onNotFound([this]() {
      if (!localClient()) { json(403, "error", "Connect to setup Wi-Fi first."); return; }
      if (web.method() != HTTP_GET) { json(404, "error", "Page not found."); return; }
      commonHeaders();
      web.sendHeader("Location", String(PORTAL_URL) + "/");
      web.send(302, "text/plain", "Open the display setup page.");
    });
    dns.start(53, "*", AP_IP);
    web.begin();
  }

  void updateScan() {
    if (scanRequested && !scanning && state != "connecting" && !connected) {
      WiFi.scanDelete();
      scanRequested = false;
      const int result = WiFi.scanNetworks(true, true);
      scanning = result != WIFI_SCAN_FAILED;
    }
    if (!scanning) return;
    int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return;
    scanning = false;
    DynamicJsonDocument document(8192);
    JsonArray networks = document.to<JsonArray>();
    for (int i = 0; i < count && networks.size() < 32; ++i) {
      const String name = WiFi.SSID(i);
      if (name.isEmpty()) continue;
      bool duplicate = false;
      for (JsonObject existing : networks)
        if (existing["ssid"].as<String>() == name) { duplicate = true; break; }
      if (duplicate) continue;
      JsonObject network = networks.createNestedObject();
      network["ssid"] = name;
      network["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
    scanResults = "";
    serializeJson(networks, scanResults);
    WiFi.scanDelete();
  }

  void updateJoin() {
    // Let an in-progress scan finish before WiFi.begin(), which otherwise can
    // fail immediately because the ESP32 radio is scanning another channel.
    if (startJoin && !scanning) {
      startJoin = false;
      scanRequested = false;
      WiFi.disconnect(false, false);
      if (WiFi.begin(candidate.ssid.c_str(), candidate.password.c_str()) == WL_CONNECT_FAILED) {
        WiFi.disconnect(false, false);
        clearPassword(candidate.password);
        state = "error";
        statusMessage = "The Wi-Fi connection couldn’t start. Please try again.";
        return;
      }
      joinStarted = millis();
    }
    if (state != "connecting" || startJoin) return;
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == candidate.ssid &&
        WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      if (save(candidate)) {
        connected = true;
        connectedAt = millis();
        state = "connected";
        statusMessage = "You’re all set! The editions are loading now. You can reconnect your phone to home Wi-Fi.";
      } else {
        state = "error";
        statusMessage = "Wi-Fi worked, but the device couldn’t save it. Please restart the display and try again.";
        WiFi.disconnect(false, false);
      }
      clearPassword(candidate.password);
    } else if (millis() - joinStarted >= JOIN_TIMEOUT_MS) {
      WiFi.disconnect(false, false);
      clearPassword(candidate.password);
      state = "error";
      statusMessage = "We couldn’t join that network. Check the Wi-Fi name and password, make sure it supports 2.4 GHz, and try again.";
    }
  }

  void end() {
    dns.stop();
    web.stop();
    WiFi.scanDelete();
    WiFi.softAPdisconnect(true);
    clearPassword(candidate.password);
  }
};
} // namespace

String setupSsid() { return AP_SSID; }

String setupPassword() {
  if (apPassword.length() == 12) return apPassword;
  Preferences storage;
  bool opened = storage.begin(NVS_NAMESPACE, false);
  if (opened) apPassword = storage.getString("setup_key", "");
  if (apPassword.length() != 12) {
    constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    static_assert(sizeof(alphabet) - 1 == 32, "AP alphabet must be unbiased 5-bit choices");
    uint8_t random[12];
    // Enabling Wi-Fi activates the hardware RNG's RF entropy source. The
    // welcome-screen caller may ask for this key before run() starts its AP.
    const wifi_mode_t previousMode = WiFi.getMode();
    if (previousMode == WIFI_MODE_NULL) {
      WiFi.persistent(false);
      WiFi.mode(WIFI_STA);
    }
    esp_fill_random(random, sizeof(random));
    if (previousMode == WIFI_MODE_NULL) WiFi.mode(WIFI_OFF);
    apPassword = "";
    apPassword.reserve(12);
    for (uint8_t value : random) apPassword += alphabet[value & 31];
    if (opened) storage.putString("setup_key", apPassword);
  }
  if (opened) storage.end();
  return apPassword;
}

bool load(Credentials& credentials) {
  Preferences storage;
  if (!storage.begin(NVS_NAMESPACE, true)) return false;
  String encoded = storage.getString("credentials", "");
  storage.end();
  StaticJsonDocument<768> document;
  bool valid = encoded.length() <= 768 && !deserializeJson(document, encoded) &&
               document["version"] == 1 && document["ssid"].is<const char*>() &&
               document["password"].is<const char*>();
  Credentials value;
  if (valid) {
    value.ssid = document["ssid"].as<String>();
    value.password = document["password"].as<String>();
    valid = credentialsValid(value);
  }
  clearPassword(encoded);
  if (valid) credentials = value;
  clearPassword(value.password);
  return valid;
}

bool setupRequested() {
  pinMode(NEXT_BUTTON, INPUT_PULLUP);
  pinMode(PREVIOUS_BUTTON, INPUT_PULLUP);
  if (digitalRead(NEXT_BUTTON) != LOW || digitalRead(PREVIOUS_BUTTON) != LOW)
    return false;
  const uint32_t start = millis();
  while (millis() - start < 2000) {
    if (digitalRead(NEXT_BUTTON) != LOW || digitalRead(PREVIOUS_BUTTON) != LOW)
      return false;
    delay(10);
  }
  return true;
}

Result run() {
  const String password = setupPassword();
  WiFi.persistent(false); // Unsuccessful candidates never enter SDK flash state.
  WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_AP_STA) || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
    WiFi.mode(WIFI_OFF);
    return Result::TimedOut;
  }
  WiFi.disconnect(false, false);
  if (!WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK) ||
      !WiFi.softAP(AP_SSID, password.c_str(), 1, false, 2)) {
    WiFi.mode(WIFI_OFF);
    return Result::TimedOut;
  }
  Portal portal;
  portal.begin();
  const uint32_t started = millis();
  while (millis() - started < PORTAL_LIFETIME_MS) {
    portal.dns.processNextRequest();
    portal.web.handleClient();
    portal.updateScan();
    portal.updateJoin();
    if (portal.connected && millis() - portal.connectedAt >= SUCCESS_DISPLAY_MS) break;
    delay(2);
  }
  const bool connected = portal.connected;
  portal.end();
  if (connected) {
    WiFi.mode(WIFI_STA);
    return Result::Connected;
  }
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  return Result::TimedOut;
}
} // namespace provisioning
