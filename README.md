# Flüstertüte

A handheld player for children's audio stories (Hörspiele) that streams them from your own NAS over Wi-Fi.
No figurines to buy, no cloud, no account: the stories stay where they already are, and the device only talks to
your Wi-Fi and your NAS. Made for a son who has outgrown shelves of CDs.

**Set it up:** <https://fluestertuete.zeromips.org/> explains the project; its setup page flashes the firmware and your settings straight from the
browser (Chrome or Edge on a computer), or gives you a package for `esptool` (Firefox, Safari).

## Devices

| Device | Input | Notes |
|---|---|---|
| [LilyGo T-Deck](https://www.lilygo.cc/products/t-deck) (ESP32-S3) | touch, trackball, keyboard | search by typing |
| [M5Stack Core2](https://docs.m5stack.com/en/core/core2) v1.1 (ESP32, AXP2101) | touch, three touch buttons | no search yet |

Both need nothing else: built-in speaker and display. The Core2 has a battery built in; the T-Deck runs from a
USB power supply alone, or from an added battery to carry it around.

## What it does

- Browse the folders of your library, start an album, it plays on from where it stopped last time (every album
  remembers its own position). "Weiterhören" on the home list resumes the last one.
- A folder that holds MP3s is an album. Tracks play in natural order ("Folge 2" before "Folge 10").
- Search (T-Deck): type anywhere; finds series, albums and tracks, tolerates typos and missing umlauts.
- The library is indexed in the background and kept on the device, so lists open instantly; new albums show up
  after the next scan (every 3 hours, sooner on a Synology through its media server, or at once with "Bibliothek
  neu einlesen").
- German or English on the device.
- Optional, with a Raspberry Pi running the CD ripper (separate project): see and steer the ripping station, and
  play on a Bluetooth speaker ("boombox") next to the Pi instead of the built-in speaker.

## What the NAS needs

One of:

- **Synology share link:** in File Station, share the folder with the stories, set a password, copy the link.
  The device only ever holds that link and its password, never an account password. Deleting the link cuts
  every device off.
- **WebDAV:** offered by almost every NAS (Synology "WebDAV Server" package, QNAP, Nextcloud, …). Best with a
  user of its own that may only read the stories folder.

HTTPS is checked against public certificate authorities; a NAS reached by IP whose certificate carries a name, or
one with a self-signed certificate, can be set up under "Erweitert" on the website.

## Controls

| | Lists | Player |
|---|---|---|
| Touch | tap a row; ♪ top right returns to the player | on-screen buttons; ☰ lists the tracks |
| T-Deck trackball up/down | select | volume |
| T-Deck trackball left/right | back / open | -30 s / +30 s |
| T-Deck trackball click, Space, Enter | open | play/pause |
| T-Deck backspace | back | back |
| T-Deck letter keys | search | search |
| Core2 button A / B (below the screen) | back / open | back / play-pause |

The screen goes dark after a minute; the first touch only wakes it.

## Building

ESP-IDF 5.5 (`. ~/esp/esp-idf/export.sh`):

```sh
idf.py build flash                                   # T-Deck (ESP32-S3), into build/
idf.py -B build-core2 -D SDKCONFIG=build-core2/sdkconfig -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.core2 \
       -D IDF_TARGET=esp32 build flash               # Core2 (ESP32), into build-core2/
tools/make_config.py settings.json --flash /dev/ttyACM0   # settings only; positions stay
tests/run.sh                                         # host tests (decoder vs. ffmpeg, parsers, settings block)
tools/web_bundle.py && python3 -m http.server -d web # the setup website with your builds, on localhost
```

`settings.json` is what the website's "Einstellungen speichern" writes, or a plain object with the keys listed
in `tools/make_config.py`. Without settings the device shows where to set it up.

A version tag (`v1.2.3`) builds both boards in CI and publishes the website with that firmware.

## How it is put together

- `main/board_tdeck.c`, `main/board_core2.c`: display, touch, buttons, power per device (`main/board.h`).
- `main/config.c`: the settings, read from their own 16 KB partition (`partitions.csv`), written by the website
  or `tools/make_config.py`. Same bytes from both (`tests/config_vector.json`).
- `main/nas.c` picks `nas_synology.c` or `nas_webdav.c` (+ `davxml.c`); each can be left out of a build
  (menuconfig "Flüstertüte").
- `main/player.c`: download into a 1 MB ring buffer in PSRAM, MP3 decoding (`mp3s.c`, Helix), I2S output; or,
  with the boombox, the same commands to the Pi (`boombox.c`).
- `main/index.c`, `main/search.c`: the library index and the search over it.
- `main/i18n.c`: all texts on the device, German and English.
- `web/`: the setup website (static; esptool-js in `web/vendor/`).
- On the Core2's ESP32, internal RAM is the scarce resource: task stacks live in PSRAM, and every flash access goes
  through `main/flash_guard.c` (a task with its stack in PSRAM must not touch flash). The `mem:` log line shows
  what is left once a minute.

## License

MIT, see `LICENSE`. Third-party parts (ESP-IDF, LVGL, the Helix MP3 decoder, esptool-js, Roboto) keep their
licenses, see `THIRD_PARTY.md`.

Contact: fluestertuete@zeromips.org
