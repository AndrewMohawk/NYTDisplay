# Driver provenance

The adjacent .h/.cpp files originate from Seeed Studio's maintained GxEPD2 fork:

https://github.com/Seeed-Projects/Seeed_GxEPD2/tree/1100ea37c16b910fd79152f4250c13d802b9c20b/examples/GxEPD2_reTerminal_E1004

License: GPL-3.0; full text is in `firmware/LICENSE`. The application firmware is
distributed under the same license. The driver is copied here because Seeed
ships it in an example directory, outside the library's compiled `src/` tree.

Local changes on 2026-09-29:

- Expose sticky `ok()` / `error()` state for BUSY and allocation failures.
- Return failure instead of hanging forever when PSRAM allocation fails.
- Replace the reset wait with the same timed BUSY polling used elsewhere.
- Stop initialization/frame transfer/refresh when a timed BUSY wait fails.
- Keep register values, color translation, SPI sequence, and controller split
  unchanged from the pinned upstream version.

The driver can observe BUSY timeouts and allocation failures. It cannot confirm
the visual contents of the physical panel. Returning success is not a visual
readback. The application stores a displayed hash only after refresh returns
without an observed driver error.

The panel framebuffer uses GxEPD2 indices 0 black, 1 white, 2 green, 3 blue,
4 red, 5 yellow. Its transfer method translates those to T133A01 hardware
codes 0, 1, 6, 5, 3, 2 respectively. Each byte holds the left/even pixel in its
high nibble. Do not apply Seeed_GFX palette codes to this driver.
