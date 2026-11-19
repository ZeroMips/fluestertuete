#!/usr/bin/env python3
"""Builds the device settings (the "config" partition) and optionally flashes them.

  tools/make_config.py settings.json [--out build/config.bin] [--flash /dev/ttyACM0]

settings.json holds the same keys the setup website (web/) writes (its "Save settings" file works too): lang ("de" | "en"), wifi_ssid, wifi_password, nas_type
("synology" | "webdav"), nas_url, root, share_id, share_password | dav_user, dav_password, optional tls_name,
ca_pem, ripper_url, ripper_password, boombox_url, dlna_url. Flashing touches only the config partition:
listening positions stay. The output contains passwords: it is written with mode 600 and stays out of git.
Format (also in main/config.h and web/config.js): "FLUE", u16 version 1, u16 0, u32 length, u32 CRC-32, JSON."""
import argparse
import json
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
CONFIG_OFFSET = 0x4A0000
CONFIG_SIZE = 0x4000
KEYS = ["lang", "wifi_ssid", "wifi_password", "nas_type", "nas_url", "tls_name", "ca_pem", "root", "share_id",
        "share_password", "dav_user", "dav_password", "ripper_url", "ripper_password", "boombox_url", "dlna_url"]


def pack(settings):
    """Header + JSON, exactly as web/config.js builds it (tests/config_vector.json pins both)."""
    clean = {k: settings[k] for k in KEYS if settings.get(k)}
    body = json.dumps(clean, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(body) + 16 > CONFIG_SIZE:
        sys.exit(f"settings too large: {len(body)} bytes")
    return b"FLUE" + struct.pack("<HHII", 1, 0, len(body), zlib.crc32(body) & 0xFFFFFFFF) + body


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("settings", help="settings JSON file")
    ap.add_argument("--out", default=os.path.join(PROJECT, "build", "config.bin"))
    ap.add_argument("--flash", metavar="PORT", help="write the config partition on this serial port")
    a = ap.parse_args()
    settings = json.load(open(a.settings, encoding="utf-8"))
    settings = settings.get("settings", settings)  # the website's file wraps them
    data = pack(settings)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    fd = os.open(a.out, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(data)
    print(f"wrote {a.out} ({len(data)} bytes)")
    if a.flash:
        subprocess.run([sys.executable, "-m", "esptool", "--port", a.flash, "write_flash", hex(CONFIG_OFFSET), a.out],
                       check=True)


if __name__ == "__main__":
    main()
