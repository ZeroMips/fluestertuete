#!/usr/bin/env python3
"""Compare decoder output against a reference: python3 compare.py ours.pcm ref.pcm channels [search_samples]"""
import array, math, sys

ours, ref = array.array("h"), array.array("h")
ours.frombytes(open(sys.argv[1], "rb").read())
ref.frombytes(open(sys.argv[2], "rb").read())
search = int(sys.argv[4]) if len(sys.argv) > 4 else 8000
n = 40000
start = min(len(ours) // 2, 60000)
a = ours[start:start + n]
best = (1e18, 0)
for lag in range(-search, search + 1):
    b0 = start + lag
    if b0 < 0 or b0 + len(a) > len(ref):
        continue
    e = sum((a[i] - ref[b0 + i]) ** 2 for i in range(0, len(a), 97))
    if e < best[0]:
        best = (e, lag)
lag = best[1]
b = ref[start + lag:start + lag + len(a)]
sig = sum(x * x for x in b) or 1
noise = sum((x - y) ** 2 for x, y in zip(a, b)) or 1
snr = 10 * math.log10(sig / noise)
print(f"samples ours={len(ours)} ref={len(ref)} lag={lag} snr={snr:.1f} dB")
sys.exit(0 if snr > 40 else 1)
