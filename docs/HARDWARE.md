# E1004 installation and recovery

This firmware targets the **Seeed Studio reTerminal E1004**: ESP32-S3,
1200 × 1600 Spectra 6 panel, 8 MB PSRAM, and 32 MB physical flash. Put the
stand in portrait orientation. No microSD card is required by this project.
The Wi-Fi network must support 2.4 GHz. Seeed specifies USB-C power at
5 V / 1 A. Hardware identification and connector locations are shown in
[Seeed's E1004 guide](https://wiki.seeedstudio.com/getting_started_with_reterminal_e1004/).

## Build and flash once

1. Install Python and PlatformIO (`python -m pip install platformio==6.2.0`).
2. From the package's `firmware` directory run `pio run`. The first build
   downloads the pinned platform, compiler, and libraries; allow time for it.
3. Connect the E1004 with a **USB-C data cable**, turn its rear power switch
   **ON**, and close other programs using its serial port.
4. Run `pio device list` to identify the board's port. Run
   `pio run -t upload --upload-port PORT`, replacing `PORT` with its actual
   name, such as `COM5` or `/dev/ttyUSB0`. Automatic detection also works
   with `pio run -t upload` when there is only one matching device.
5. After upload, press **RESET** if the application does not start. A fresh
   device shows the Wi-Fi setup screen; allow the full color refresh to finish.

PlatformIO writes the bootloader, dual-slot OTA partition table, and application
at their configured offsets. Do not upload the application-only `firmware.bin`
at offset zero. No credentials need to be edited or compiled into the source.

The first installation of the browser-update build must use this complete
PlatformIO upload. The layout reserves two 4032 KiB application slots, 24 MiB
for LittleFS, and preserves NVS at its prior offset. Moving LittleFS means the
five-edition cache is rebuilt after this migration. This edition uses its own
Wi-Fi namespace, so it starts setup even if another firmware was previously
installed. Old credentials remain in NVS until a full erase.
Afterward, the maintenance page accepts the generated
`.pio/build/reterminal_e1004/firmware.bin` and writes it to the inactive slot.

The build follows [Seeed's PlatformIO recipe](https://wiki.seeedstudio.com/epaper_work_with_platformio/):
`seeed-xiao-esp32-s3-sense`, `qio_opi`, and `BOARD_HAS_PSRAM`. Despite that
board identifier, the panel and button pins in this project are for E1004.
The custom partition table uses the board's 32 MB physical flash. Upload speed is
115200, also Seeed's suggested fallback for unreliable uploads in the
[Arduino display cookbook](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino/).

## If the uploader cannot connect

Confirm the power switch is on, use a data-capable cable, and select the
correct serial port. The E1004 guide links the official WCH CH34x/CH341
USB-to-serial drivers for systems that need them. Close the serial monitor
before uploading. A sleeping application may need RESET before upload.

For manual ROM download mode:

1. Hold the **BOOT** button on the back.
2. While holding BOOT, press and release **RESET**.
3. Start the upload while keeping BOOT held through the initial connection.
4. Release BOOT once the uploader connects. After flashing, release both
   buttons and press RESET to run the application.

Seeed identifies the E1004's BOOT and RESET controls. The hold-BOOT/reset
sequence follows [Espressif's ESP32-S3 boot-mode documentation](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html):
GPIO0 low during reset selects the ROM downloader. This recovery procedure
has not yet been exercised with this edition on a physical unit.

## Buttons with this firmware

| Control | Behavior |
|---|---|
| Next + previous + refresh | From sleep, hold all three face buttons together for two seconds to open browser maintenance. No RESET or BOOT access is needed. |
| Refresh, GPIO5 | Short press wakes and forces a current-edition check. |
| Both direction buttons, GPIO3 + GPIO4 | Hold through reset for two seconds to reopen Wi-Fi setup. Existing credentials remain until a new connection is saved. |
| RESET | Restarts the application; saved settings remain. |
| BOOT | Selects the ROM downloader when held during reset; not a setup button. |
| Rear power switch | Cuts/restores power; the ePaper image remains visible. |

The two-second setup behavior belongs to this project. Seeed's stock
firmware uses different controls. The right-button mapping and active-low
wake behavior come from [Seeed's low-power cookbook](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino_peripherals_2/).

The setup screen opens automatically on a fresh installation. After 15 minutes
without completed setup, the portal sleeps until a face button or RESET wakes
the device. See the [setup steps](../README.md#first-run).

## Pin and display mapping

These are internal device assignments; no wiring is needed for normal use.
The panel driver is pinned to the official
[E1004 example](https://github.com/Seeed-Projects/Seeed_GxEPD2/tree/1100ea37c16b910fd79152f4250c13d802b9c20b/examples/GxEPD2_reTerminal_E1004).

| Signal | ESP32-S3 GPIO | Use |
|---|---:|---|
| SPI SCK / MISO / MOSI | 7 / 8 / 9 | Display SPI bus |
| Panel CS / CS1 | 10 / 2 | Two display controllers |
| Panel DC / RESET / BUSY | 11 / 38 / 13 | Command, reset, busy |
| Panel power enable | 12 | HIGH while active; held LOW in sleep |
| SD power enable | 16 | Unused; held LOW |
| KEY0 | 4 | Active-low right direction / wake button |
| Left direction | 3 | Active-low next-edition / wake button |
| Refresh | 5 | Active-low forced check / maintenance button |
| UART0 TX / RX | 43 / 44 | `Serial0` status output, 115200 baud |

The public eight-pin expansion header's UART uses GPIO41/42, which is a
different UART from these logs. Do not assume that header is UART0.
If the USB port exposes the board's UART0 bridge, run
`pio device monitor -b 115200 --port PORT` there. Native USB CDC monitoring
does not automatically receive `Serial0` output.

Images are packed as two pixels per byte, left pixel in the high nibble:
0 black, 1 white, 2 green, 3 blue, 4 red, 5 yellow. The driver translates
these to the panel's native color codes. Setup and newspaper use
the same 1200 × 1600 portrait mapping.

## Return to Seeed firmware

The E1004 guide links the official
[reTerminal E-Series Firmware Hub](https://seeed-projects.github.io/OSHW-reTerminal-Series-E-D/).
Select **E1004** and follow its browser flasher. Seeed's SenseCraft Seeedash
Firmware Flasher also offers device firmware. Check the selected model;
E1001/E1002/E1003 images do not target this panel. A full erase/full flash
removes stored settings, including this project's credentials and setup
password. Ordinary RESET and this project's setup button do not erase them.

## What still needs the physical display

Software build and host image/QR tests do not establish hardware operation.
On arrival, check upload and first boot, full-screen orientation and color,
QR scanning, successful 2.4 GHz setup, a newspaper download, button wake,
and the next scheduled wake.

No battery-life or sleep-current measurement has been made. This firmware
hibernates the panel and deep-sleeps the ESP32. It estimates battery level from
the board's ADC and overlays that estimate on displayed editions.
Network/image failures before refresh
retain the old image; a power loss or hardware failure during refresh can
leave a partial image that software cannot roll back. The driver detects
BUSY timeouts, not the visual contents of the display.
