// Host integration check. Compile with src/jpeg_render.cpp and pinned JPEGDEC.cpp.
// Usage: ./test_renderer source.jpg preview.ppm packed.bin
#include "jpeg_render.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
  assert(argc == 4);
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> jpeg((std::istreambuf_iterator<char>(input)), {});
  assert(!jpeg.empty());
  std::vector<uint16_t> canvas(1200 * 1600);
  assert(raster::decodeJpegToRgb565(jpeg.data(), jpeg.size(), canvas.data(), 1200, 1600));
  std::vector<uint8_t> packed(1200 * 1600 / 2);
  assert(raster::ditherRgb565ToPacked(canvas.data(), packed.data(), 1200, 1600));
  const uint8_t colors[6][3] = {
    {0,0,0},{255,255,255},{29,185,84},{0,76,255},{229,57,53},{255,216,0}
  };
  size_t counts[6] = {};
  std::ofstream ppm(argv[2], std::ios::binary);
  ppm << "P6\n1200 1600\n255\n";
  for (uint8_t b : packed) {
    for (int index : {b >> 4, b & 15}) {
      assert(index < 6);
      ++counts[index];
      ppm.write(reinterpret_cast<const char*>(colors[index]), 3);
    }
  }
  std::ofstream output(argv[3], std::ios::binary);
  output.write(reinterpret_cast<const char*>(packed.data()), packed.size());
  assert(counts[0] > 1000 && counts[1] > 1000);
  for (int i = 0; i < 6; ++i) std::cout << i << ':' << counts[i] << ' ';
  std::cout << "\nJPEG decode, full coverage, packing, and six-color dither passed.\n";
  const uint8_t invalid[4] = {0,0,0,0};
  assert(!raster::decodeJpegToRgb565(invalid, sizeof(invalid), canvas.data(), 1200, 1600));
  assert(!raster::ditherRgb565ToPacked(canvas.data(), packed.data(), 1199, 1600));
  // Sparse neutral newspaper pixels remain deterministic rather than turning
  // antialiased type and paper texture into error-diffusion speckle.
  const uint16_t neutral[] = {0x0000, 0xC618, 0xDEFB, 0xFFFF};
  uint8_t neutralPacked[2] = {};
  assert(raster::ditherRgb565ToPacked(neutral, neutralPacked, 4, 1));
  assert(neutralPacked[0] == 0x00);  // black and mid-gray ink -> black
  assert(neutralPacked[1] == 0x11);  // near-white paper and white -> white
  // A dense neutral tile is photographic content and must retain middle-tone
  // coverage instead of collapsing all gray to solid black.
  std::vector<uint16_t> grayPhoto(16 * 16, 0x8410);
  std::vector<uint8_t> grayPacked(16 * 16 / 2);
  assert(raster::ditherRgb565ToPacked(grayPhoto.data(), grayPacked.data(), 16, 16));
  size_t black = 0, white = 0;
  for (uint8_t byte : grayPacked) {
    black += (byte >> 4) == 0;
    black += (byte & 15) == 0;
    white += (byte >> 4) == 1;
    white += (byte & 15) == 1;
  }
  assert(black > 64 && white > 64);
  return 0;
}
