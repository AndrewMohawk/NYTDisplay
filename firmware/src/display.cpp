#include "display.h"
#include <Arduino.h>
#include <SPI.h>
#include "GxEPD2_T133A01_1200x1600.h"

namespace {
SPIClass panelSpi(HSPI);
GxEPD2_T133A01_1200x1600 panel(10, 11, 38, 13, 2, 12);
bool initialized = false;
}

bool showPackedFrame(const uint8_t* data, size_t length) {
  if (!data || length != DISPLAY_FRAME_BYTES || !psramFound()) return false;
  for (size_t i = 0; i < length; ++i) {
    if ((data[i] >> 4) > 5 || (data[i] & 15) > 5) return false;
  }
  if (!initialized) {
    panelSpi.begin(7, 8, 9, -1);
    panel.selectSPI(panelSpi, SPISettings(10000000, MSBFIRST, SPI_MODE0));
    initialized = true;
  }
  // init resets the sticky error and wakes the panel after a previous frame.
  panel.init(115200);
  if (!panel.ok()) {
    Serial0.printf("[panel] Initialization error: %s\n", panel.error());
    return false;
  }
  panel.writeNative(data, nullptr, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  Serial0.println("[panel] Full color refresh; allow around 40 seconds.");
  panel.refresh(false);
  if (!panel.ok()) {
    Serial0.printf("[panel] Refresh error: %s\n", panel.error());
    return false;
  }
  panel.hibernate();
  if (!panel.ok()) Serial0.printf("[panel] Hibernate error: %s\n", panel.error());
  return panel.ok();
}

void sleepPanel() {
  if (initialized && panel.ok()) panel.hibernate();
  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);
}
