// SPDX-License-Identifier: GPL-3.0-only
#include "welcome.h"
#include "display.h"
#include "loading_background.h"
#include "welcome_background.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <esp_heap_caps.h>
#include <qrcode.h>

namespace {
constexpr size_t kBytes = 1200u * 1600u / 2;
class PackedCanvas : public Adafruit_GFX {
 public:
  explicit PackedCanvas(uint8_t* pixels) : Adafruit_GFX(1200, 1600), data(pixels) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= 1200 || y >= 1600 || color > 5) return;
    size_t offset = size_t(y) * 600 + x / 2;
    if (x & 1) data[offset] = (data[offset] & 0xF0) | color;
    else data[offset] = (data[offset] & 0x0F) | (color << 4);
  }
 private:
  uint8_t* data;
};

String wifiEscape(const String& text) {
  String result;
  for (size_t i = 0; i < text.length(); ++i) {
    char c = text[i];
    if (c == '\\' || c == ';' || c == ':' || c == ',' || c == '"') result += '\\';
    result += c;
  }
  return result;
}

template <size_t N>
bool unpackBackground(const uint8_t (&encoded)[N], uint8_t* frame) {
  size_t written = 0;
  for (size_t i = 0; i + 1 < N; i += 2) {
    uint8_t count = pgm_read_byte(encoded + i);
    uint8_t value = pgm_read_byte(encoded + i + 1);
    if (!count || written + count > kBytes) return false;
    memset(frame + written, value, count);
    written += count;
  }
  return written == kBytes;
}
}

bool showLoadingProgress(uint8_t completed, uint8_t total) {
  if (!total || completed > total) return false;
  auto* frame = static_cast<uint8_t*>(heap_caps_malloc(
      kBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frame) return false;
  if (!unpackBackground(LOADING_BACKGROUND_RLE, frame)) {
    free(frame);
    return false;
  }
  PackedCanvas canvas(frame);
  constexpr int x = 110, y = 1280, width = 980, height = 80;
  const int filled = width * completed / total;
  if (filled) canvas.fillRect(x, y, filled, height, 0);
  for (uint8_t step = 1; step < total; ++step) {
    const int marker = x + width * step / total;
    canvas.drawFastVLine(marker, y, height, completed * width >= step * width ? 1 : 0);
  }
  canvas.setFont(&FreeMonoBold18pt7b);
  canvas.setTextColor(completed ? 1 : 0, completed ? 0 : 1);
  canvas.setCursor(500, 1334);
  canvas.print(completed);
  canvas.print(" / ");
  canvas.print(total);
  const bool okay = showPackedFrame(frame, kBytes);
  free(frame);
  return okay;
}

bool showWelcome(const String& ssid, const String& password) {
  // The baked UI names this SSID, while the password and QR are device-specific.
  if (ssid != "NYTDisplay" || password.length() < 8 || password.length() > 16) return false;
  auto* frame = static_cast<uint8_t*>(heap_caps_malloc(kBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frame) return false;
  if (!unpackBackground(WELCOME_BACKGROUND_RLE, frame)) { free(frame); return false; }
  PackedCanvas canvas(frame);
  canvas.setFont(&FreeMonoBold18pt7b);
  canvas.setTextColor(0);
  canvas.setCursor(535, 1293);
  canvas.print(password);

  const String payload = "WIFI:T:WPA;S:" + wifiEscape(ssid) + ";P:" + wifiEscape(password) + ";;";
  constexpr uint8_t version = 6;
  uint8_t qrBytes[1024];
  if (qrcode_getBufferSize(version) > sizeof(qrBytes)) { free(frame); return false; }
  QRCode qr;
  if (qrcode_initText(&qr, qrBytes, version, ECC_MEDIUM, payload.c_str()) != 0) {
    free(frame); return false;
  }
  constexpr int scale = 8, quiet = 4, x0 = 80, y0 = 1110;
  canvas.fillRect(x0, y0, (qr.size + 2 * quiet) * scale, (qr.size + 2 * quiet) * scale, 1);
  for (uint8_t y = 0; y < qr.size; ++y) {
    for (uint8_t x = 0; x < qr.size; ++x) {
      if (qrcode_getModule(&qr, x, y))
        canvas.fillRect(x0 + (x + quiet) * scale, y0 + (y + quiet) * scale, scale, scale, 0);
    }
  }
  const bool okay = showPackedFrame(frame, kBytes);
  free(frame);
  return okay;
}
