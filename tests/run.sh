#!/bin/bash
# Builds the host tests against the decoder fetched by the firmware build, and compares with ffmpeg.
set -e
cd "$(dirname "$0")/.."
H=managed_components/chmorgan__esp-libhelix-mp3/libhelix-mp3
B=tests/build; mkdir -p $B
rm -rf $B/helix && mkdir -p $B/helix && cp $H/*.c $H/pub/* $H/real/*.c $H/real/*.h $B/helix/ && cp tests/shim/assembly.h $B/helix/
CJSON=${IDF_PATH:-$HOME/esp/esp-idf}/components/json/cJSON
gcc -O1 -g -fsanitize=address -w -DUSE_DEFAULT_STDLIB -Imain -I$B/helix -I$CJSON \
    tests/host_test.c main/util.c main/ring.c main/mp3s.c main/search.c main/config.c main/davxml.c $CJSON/cJSON.c \
    $B/helix/*.c -lm -lz -o $B/host_test

# Settings block: website (JS) and tools/make_config.py (Python) write the same bytes.
python3 - <<'P'
import json, importlib.util
spec = importlib.util.spec_from_file_location("mc", "tools/make_config.py")
mc = importlib.util.module_from_spec(spec); spec.loader.exec_module(mc)
v = json.load(open("tests/config_vector.json", encoding="utf-8"))
assert mc.pack(v["settings"]).hex() == v["hex"], "make_config.py no longer matches tests/config_vector.json"
print("config vector ok (python)")
P
if command -v node >/dev/null; then node tests/config_vector.mjs; fi

# Search over a real library listing, when one is given: LIBRARY_LISTING=files.jsonl LIBRARY_PREFIX=/volume1/music
# LIBRARY_ROOT="/Hörspiele für Kinder" (JSON lines with "path" and "size"; see tests/make_index.py).
FILES=${LIBRARY_LISTING:-}
if [ -n "$FILES" ] && [ -f "$FILES" ]; then
    python3 -I tests/make_index.py "$FILES" "$LIBRARY_PREFIX" "$LIBRARY_ROOT" > $B/index.txt
    $B/host_test $B/index.txt query hundebabis kokosnus "bibi hunde" feuerwehr "pu baer" sams "sams cd2" eiskonigin \
        "ritter rost" pettersson lillifee pferd weihnachten "michel" kastner lilifee petterson "hnude" bibi
fi
$B/host_test
$B/host_test

# 12 s of speech-like tone sweep plus noise, in the formats the library holds.
ffmpeg -v error -y -f lavfi -i "sine=f=330:d=12,aeval=val(0)*0.5+0.2*random(0)*sin(t*9)" -ac 2 -ar 44100 $B/src.wav
ffmpeg -v error -y -f lavfi -i "color=c=red:s=640x640:d=0.1" -frames:v 1 $B/art.png
ffmpeg -v error -y -i $B/src.wav -b:a 128k $B/cbr.mp3
ffmpeg -v error -y -i $B/src.wav -q:a 4 $B/vbr.mp3
ffmpeg -v error -y -i $B/src.wav -ac 1 -ar 22050 -b:a 64k $B/mono22.mp3
ffmpeg -v error -y -i $B/src.wav -i $B/art.png -map 0 -map 1 -c:v copy -b:a 192k -id3v2_version 3 -metadata title=Test $B/art.mp3
head -c 3000 /dev/urandom > $B/junk.bin
cat $B/junk.bin $B/cbr.mp3 > $B/junk.mp3

for f in cbr vbr mono22 art junk; do
    ch=2; [ $f = mono22 ] && ch=1
    $B/host_test $B/$f.mp3 0 $B/$f.pcm | head -1
    ffmpeg -v error -y -i $B/$f.mp3 -f s16le -acodec pcm_s16le -ac $ch $B/$f.ref.pcm
    python3 tests/compare.py $B/$f.pcm $B/$f.ref.pcm $ch
done

# A seek lands inside a frame, without the bit reservoir: it has to recover and stay aligned.
size=$(stat -c %s $B/cbr.mp3); off=$((size / 2 + 137))
$B/host_test $B/cbr.mp3 $off $B/seek.pcm | head -1
python3 - "$B/seek.pcm" "$B/cbr.ref.pcm" <<'P'
import sys,os
a,b=(os.path.getsize(p) for p in sys.argv[1:3])
r=a/b; print(f"seek decoded {r:.3f} of the file"); sys.exit(0 if 0.45<r<0.55 else 1)
P
# Real recordings hold false sync words the synthetic files lack. Pass any MP3s as arguments.
for f in "$@"; do $B/host_test "$f" sweep 300 | head -1; done
echo ALL OK
