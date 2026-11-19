#!/usr/bin/env python3
"""Index text, as the device builds it, from a file listing (JSON lines with "path" and "size"):
  make_index.py files.jsonl PREFIX ROOT
PREFIX is cut from every path (what the device does not see), ROOT is the library folder below it, e.g.
  make_index.py files.jsonl /volume1/music /Stories
"""
import json, os, sys, collections

PREFIX = sys.argv[2]
ROOT = PREFIX + sys.argv[3]
files = [json.loads(l) for l in open(sys.argv[1], encoding="utf-8")]
files = [f for f in files if f["path"].startswith(ROOT + "/")]
audio = collections.defaultdict(list)
dirs = set()
for f in files:
    d = os.path.dirname(f["path"])
    if f["path"].lower().endswith(".mp3"):
        audio[d].append(f)
    while d != ROOT:
        dirs.add(d)
        d = os.path.dirname(d)
out = []
for d in sorted(dirs):
    out.append(("A" if audio[d] else "S") + d[len(PREFIX):])
    out += ["T%s\t%d" % (f["path"][len(PREFIX):], f["size"]) for f in sorted(audio[d], key=lambda f: f["path"])]
sys.stdout.write("\n".join(out) + "\n")
