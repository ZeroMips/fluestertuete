#!/usr/bin/env python3
"""Copies built firmware into the setup website: web/firmware/<board>/ with a manifest.json the page reads.

  tools/web_bundle.py                       # tdeck from build/, core2 from build-core2/ (those that exist)
  tools/web_bundle.py tdeck=build core2=build-core2

The page flashes the files at the offsets the build chose (flasher_args.json) and then the settings block."""
import json
import os
import shutil
import sys

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


if __name__ == "__main__":
    main()
