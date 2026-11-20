#!/usr/bin/env python3
"""Take a screenshot of a running Flüstertüte over its serial console.

    tools/screenshot.py /dev/ttyUSB0 shot.png [--scale 2] [--boot]

Sends 's', reads the RGB565 dump that main/screenshot.c prints and writes a PNG. Leaves DTR/RTS as they
are, so the device is not reset. Needs pyserial and Pillow.
"""
import argparse
import base64
import sys
import time

import serial
from PIL import Image


def read_dump(s: serial.Serial, chunks: dict, timeout: float) -> tuple:
    """One dump into chunks (offset -> bytes); returns (w, h, stride)."""
    s.reset_input_buffer()
    s.write(b"s\n")
    deadline = time.monotonic() + timeout
    header = None
    while time.monotonic() < deadline:
        line = s.readline().decode("ascii", "replace").strip()
        if header is None:
            if line == "SHOT FAILED":
                raise SystemExit("the device could not take the snapshot (out of memory?)")
            if line.startswith("SHOT ") and line != "SHOT END":
                header = tuple(int(v) for v in line.split()[1:4])
            continue
        if line == "SHOT END":
            return header
        off, sep, b64 = line.partition(":")  # "<hex offset>:<base64>"; anything else slipped in between
        if sep:
            try:
                chunks.setdefault(int(off, 16), base64.b64decode(b64, validate=True))
            except ValueError:
                pass
    raise SystemExit("no screenshot from the device (is it running this firmware?)")


def restart_and_wait(s: serial.Serial, timeout: float = 15.0) -> None:
    """Restart the device and return as soon as its start screen is up (for a picture of that screen)."""
    s.dtr = False  # RTS up, DTR down: EN low
    time.sleep(0.1)
    s.dtr = True   # both up again: EN released, boots normally
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if b"main_task: Calling app_main()" in s.readline():
            time.sleep(1.2)  # settings, index, display: then the start screen is up and Wi-Fi not yet there
            return
    raise SystemExit("the device did not restart")


def grab(port: str, timeout: float = 150.0, tries: int = 4, boot: bool = False) -> Image.Image:
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 1
    # Linux raises DTR and RTS when the port opens. Keep them both up: dropping one of them first (pyserial's
    # default order) pulls EN low through the auto-reset circuit and restarts the device.
    s.dtr = True
    s.rts = True
    s.open()
    chunks: dict = {}
    try:
        if boot:
            restart_and_wait(s)
            tries = 1  # the screen changes right after: one go
        # Other output can damage a line now and then: ask again and fill the gaps (the screen must not change).
        for _ in range(tries):
            w, h, stride = read_dump(s, chunks, timeout)
            total = stride * h
            missing = [o for o in range(0, total, 54) if len(chunks.get(o, b"")) != min(54, total - o)]
            for o in missing:
                chunks.pop(o, None)
            if not missing:
                break
        else:
            raise SystemExit(f"{len(missing)} damaged lines after {tries} tries")
    finally:
        s.close()

    data = b"".join(chunks[o] for o in range(0, total, 54))
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        row = data[y * stride:(y + 1) * stride]
        for x in range(w):
            v = row[2 * x] | (row[2 * x + 1] << 8)
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            px[x, y] = (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)
    return img


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("out")
    ap.add_argument("--scale", type=int, default=1, help="enlarge by this factor (sharp pixels)")
    ap.add_argument("--boot", action="store_true", help="restart the device and take the start screen")
    a = ap.parse_args()
    img = grab(a.port, boot=a.boot)
    if a.scale > 1:
        img = img.resize((img.width * a.scale, img.height * a.scale), Image.NEAREST)
    img.save(a.out)
    print(f"{a.out}: {img.width}x{img.height}", file=sys.stderr)


if __name__ == "__main__":
    main()
