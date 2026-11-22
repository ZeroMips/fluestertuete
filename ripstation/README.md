# Flüstertüte CD ripping station (Raspberry Pi)

Insert an audio CD, wait, the disc ejects, the album is on the NAS - tagged, with cover, sorted into the
folder the Flüstertüte plays. Optionally the same Pi plays albums on a Bluetooth speaker for the Flüstertüte.

**Set it up:** <https://fluestertuete.zeromips.org/ripstation.html> writes three files for the SD card of a
fresh Raspberry Pi OS Lite; the first boot installs everything. By hand: write `/etc/cd-ripper/station.conf`
(see `setup-station.py`) and run `sudo ./install.sh` from this folder.

```
udev (audio disc inserted) -> cd-rip@sr0.service -> /usr/local/bin/cd-rip
   libdiscid -> MusicBrainz (GnuDB fallback) -> classify -> abcde (cdparanoia, lame, flac) -> retag + cover -> NAS -> eject
```

## Where things end up

The NAS share is mounted at `/mnt/nas` (SMB, NFS or WebDAV, see `setup-station.py`). Folders inside it, as
chosen on the setup page:

| Class | MP3 (default folder) |
|---|---|
| Kinderhörspiel | `Hörspiele/<Series>/<Album>/` (what the Flüstertüte plays) |
| Hörbuch (MusicBrainz "Audiobook") | `Hörbücher/...` |
| Musik | `Musik/<Artist>/<Album>/` |
| Unsortiert (no metadata, or audio drama without a children's signal) | `Unsortiert/...` |

Changing this is one line per destination in `/etc/cd-ripper/cd-ripper.conf` (`DEST_<CLASS>_<FORMAT>`).
A class is ripped in exactly the formats that have a `DEST` line (add `DEST_<CLASS>_FLAC` lines for FLAC too).
Files are named `NN - Title.mp3`, multi-disc sets become `Album (Disc N)`. If a target folder already
exists, the new one is named `Album (2)` - nothing is ever overwritten. A disc that was already ripped
(`/var/lib/cd-ripper/done.tsv`) is skipped and ejected; `cd-rip --force` overrides.

**Library check:** once the album is identified, the NAS (`LIBRARY_ROOTS`, default the whole share) is searched for the same artist/album (`Artist/Album`, `Genre/Artist/Album` or
`Artist - Album`; case, accents and punctuation ignored). Same album and same number of audio files ->
not ripped, disc ejected, the web page says where it is. The web button "Nächste CD trotzdem rippen" (or
`--force`) overrides once. A different track count counts as another edition: ripped, with a note.
Needs metadata (a disc nobody knows cannot be looked up) and the NAS mounted; `LIBRARY_CHECK=0` turns it off.

## How a disc is classified (first match wins)

1. **Manual choice** (optional hardware panel, `--kids`) made before inserting the disc wins over everything.
   The web page has no such setting: it shows the detection and lets you correct it while the disc rips.
2. **`/etc/cd-ripper/kinder-serien.txt`** matches artist, album or label (whole words, case-insensitive).
3. **Discogs** (only with `DISCOGS_TOKEN` set): genre "Children's" + style "Story".
4. MusicBrainz "Audiobook" -> Hörbuch; "Audio drama"/"Spokenword" without 1-3 -> Unsortiert.
5. MusicBrainz finds nothing (exact and by track lengths, see 6) -> **GnuDB** (the free CDDB, `GNUDB_CONTACT` = an e-mail address,
   GnuDB refuses anonymous requests; empty disables it). An entry is used only if every track length matches.
   Its free-text genre decides: "Kinder…" -> Kinderhörspiel, "Hörspiel"/"Hörbuch" -> as an audio drama in 4,
   anything else -> Musik. The series list (2) still applies first. GnuDB has no cover art.
6. Neither knows it -> Unsortiert (named `Unbekannt / Disc <date>`). Before giving up, a fuzzy
   lookup by track lengths is tried and accepted only if every track length matches. Such discs are
   listed with an attach link in `/var/lib/cd-ripper/attach-links.txt`: open the link in MusicBrainz to
   register your disc id so the next rip matches exactly.
   Wrong or missing metadata can be fixed afterwards: `cd-rip --relabel <jobdir> --release <MBID>`
   (for jobs still waiting for the NAS) or `cd-rip --release <MBID>` when ripping.
7. Everything else -> Musik.

**Adding a series:** append a line to `kinder-serien.txt` (`Name`, or `artist:`/`album:`/`label:` Name).
No restart needed; it is read for every disc. Die drei ??? / TKKG are commented out as borderline.

## Web UI

`http://ripstation.local/` (port `WEB_PORT`, optional login via `WEB_PASSWORD`): live status and progress,
what was detected for the disc (album, type and why, source, destination) with corrections while it rips
(another type, or another MusicBrainz release), "rip the next disc anyway", eject, retry of
waiting NAS copies, fixing metadata of a waiting album (paste a MusicBrainz release link), MusicBrainz
attach links, and the last finished albums. The page is plain HTML+JS served by `cd-web` (standard
library only). There is no encryption: keep it on the home network.

### JSON API (the Flüstertüte's ripper screen depends on it: keep it stable)

`GET /api/status` returns `status` (`state`, `progress` 0..1, `artist`, `album`, `cls` = type label, `message`,
`library` when the disc was skipped as already there, `detect` = {`source`, `reason`, `year`, `dests`, `choices`,
`hint`}), `classes` (type key -> label), `correction` ({`cls`, `release`} still pending), `force`, `jobs`
(`failed`), `history`, `attach`, `mounts`, `now`. Actions (`POST`, header `X-Requested-With: cd-web`, JSON body):
`/api/correct` {`cls`: key or null, `release`: MusicBrainz id/link or ""} (only while ripping), `/api/force`
{`on`}, `/api/eject`, `/api/flush`, `/api/relabel` {`job`, `release`}. `tests/test_cd_web.py` (`Contract`) pins
these names; add fields freely, but do not rename or remove any without changing the Flüstertüte client
(`main/ripper.c` in this repository).

## Bluetooth speaker (cd-bt)

The Pi also plays albums on a Bluetooth speaker (a boombox next to it), remote-controlled by the
Flüstertüte: the handheld sends the album, track and position, the Pi reads the MP3s from the stories folder
on `/mnt/nas` (`BT_SHARE_DIR`) and plays them with `mpg123` through bluez-alsa. Pair the speaker on the web
page ("Lautsprecher": speaker in pairing mode, "Suchen", "Koppeln"); `cd-bt` reconnects it when playback
starts. One speaker at a time: pairing another replaces it.

JSON API on `BT_PORT` (8081), same login and POST header as cd-web: `GET /api/bt/status` returns
`speaker` ({`address`, `name`, `paired`, `connected`}), `state` (`stopped`, `connecting`, `playing`, `paused`,
`error`), `message`, `finished` (the album played to its end), `album`, `track`, `index`, `count`, `pos`, `dur`
(seconds), `volume` (0..100), and `code` for `message`; errors are {`error`, `code`}. The handheld shows its own
text for a code (`album_end`, `connecting`, `speaker_unreachable`, `no_speaker`, `playback_aborted`, …) in its
own language and falls back to the German `message` for codes it does not know. Actions:
`/api/bt/play` {`album` (path as the player sees it, e.g. `/Hörspiele/…`), `track` (file name),
`pos`}, `/api/bt/toggle`, `/api/bt/stop`, `/api/bt/seek` {`delta`} or {`pos`}, `/api/bt/step` {`dir`},
`/api/bt/jump` {`index`}, `/api/bt/volume` {`volume`}, `/api/bt/connect`, `/api/bt/scan` (about 10 s, returns
`devices` [{`address`, `name`, `audio`, `paired`}]), `/api/bt/pair` {`address`}, `/api/bt/forget`. The web page reaches
these through cd-web (`/api/bt/…` on port 80 is passed on). Album paths: whatever the player sends; when the full
path is not found below `BT_SHARE_DIR`, its leading folders are dropped one by one. Tracks are in the player's order
(natural, umlauts with their base letter). Tests: `tests/test_cd_bt.py`.

## Hardware

The CD drive is all that is required. Everything below is an optional hardware panel (`cd-ui`,
not enabled by default: `sudo systemctl enable --now cd-ui`) for people who want a display and knob.

- Pi 5, powered USB CD drive (a slim drive on a bare Pi port often browns out).
- Display, default: 16x2 HD44780 LCD (1602A), 4-bit mode, no I2C backpack. LCD pin -> Pi:
  1 VSS -> GND, 2 VDD -> 5 V, 3 V0 -> contrast (10k trimmer between 5 V and GND, or ~1k to GND),
  4 RS -> GPIO22, 5 RW -> GND, 6 E -> GPIO23, 11 D4 -> GPIO24, 12 D5 -> GPIO25, 13 D6 -> GPIO5,
  14 D7 -> GPIO6, 15 A -> 5 V via 220 R, 16 K -> GND. D0-D3 unconnected.
  Pins are configurable (`LCD_PINS`); `UI_DISPLAY=oled` selects the SSD1327 OLED on I2C instead.
- Rotary encoder with push button (KY-040), powered from 3.3 V: `+` -> 3.3 V, GND -> GND,
  SW -> GPIO17, CLK -> GPIO27, DT -> GPIO26. Turn = class of the NEXT disc (Auto, Kinderhörspiel,
  Hörbuch, Musik, Unsortiert), press = back to Auto. The choice is used once, then reset.
- Buzzer (optional, active type such as KY-012): S -> GPIO12 (pin 32), `+` -> 3.3 V, `-` -> GND, and set
  `BUZZER_GPIO=12` in `cd-ripper.conf`. One short beep when ripping starts, two when an album is done,
  three quick ones when the disc is already in the library (nothing ripped), three long ones on an error. A passive buzzer (KY-006) only clicks; it needs PWM.
  Without `ENCODER_PINS` a single button on `BUTTON_GPIO` toggles "next disc is a Kinderhörspiel".

## Setup

1. Raspberry Pi Imager: "Raspberry Pi OS Lite (64-bit)", **no** OS customisation.
2. On <https://fluestertuete.zeromips.org/ripstation.html>: Wi-Fi, the NAS share (SMB, NFS or WebDAV) and its
   folders; download the three files and copy them onto the card's `bootfs` partition (overwrite).
3. Card into the Pi, CD drive (powered USB drive on a Pi 5) and network, power on. The first boot installs
   everything (about 10 minutes, internet needed); then `http://ripstation.local/` shows the station.
4. Log of the install: `/var/log/ripstation-install.log`. Settings later: edit
   `/etc/cd-ripper/station.conf`, then `sudo /usr/local/lib/ripstation/setup-station.py` (or run `install.sh` again).

NAS notes: SMB needs a user that may write the share. NFS (Synology: Control Panel > File Services > NFS,
NFSv4.1) needs read/write permission for the Pi's IP with squash "Map all users to admin". WebDAV
(Nextcloud, Synology "WebDAV Server") is mounted with davfs2; uploads happen when a file is closed.

## Operation and troubleshooting

- Watch a rip: `journalctl -u 'cd-rip@*' -f`. abcde's own log: `/var/lib/cd-ripper/jobs/<job>/abcde.log`.
- Rip by hand: `sudo -u ripper cd-rip /dev/sr0` (add `--kids`, `--force`).
- NAS down when a rip finishes: the finished job waits in `/var/lib/cd-ripper/jobs/` and is delivered by
  `cd-rip-flush.timer` (every 15 minutes) or `sudo -u ripper cd-rip --flush`. Failed rips stay there too.
- Disc is ejected with "Rippen fehlgeschlagen": scratched disc or drive power problem; see `abcde.log`.
- Nothing happens on insert: `udevadm monitor --property` should show `ID_CDROM_MEDIA_TRACK_COUNT_AUDIO`.
  Data discs are ignored on purpose.
- Wrong album from MusicBrainz: fix the release at musicbrainz.org (or move the folder on the NAS).
- No accuracy check: `abcde` uses `cdparanoia` (error-corrected, but without AccurateRip). `whipper` is
  the alternative if bit-perfect verification ever matters.
- Tests: `python3 -I tests/test_cd_rip.py` (needs `lame`, `flac`, `python3-mutagen` for the tag/delivery tests).
