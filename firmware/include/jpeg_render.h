#pragma once
#include <stddef.h>
#include <stdint.h>

namespace raster {
// Caller allocates width*height uint16_t pixels. No full source RGB image exists.
// Baseline JPEG only; progressive sources fail without touching the display.
bool decodeJpegToRgb565(const uint8_t* jpeg, size_t length, uint16_t* canvas,
                        int width, int height);
// Caller allocates width*height/2 bytes. Even dimensions required.
bool ditherRgb565ToPacked(const uint16_t* canvas, uint8_t* packed, int width, int height);
}
