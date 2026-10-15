#include "source.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace source {
namespace {
constexpr size_t kMaxJpegBytes = 2U * 1024U * 1024U;
constexpr uint32_t kTransferLimitMs = 120000;
constexpr uint32_t kIdleLimitMs = 15000;
const char kCdnBase[] = "https://d2dr22b2lm4tvw.cloudfront.net/ny_nyt/";
String error;

// Amazon Trust Services' recommended custom trust store: Amazon Root CA 1-4
// and Starfield Services Root Certificate Authority G2. Roots are unmodified
// from https://www.amazontrust.com/repository/ (CC BY-ND 4.0). See SOURCES.md.
// This authenticates both hostname and chain; never falls back to setInsecure().
const char kAmazonRoots[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----

-----BEGIN CERTIFICATE-----
MIIFQTCCAymgAwIBAgITBmyf0pY1hp8KD+WGePhbJruKNzANBgkqhkiG9w0BAQwF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAyMB4XDTE1MDUyNjAwMDAwMFoXDTQwMDUyNjAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMjCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK2Wny2cSkxK
gXlRmeyKy2tgURO8TW0G/LAIjd0ZEGrHJgw12MBvIITplLGbhQPDW9tK6Mj4kHbZ
W0/jTOgGNk3Mmqw9DJArktQGGWCsN0R5hYGCrVo34A3MnaZMUnbqQ523BNFQ9lXg
1dKmSYXpN+nKfq5clU1Imj+uIFptiJXZNLhSGkOQsL9sBbm2eLfq0OQ6PBJTYv9K
8nu+NQWpEjTj82R0Yiw9AElaKP4yRLuH3WUnAnE72kr3H9rN9yFVkE8P7K6C4Z9r
2UXTu/Bfh+08LDmG2j/e7HJV63mjrdvdfLC6HM783k81ds8P+HgfajZRRidhW+me
z/CiVX18JYpvL7TFz4QuK/0NURBs+18bvBt+xa47mAExkv8LV/SasrlX6avvDXbR
8O70zoan4G7ptGmh32n2M8ZpLpcTnqWHsFcQgTfJU7O7f/aS0ZzQGPSSbtqDT6Zj
mUyl+17vIWR6IF9sZIUVyzfpYgwLKhbcAS4y2j5L9Z469hdAlO+ekQiG+r5jqFoz
7Mt0Q5X5bGlSNscpb/xVA1wf+5+9R+vnSUeVC06JIglJ4PVhHvG/LopyboBZ/1c6
+XUyo05f7O0oYtlNc/LMgRdg7c3r3NunysV+Ar3yVAhU/bQtCSwXVEqY0VThUWcI
0u1ufm8/0i2BWSlmy5A5lREedCf+3euvAgMBAAGjQjBAMA8GA1UdEwEB/wQFMAMB
Af8wDgYDVR0PAQH/BAQDAgGGMB0GA1UdDgQWBBSwDPBMMPQFWAJI/TPlUq9LhONm
UjANBgkqhkiG9w0BAQwFAAOCAgEAqqiAjw54o+Ci1M3m9Zh6O+oAA7CXDpO8Wqj2
LIxyh6mx/H9z/WNxeKWHWc8w4Q0QshNabYL1auaAn6AFC2jkR2vHat+2/XcycuUY
+gn0oJMsXdKMdYV2ZZAMA3m3MSNjrXiDCYZohMr/+c8mmpJ5581LxedhpxfL86kS
k5Nrp+gvU5LEYFiwzAJRGFuFjWJZY7attN6a+yb3ACfAXVU3dJnJUH/jWS5E4ywl
7uxMMne0nxrpS10gxdr9HIcWxkPo1LsmmkVwXqkLN1PiRnsn/eBG8om3zEK2yygm
btmlyTrIQRNg91CMFa6ybRoVGld45pIq2WWQgj9sAq+uEjonljYE1x2igGOpm/Hl
urR8FLBOybEfdF849lHqm/osohHUqS0nGkWxr7JOcQ3AWEbWaQbLU8uz/mtBzUF+
fUwPfHJ5elnNXkoOrJupmHN5fLT0zLm4BwyydFy4x2+IoZCn9Kr5v2c69BoVYh63
n749sSmvZ6ES8lgQGVMDMBu4Gon2nL2XA46jCfMdiyHxtN/kHNGfZQIG6lzWE7OE
76KlXIx3KadowGuuQNKotOrN8I1LOJwZmhsoVLiJkO/KdYE+HvJkJMcYr07/R54H
9jVlpNMKVv/1F2Rs76giJUmTtt8AF9pYfl3uxRuw0dFfIRDH+fO6AgonB8Xx1sfT
4PsJYGw=
-----END CERTIFICATE-----

-----BEGIN CERTIFICATE-----
MIIBtjCCAVugAwIBAgITBmyf1XSXNmY/Owua2eiedgPySjAKBggqhkjOPQQDAjA5
MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6b24g
Um9vdCBDQSAzMB4XDTE1MDUyNjAwMDAwMFoXDTQwMDUyNjAwMDAwMFowOTELMAkG
A1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJvb3Qg
Q0EgMzBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABCmXp8ZBf8ANm+gBG1bG8lKl
ui2yEujSLtf6ycXYqm0fc4E7O5hrOXwzpcVOho6AF2hiRVd9RFgdszflZwjrZt6j
QjBAMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgGGMB0GA1UdDgQWBBSr
ttvXBp43rDCGB5Fwx5zEGbF4wDAKBggqhkjOPQQDAgNJADBGAiEA4IWSoxe3jfkr
BqWTrBqYaGFy+uGh0PsceGCmQ5nFuMQCIQCcAu/xlJyzlvnrxir4tiz+OpAUFteM
YyRIHN8wfdVoOw==
-----END CERTIFICATE-----

-----BEGIN CERTIFICATE-----
MIIB8jCCAXigAwIBAgITBmyf18G7EEwpQ+Vxe3ssyBrBDjAKBggqhkjOPQQDAzA5
MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6b24g
Um9vdCBDQSA0MB4XDTE1MDUyNjAwMDAwMFoXDTQwMDUyNjAwMDAwMFowOTELMAkG
A1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJvb3Qg
Q0EgNDB2MBAGByqGSM49AgEGBSuBBAAiA2IABNKrijdPo1MN/sGKe0uoe0ZLY7Bi
9i0b2whxIdIA6GO9mif78DluXeo9pcmBqqNbIJhFXRbb/egQbeOc4OO9X4Ri83Bk
M6DLJC9wuoihKqB1+IGuYgbEgds5bimwHvouXKNCMEAwDwYDVR0TAQH/BAUwAwEB
/zAOBgNVHQ8BAf8EBAMCAYYwHQYDVR0OBBYEFNPsxzplbszh2naaVvuc84ZtV+WB
MAoGCCqGSM49BAMDA2gAMGUCMDqLIfG9fhGt0O9Yli/W651+kI0rz2ZVwyzjKKlw
CkcO8DdZEv8tmZQoTipPNU0zWgIxAOp1AE47xDqUEpHJWEadIRNyp4iciuRMStuW
1KyLa2tJElMzrdfkviT8tQp21KW8EA==
-----END CERTIFICATE-----

-----BEGIN CERTIFICATE-----
MIID7zCCAtegAwIBAgIBADANBgkqhkiG9w0BAQsFADCBmDELMAkGA1UEBhMCVVMx
EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxJTAjBgNVBAoT
HFN0YXJmaWVsZCBUZWNobm9sb2dpZXMsIEluYy4xOzA5BgNVBAMTMlN0YXJmaWVs
ZCBTZXJ2aWNlcyBSb290IENlcnRpZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5
MDkwMTAwMDAwMFoXDTM3MTIzMTIzNTk1OVowgZgxCzAJBgNVBAYTAlVTMRAwDgYD
VQQIEwdBcml6b25hMRMwEQYDVQQHEwpTY290dHNkYWxlMSUwIwYDVQQKExxTdGFy
ZmllbGQgVGVjaG5vbG9naWVzLCBJbmMuMTswOQYDVQQDEzJTdGFyZmllbGQgU2Vy
dmljZXMgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkgLSBHMjCCASIwDQYJKoZI
hvcNAQEBBQADggEPADCCAQoCggEBANUMOsQq+U7i9b4Zl1+OiFOxHz/Lz58gE20p
OsgPfTz3a3Y4Y9k2YKibXlwAgLIvWX/2h/klQ4bnaRtSmpDhcePYLQ1Ob/bISdm2
8xpWriu2dBTrz/sm4xq6HZYuajtYlIlHVv8loJNwU4PahHQUw2eeBGg6345AWh1K
Ts9DkTvnVtYAcMtS7nt9rjrnvDH5RfbCYM8TWQIrgMw0R9+53pBlbQLPLJGmpufe
hRhJfGZOozptqbXuNC66DQO4M99H67FrjSXZm86B0UVGMpZwh94CDklDhbZsc7tk
6mFBrMnUVN+HL8cisibMn1lUaJ/8viovxFUcdUBgF4UCVTmLfwUCAwEAAaNCMEAw
DwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYEFJxfAN+q
AdcwKziIorhtSpzyEZGDMA0GCSqGSIb3DQEBCwUAA4IBAQBLNqaEd2ndOxmfZyMI
bw5hyf2E3F/YNoHN2BtBLZ9g3ccaaNnRbobhiCPPE95Dz+I0swSdHynVv/heyNXB
ve6SbzJ08pGCL72CQnqtKrcgfU28elUSwhXqvfdqlS5sdJ/PHLTyxQGjhdByPq1z
qwubdQxtRbeOlKyWN7Wg0I8VRw7j6IPdj/3vQQF3zCepYoUz8jcI73HPdwbeyBkd
iEDPfUYd/x7H4c7/I9vG+o1VTqkC50cRRj70/b17KSa7qWFiNyi2LSr2EIZkyXCn
0q23KXB56jzaYyWf/Wi3MOxw+3WKt21gZ7IeyLnp2KhvAotnDU0mV3HaIPzBSlCN
sSi6
-----END CERTIFICATE-----
)PEM";

bool validDate(const char *s) {
  if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
  for (int i = 0; i < 10; ++i) {
    if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return false;
  }
  const int year = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + s[3]-'0';
  const int month = (s[5]-'0')*10 + s[6]-'0';
  const int day = (s[8]-'0')*10 + s[9]-'0';
  const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year < 2024 || year > 2099 || month < 1 || month > 12) return false;
  const bool leap = (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
  const int maxDay = days[month - 1] + (month == 2 && leap ? 1 : 0);
  return day >= 1 && day <= maxDay;
}

// Confirm a complete, baseline 8-bit JPEG at useful newspaper resolution before
// handing untrusted remote bytes to JPEGDEC. Decoder still validates the stream.
bool validJpeg(const uint8_t *data, size_t size) {
  if (size < 32 || data[0] != 0xff || data[1] != 0xd8 ||
      data[size - 2] != 0xff || data[size - 1] != 0xd9) return false;
  size_t pos = 2;
  while (pos + 4 <= size) {
    if (data[pos++] != 0xff) return false;
    while (pos < size && data[pos] == 0xff) ++pos;
    if (pos >= size) return false;
    const uint8_t marker = data[pos++];
    if (marker == 0xda || marker == 0xd9) return false;  // No baseline SOF yet.
    if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
    if (pos + 2 > size) return false;
    const size_t length = (size_t(data[pos]) << 8) | data[pos + 1];
    if (length < 2 || length > size - pos) return false;
    if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
      if (marker != 0xc0 || length < 11 || data[pos + 2] != 8) return false;
      const unsigned height = (unsigned(data[pos + 3]) << 8) | data[pos + 4];
      const unsigned width = (unsigned(data[pos + 5]) << 8) | data[pos + 6];
      const unsigned components = data[pos + 7];
      return (components == 1 || components == 3) && length == 8 + 3 * components &&
             width >= 600 && height >= 800 && width <= 4096 && height <= 8192 &&
             uint32_t(width) * height <= 12000000U;
    }
    pos += length;
  }
  return false;
}
}  // namespace

const char *lastError() { return error.c_str(); }

bool fetchDailyJpeg(const char *requestedISODate, Image &out) {
  error = "";
  // Don't discard a caller-owned allocation on accidental reuse.
  if (out.data) { error = "Release the previous JPEG before fetching another"; return false; }
  out.length = 0;
  out.edition = "";
  if (!validDate(requestedISODate)) { error = "Invalid requested edition date"; return false; }
  if (time(nullptr) < 1704067200) { error = "Clock must be synchronized before HTTPS"; return false; }

  const String url = String(kCdnBase) + requestedISODate + "/front-page-large.jpg";
  WiFiClientSecure client;
  client.setCACert(kAmazonRoots);
  client.setHandshakeTimeout(15);
  client.setTimeout(kIdleLimitMs);
  HTTPClient http;
  http.setConnectTimeout(kIdleLimitMs);
  http.setTimeout(kIdleLimitMs);
  http.setReuse(false);
  http.useHTTP10(true);  // Fixed Content-Length; no chunked stream ambiguity.
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char *headers[] = {"Content-Type", "Content-Encoding"};
  http.collectHeaders(headers, 2);
  uint8_t *buffer = nullptr;
  auto fail = [&](const String &message) {
    error = message;
    free(buffer);
    http.end();
    Serial0.printf("[source] %s\n", error.c_str());
    return false;
  };
  if (!http.begin(client, url)) return fail("Could not initialize HTTPS request");
  http.setUserAgent("NYT-E1004-personal-display/2.0");
  http.addHeader("Accept", "image/jpeg");
  http.addHeader("Accept-Encoding", "identity");
  const uint32_t started = millis();
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    if (status == HTTP_CODE_NOT_FOUND) return fail("Today's edition is not published yet (404)");
    return fail(String("HTTPS request failed, code ") + status);
  }
  String contentType = http.header("Content-Type");
  contentType.toLowerCase();
  const int semicolon = contentType.indexOf(';');
  if (semicolon >= 0) contentType = contentType.substring(0, semicolon);
  contentType.trim();
  if (contentType != "image/jpeg") return fail("Source did not return image/jpeg");
  String encoding = http.header("Content-Encoding");
  encoding.toLowerCase();
  encoding.trim();
  if (encoding.length() && encoding != "identity") return fail("Unexpected compressed HTTP response");
  const int expected = http.getSize();
  if (expected <= 0 || size_t(expected) > kMaxJpegBytes)
    return fail("JPEG Content-Length is missing, empty, or greater than 2 MiB");
  buffer = static_cast<uint8_t *>(heap_caps_malloc(size_t(expected), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buffer) return fail("Not enough PSRAM for the JPEG download");
  auto *stream = http.getStreamPtr();
  size_t received = 0;
  uint32_t lastProgress = millis();
  while (received < size_t(expected)) {
    if (uint32_t(millis() - started) > kTransferLimitMs ||
        uint32_t(millis() - lastProgress) > kIdleLimitMs)
      return fail("JPEG download timed out");
    const int available = stream->available();
    if (available > 0) {
      const size_t want = std::min(size_t(available), size_t(expected) - received);
      const int count = stream->read(buffer + received, want);
      if (count < 0) return fail("JPEG connection read failed");
      if (count > 0) { received += size_t(count); lastProgress = millis(); }
    } else if (!http.connected()) {
      return fail("JPEG download ended before Content-Length");
    }
    delay(1);  // Wi-Fi/watchdog servicing during a bounded download.
  }
  http.end();
  if (!validJpeg(buffer, received)) return fail("JPEG is incomplete, unsupported, too small, or too large");
  out.data = buffer;
  out.length = received;
  out.edition = requestedISODate;  // Exact dated public resource, no stale fallback.
  Serial0.printf("[source] Downloaded %u bytes for %s\n", unsigned(received), requestedISODate);
  return true;
}
}  // namespace source
