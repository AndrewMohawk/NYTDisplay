# NYTDisplay

Firmware for a [Seeed reTerminal E1004](https://wiki.seeedstudio.com/getting_started_with_reterminal_e1004/) that shows the New York Times print front page on its six-color ePaper display. The device handles Wi-Fi setup, downloads and renders a dated page, keeps five editions, and sleeps between checks. No server or API key is required.

![Framed reTerminal E1004 showing a front page](assets/device-photo.jpg)

*An E1004 rendering a front page during development. This source edition has been built and checked on a host but has not yet been flashed to a physical E1004.*

## Build and install

You need an E1004, a USB-C data cable, 2.4 GHz Wi-Fi, Python 3.12, and PlatformIO 6.2.0. From this repository:

```sh
python3.12 -m pip install platformio==6.2.0
pio run -d firmware -e reterminal_e1004
pio device list
pio run -d firmware -e reterminal_e1004 -t upload --upload-port PORT
```

Replace `PORT` with the device's serial port. The first upload writes the bootloader, partition table, and application. Do not flash the application-only `firmware.bin` at offset zero. See [hardware and recovery](docs/HARDWARE.md) if the serial port or upload fails.

For browser-based USB installation, see the separate [NYTDisplay Installer](https://github.com/AndrewMohawk/NYTDisplay-Installer) with an experimental merged factory image built from this firmware.

## Use

On first boot, the display shows a setup QR code and starts a password-protected Wi-Fi network named **NYTDisplay**. Join it, open `http://192.168.4.1` if the setup page does not appear, and choose your home Wi-Fi. The device then downloads the five most recent editions.

Use the direction buttons to browse saved pages and the refresh button to check for today's edition. The device checks for updates every three hours. A failed download leaves the current image on screen. Hold all three face buttons for two seconds as it wakes to open the local maintenance page for application updates.

## Source and status

The firmware fetches a dated JPEG from [Freedom Forum's Today's Front Pages](https://frontpages.freedomforum.org/newspapers/ny_nyt-The_New_York_Times). This is an observed public website asset, not a supported API; its URL or format may change. No source newspaper JPEGs or cached editions are included here. See [source details](docs/SOURCES.md).

The code and original generated setup artwork are GPL-3.0-only; see [LICENSE](LICENSE) and [third-party notices](firmware/THIRD_PARTY.md). The device photo is included for illustration; its copyright remains with the photographer, and it does not license the newspaper content visible on its screen. This project is independent of and not endorsed by The New York Times or Freedom Forum.

The firmware builds and the host image/QR checks pass. Physical installation, Wi-Fi setup, daily download, buttons, battery behavior, and OTA updating still need validation with this source edition.
