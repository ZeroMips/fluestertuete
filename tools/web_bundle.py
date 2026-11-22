#!/usr/bin/env python3
"""Copies built firmware into the setup website: web/firmware/<board>/ with a manifest.json the page reads.

  tools/web_bundle.py                       # tdeck from build/, core2 from build-core2/ (those that exist)
  tools/web_bundle.py tdeck=build core2=build-core2

The page flashes the files at the offsets the build chose (flasher_args.json) and then the settings block."""
import gzip
import io
import json
import os
import shutil
import sys
import tarfile

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(PROJECT, "web", "firmware")
NAMES = {"tdeck": "LilyGo T-Deck", "core2": "M5Stack Core2"}


def bundle(board, build):
    build = os.path.join(PROJECT, build)
    args = json.load(open(os.path.join(build, "flasher_args.json")))
    desc = json.load(open(os.path.join(build, "project_description.json")))
    dest = os.path.join(OUT, board)
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest)
    parts = []
    for offset, rel in sorted(args["flash_files"].items(), key=lambda kv: int(kv[0], 16)):
        name = os.path.basename(rel)
        shutil.copy(os.path.join(build, rel), os.path.join(dest, name))
        parts.append({"file": name, "offset": int(offset, 16)})
    manifest = {"board": board, "name": NAMES.get(board, board), "chip": args["extra_esptool_args"]["chip"],
                "version": desc.get("project_version", ""), "flash_size": args["flash_settings"]["flash_size"],
                "parts": parts}
    json.dump(manifest, open(os.path.join(dest, "manifest.json"), "w"), indent=1)
    print(f"{board}: {manifest['chip']} {manifest['version']}, {len(parts)} files")
    return manifest


def bundle_ripstation():
    """ripstation/ as web/ripstation/ripstation.tar.gz: the setup page puts it on the station's SD card.
    Reproducible: sorted names, no times or owners, so an unchanged station gives the same bytes."""
    src = os.path.join(PROJECT, "ripstation")
    files = []
    for dirpath, dirnames, filenames in os.walk(src):
        dirnames[:] = sorted(d for d in dirnames if d not in ("tests", "__pycache__"))
        files += [os.path.join(dirpath, f) for f in sorted(filenames)]
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for path in files:
            info = tar.gettarinfo(path, arcname=os.path.relpath(path, src))
            info.mtime, info.uid, info.gid, info.uname, info.gname = 0, 0, 0, "root", "root"
            info.mode = 0o755 if os.access(path, os.X_OK) else 0o644
            with open(path, "rb") as f:
                tar.addfile(info, f)
    dest = os.path.join(PROJECT, "web", "ripstation")
    os.makedirs(dest, exist_ok=True)
    with open(os.path.join(dest, "ripstation.tar.gz"), "wb") as out:
        with gzip.GzipFile(fileobj=out, mode="wb", mtime=0, filename="") as gz:
            gz.write(raw.getvalue())
    print(f"ripstation: {len(files)} files, {os.path.getsize(os.path.join(dest, 'ripstation.tar.gz'))} bytes")


def main():
    pairs = [a.split("=", 1) for a in sys.argv[1:]] or [("tdeck", "build"), ("core2", "build-core2")]
    boards = []
    for board, build in pairs:
        if os.path.exists(os.path.join(PROJECT, build, "flasher_args.json")):
            boards.append(bundle(board, build))
        else:
            print(f"{board}: no build in {build}, skipped")
    json.dump([{k: m[k] for k in ("board", "name", "chip", "version")} for m in boards],
              open(os.path.join(OUT, "index.json"), "w"), indent=1)
    bundle_ripstation()


if __name__ == "__main__":
    main()
