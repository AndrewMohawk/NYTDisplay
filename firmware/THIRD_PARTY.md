# Third-party components

| Component | Pinned source/version | License |
|---|---|---|
| Seeed board platform | `Seeed-Studio/platform-seeedboards` commit `6a313a19f06994909627250a5e082e637bd34ff6` | See upstream notices and downloaded package licenses |
| Seeed GxEPD2 fork | `Seeed-Projects/Seeed_GxEPD2` commit `1100ea37c16b910fd79152f4250c13d802b9c20b` | GPL-3.0 |
| T133A01 driver | Same GxEPD2 commit, example-local source vendored under `lib/T133A01` | GPL-3.0 |
| ArduinoJson | 6.21.5 | MIT |
| Adafruit GFX Library | 1.12.6 | BSD |
| Adafruit BusIO | 1.17.4 | MIT |
| JPEGDEC | `bitbank2/JPEGDEC` commit `86282979224c8a32fd51e091ed5a35b0c699a52b` | Apache-2.0 |
| QRCode | `ricmoo/QRCode` commit `eafbde494979abc2445c363cc2602230bcbe299c` | MIT |

The application firmware is GPL-3.0-only; full license text is in
[`../LICENSE`](../LICENSE). This firmware operates directly over Wi-Fi and
does not use a server.

The unmodified Amazon Trust Services root certificates embedded in `src/source.cpp`
are provided by Amazon under CC BY-ND 4.0; see [`docs/SOURCES.md`](../docs/SOURCES.md)
for their source and fingerprints. Their license is separate from the
application's GPL license.

T133A01 changes and provenance: [lib/T133A01/UPSTREAM.md](lib/T133A01/UPSTREAM.md).

Source URLs:

- https://github.com/Seeed-Studio/platform-seeedboards/tree/6a313a19f06994909627250a5e082e637bd34ff6
- https://github.com/Seeed-Projects/Seeed_GxEPD2/tree/1100ea37c16b910fd79152f4250c13d802b9c20b
- https://github.com/bblanchon/ArduinoJson/tree/v6.21.5
- https://github.com/bitbank2/JPEGDEC/tree/86282979224c8a32fd51e091ed5a35b0c699a52b
- https://github.com/ricmoo/QRCode/tree/eafbde494979abc2445c363cc2602230bcbe299c

Arduino-ESP32, ESP-IDF, mbedTLS, the compiler/toolchain, and transitive
dependencies are installed by the pinned platform. Their notices remain
in the downloaded PlatformIO packages. The distribution includes application
and modified driver source rather than a prebuilt binary.
