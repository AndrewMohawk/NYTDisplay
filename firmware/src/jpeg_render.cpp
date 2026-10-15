#include "jpeg_render.h"
#include <JPEGDEC.h>
#include <algorithm>
#include <limits.h>
#include <memory>
#include <new>
#include <stdlib.h>
#include <string.h>
#ifdef ARDUINO
#include <Arduino.h>
#define RASTER_YIELD() yield()
#else
#define RASTER_YIELD() do {} while(0)
#endif

namespace raster {
namespace {
struct DecodeContext {
  uint16_t* canvas;
  int canvasWidth, canvasHeight, sourceWidth, sourceHeight;
  int targetWidth, targetHeight, offsetX, offsetY;
  size_t painted = 0;
};

int drawBlock(JPEGDRAW* block) {
  auto* c = static_cast<DecodeContext*>(block->pUser);
  if (!c || block->iBpp != 16 || !block->pPixels || block->x < 0 || block->y < 0)
    return 0;
  int used = block->iWidthUsed > 0 ? block->iWidthUsed : block->iWidth;
  int right = std::min(block->x + used, c->sourceWidth);
  int bottom = std::min(block->y + block->iHeight, c->sourceHeight);
  // Assign every destination pixel exactly once, including partial MCU blocks
  // and odd JPEG edges. Average the source footprint instead of selecting one
  // nearest pixel: the newspaper is only reduced by about 22%, and preserving
  // both pixels at those narrow boundaries makes small letter strokes much
  // closer to a high-quality desktop resize.
  int dx0 = std::max(0, (block->x * c->targetWidth + c->sourceWidth - 1) /
                            c->sourceWidth);
  int dx1 = std::min(c->canvasWidth - c->offsetX,
                     (right * c->targetWidth + c->sourceWidth - 1) /
                         c->sourceWidth);
  int dy0 = std::max(0, (block->y * c->targetHeight + c->sourceHeight - 1) /
                            c->sourceHeight);
  int dy1 = std::min(c->canvasHeight - c->offsetY,
                     (bottom * c->targetHeight + c->sourceHeight - 1) /
                         c->sourceHeight);
  for (int dy = dy0; dy < dy1; ++dy) {
    int sourceY0 = std::max(block->y,
                            int(int64_t(dy) * c->sourceHeight / c->targetHeight));
    int sourceY1 = std::min(bottom,
                            int((int64_t(dy + 1) * c->sourceHeight +
                                 c->targetHeight - 1) / c->targetHeight));
    for (int dx = dx0; dx < dx1; ++dx) {
      int sourceX0 = std::max(block->x,
                              int(int64_t(dx) * c->sourceWidth / c->targetWidth));
      int sourceX1 = std::min(right,
                              int((int64_t(dx + 1) * c->sourceWidth +
                                   c->targetWidth - 1) / c->targetWidth));
      if (sourceX0 >= sourceX1 || sourceY0 >= sourceY1) return 0;
      int red = 0, green = 0, blue = 0, samples = 0;
      for (int sy = sourceY0; sy < sourceY1; ++sy) {
        for (int sx = sourceX0; sx < sourceX1; ++sx) {
          uint16_t pixel = block->pPixels[(sy - block->y) * block->iWidth +
                                          sx - block->x];
          red += (pixel >> 11) & 31;
          green += (pixel >> 5) & 63;
          blue += pixel & 31;
          ++samples;
        }
      }
      red = (red + samples / 2) / samples;
      green = (green + samples / 2) / samples;
      blue = (blue + samples / 2) / samples;
      c->canvas[size_t(dy + c->offsetY) * c->canvasWidth + dx + c->offsetX] =
          uint16_t((red << 11) | (green << 5) | blue);
      ++c->painted;
    }
  }
  RASTER_YIELD();
  return 1;
}

int clamp8(int n) { return std::max(0, std::min(255, n)); }
int luminance(int red, int green, int blue) {
  // Integer Rec. 601 luma.  Newspaper body type is only a few destination
  // pixels high, so neutral pixels need a stable, high-contrast decision.
  return (77 * red + 150 * green + 29 * blue + 128) >> 8;
}
constexpr int palette[6][3] = {
    {0,0,0}, {255,255,255}, {29,185,84}, {0,76,255}, {229,57,53}, {255,216,0}
};

void rgb565(uint16_t pixel, int rgb[3]) {
  rgb[0] = int((pixel >> 11) & 31) * 255 / 31;
  rgb[1] = int((pixel >> 5) & 63) * 255 / 63;
  rgb[2] = int(pixel & 31) * 255 / 31;
}

void sharpenedRgb(const uint16_t* canvas, int width, int height,
                  int x, int y, int rgb[3]) {
  int center[3], left[3], right[3], above[3], below[3];
  rgb565(canvas[size_t(y) * width + x], center);
  rgb565(canvas[size_t(y) * width + (x ? x - 1 : x)], left);
  rgb565(canvas[size_t(y) * width + (x + 1 < width ? x + 1 : x)], right);
  rgb565(canvas[size_t(y ? y - 1 : y) * width + x], above);
  rgb565(canvas[size_t(y + 1 < height ? y + 1 : y) * width + x], below);
  for (int channel = 0; channel < 3; ++channel) {
    // Half-strength four-neighbour unsharp mask. It restores fine strokes lost
    // by the 1542-to-1200 box reduction without creating large edge halos.
    const int laplacian = 4 * center[channel] - left[channel] - right[channel] -
                          above[channel] - below[channel];
    rgb[channel] = clamp8(center[channel] + laplacian / 8);
  }
}

}

bool decodeJpegToRgb565(const uint8_t* bytes, size_t length, uint16_t* canvas,
                        int width, int height) {
  if (!bytes || !canvas || length < 4 || length > 2 * 1024 * 1024 ||
      width <= 0 || height <= 0 || width > 1600 || height > 1600 ||
      bytes[0] != 0xFF || bytes[1] != 0xD8) return false;
  auto decoder = std::unique_ptr<JPEGDEC>(new (std::nothrow) JPEGDEC());
  if (!decoder || !decoder->openRAM(const_cast<uint8_t*>(bytes), int(length), drawBlock))
    return false;
  int sw = decoder->getWidth(), sh = decoder->getHeight();
  if (decoder->getJPEGType() != JPEG_MODE_BASELINE || sw < 200 || sh < 200 ||
      sw > 8192 || sh > 8192 || uint64_t(sw) * sh > 20000000ULL) {
    decoder->close();
    return false;
  }
  // Newspaper body type becomes too small when the complete tall page is fit
  // inside the panel. Scale to the full display width and anchor at the top;
  // any overflow is deliberately cropped from the bottom.
  int tw = width, th = int(int64_t(sh) * width / sw);
  if (tw < 1 || th < 1) { decoder->close(); return false; }
  DecodeContext context{canvas,width,height,sw,sh,tw,th,0,0};
  for (size_t i = 0; i < size_t(width) * height; ++i) canvas[i] = 0xFFFF;
  decoder->setUserPointer(&context);
  decoder->setPixelType(RGB565_LITTLE_ENDIAN);
  bool ok = decoder->decode(0, 0, 0) == 1;
  decoder->close();
  return ok && context.painted == size_t(tw) * std::min(th, height);
}

bool ditherRgb565ToPacked(const uint16_t* canvas, uint8_t* packed, int width, int height) {
  if (!canvas || !packed || width <= 0 || height <= 0 || (width & 1) ||
      width > 1600 || height > 1600) return false;
  const size_t rowValues = size_t(width + 2) * 3;
  auto* errors = static_cast<int32_t*>(calloc(rowValues * 2, sizeof(int32_t)));
  auto* grayErrors = static_cast<int32_t*>(calloc(size_t(width + 2) * 2, sizeof(int32_t)));
  constexpr int tileSize = 16;
  const int tileColumns = (width + tileSize - 1) / tileSize;
  const int tileRows = (height + tileSize - 1) / tileSize;
  const size_t tileCount = size_t(tileColumns) * tileRows;
  auto* rawPhotoTiles = static_cast<uint8_t*>(calloc(tileCount, 1));
  auto* photoTiles = static_cast<uint8_t*>(calloc(tileCount, 1));
  if (!errors || !grayErrors || !rawPhotoTiles || !photoTiles) {
    free(errors);
    free(grayErrors);
    free(rawPhotoTiles);
    free(photoTiles);
    return false;
  }

  // Newspaper typography is mostly pure paper plus dark ink, while photographs
  // contain dense tone or chroma across a small region. Classify small tiles so
  // neutral photographs can be halftoned without turning body type into dots.
  for (int tileY = 0; tileY < tileRows; ++tileY) {
    for (int tileX = 0; tileX < tileColumns; ++tileX) {
      int nonPaper = 0, chromatic = 0, total = 0;
      const int xEnd = std::min(width, (tileX + 1) * tileSize);
      const int yEnd = std::min(height, (tileY + 1) * tileSize);
      for (int y = tileY * tileSize; y < yEnd; ++y) {
        for (int x = tileX * tileSize; x < xEnd; ++x) {
          int rgb[3];
          rgb565(canvas[size_t(y) * width + x], rgb);
          const int spread = std::max(rgb[0], std::max(rgb[1], rgb[2])) -
                             std::min(rgb[0], std::min(rgb[1], rgb[2]));
          nonPaper += luminance(rgb[0], rgb[1], rgb[2]) < 235;
          chromatic += spread >= 32;
          ++total;
        }
      }
      rawPhotoTiles[size_t(tileY) * tileColumns + tileX] =
          nonPaper * 100 > total * 65 || chromatic * 100 > total * 18;
    }
  }
  for (int tileY = 0; tileY < tileRows; ++tileY) {
    for (int tileX = 0; tileX < tileColumns; ++tileX) {
      int neighbours = 0;
      for (int y = std::max(0, tileY - 1); y <= std::min(tileRows - 1, tileY + 1); ++y)
        for (int x = std::max(0, tileX - 1); x <= std::min(tileColumns - 1, tileX + 1); ++x)
          neighbours += rawPhotoTiles[size_t(y) * tileColumns + x] != 0;
      const size_t tile = size_t(tileY) * tileColumns + tileX;
      photoTiles[tile] = rawPhotoTiles[tile] || neighbours >= 4;
    }
  }

  int32_t* row = errors;
  int32_t* next = errors + rowValues;
  int32_t* grayRow = grayErrors;
  int32_t* grayNext = grayErrors + width + 2;
  memset(packed, 0, size_t(width) * height / 2);
  for (int y = 0; y < height; ++y) {
    bool reverse = (y & 1) != 0;
    for (int n = 0; n < width; ++n) {
      int x = reverse ? width - 1 - n : n;
      int source[3];
      sharpenedRgb(canvas, width, height, x, y, source);
      int rgb[3];
      for (int k = 0; k < 3; ++k) rgb[k] = clamp8(source[k] + row[(x + 1) * 3 + k] / 16);
      int spread = std::max(source[0],std::max(source[1],source[2])) -
                   std::min(source[0],std::min(source[1],source[2]));
      const bool neutral = spread < 32;
      const bool photograph = photoTiles[size_t(y / tileSize) * tileColumns + x / tileSize];
      int best = 0;
      if (neutral && !photograph) {
        // Crisp paper/type path: no error diffusion speckle around tiny letters.
        best = luminance(source[0], source[1], source[2]) < 205 ? 0 : 1;
      } else if (neutral) {
        // Continuous neutral tones have no native gray ink. Preserve perceived
        // shading with black/white Floyd-Steinberg coverage instead of reducing
        // every gray in a photograph to one hard threshold.
        const int originalLuma = luminance(source[0], source[1], source[2]);
        const int adjustedLuma = clamp8(originalLuma + grayRow[x + 1] / 16);
        if (originalLuma <= 72) best = 0;
        else if (originalLuma >= 244) best = 1;
        else best = adjustedLuma < 128 ? 0 : 1;
        const int direction = reverse ? -1 : 1;
        const int error = adjustedLuma - (best ? 255 : 0);
        grayRow[x + 1 + direction] += error * 7;
        grayNext[x + 1 - direction] += error * 3;
        grayNext[x + 1] += error * 5;
        grayNext[x + 1 + direction] += error;
      } else {
        int distance = INT_MAX;
        for (int q = 0; q < 6; ++q) {
          int d = 0;
          for (int k = 0; k < 3; ++k) { int v = rgb[k] - palette[q][k]; d += v * v; }
          if (d < distance) { distance = d; best = q; }
        }
      }
      size_t index = (size_t(y) * width + x) / 2;
      if ((x & 1) == 0) packed[index] = (packed[index] & 15) | uint8_t(best << 4);
      else packed[index] = (packed[index] & 0xF0) | uint8_t(best);
      if (!neutral) {
        int direction = reverse ? -1 : 1;
        for (int k = 0; k < 3; ++k) {
          int e = rgb[k] - palette[best][k];
          row[(x + 1 + direction) * 3 + k] += e * 7;
          next[(x + 1 - direction) * 3 + k] += e * 3;
          next[(x + 1) * 3 + k] += e * 5;
          next[(x + 1 + direction) * 3 + k] += e;
        }
      }
    }
    std::swap(row, next);
    memset(next, 0, rowValues * sizeof(int32_t));
    std::swap(grayRow, grayNext);
    memset(grayNext, 0, size_t(width + 2) * sizeof(int32_t));
    RASTER_YIELD();
  }
  free(errors);
  free(grayErrors);
  free(rawPhotoTiles);
  free(photoTiles);
  return true;
}
}
