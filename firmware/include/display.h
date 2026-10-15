#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr uint16_t DISPLAY_WIDTH = 1200;
constexpr uint16_t DISPLAY_HEIGHT = 1600;
constexpr size_t DISPLAY_FRAME_BYTES = size_t(DISPLAY_WIDTH) * DISPLAY_HEIGHT / 2;
// 4-bit indices: black0 white1 green2 blue3 red4 yellow5. High nibble is left pixel.
// Checks frame before a refresh. A hardware failure may leave a partial refresh.
bool showPackedFrame(const uint8_t* data, size_t length);
void sleepPanel();
