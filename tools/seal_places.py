#!/usr/bin/env python3
"""Seal the hidden places into missingno/src/core/mn_places.h.

Reads tools/SPOILERS_places.txt (plain text -- a spoiler) and writes a table in
which every entry is encrypted with a key derived from the cell of the earth it
belongs to. The firmware can only produce that key by standing in the cell.

This must agree byte for byte with missingno/src/core/mn_seal.cpp.
Prints counts only, never the texts.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "SPOILERS_places.txt")
OUT = os.path.join(HERE, "..", "missingno", "src", "core", "mn_places.h")

M64 = (1 << 64) - 1
K_TAG = b"missingno.tag.01"
K_KA = b"the.only.way.out"
K_KB = b"is.out.//.scraps"


def rotl(x, b):
    return ((x << b) | (x >> (64 - b))) & M64


def siphash24(key, msg):
    k0 = int.from_bytes(key[:8], "little")
    k1 = int.from_bytes(key[8:], "little")
    v0 = 0x736F6D6570736575 ^ k0
    v1 = 0x646F72616E646F6D ^ k1
    v2 = 0x6C7967656E657261 ^ k0
    v3 = 0x7465646279746573 ^ k1

    def rnd():
        nonlocal v0, v1, v2, v3
        v0 = (v0 + v1) & M64; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32)
        v2 = (v2 + v3) & M64; v3 = rotl(v3, 16); v3 ^= v2
        v0 = (v0 + v3) & M64; v3 = rotl(v3, 21); v3 ^= v0
        v2 = (v2 + v1) & M64; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32)

    n = len(msg)
    full = n - (n % 8)
    for i in range(0, full, 8):
        m = int.from_bytes(msg[i:i + 8], "little")
        v3 ^= m; rnd(); rnd(); v0 ^= m
    b = (n << 56) & M64
    for i, c in enumerate(msg[full:]):
        b |= c << (8 * i)
    v3 ^= b; rnd(); rnd(); v0 ^= b
    v2 ^= 0xFF
    rnd(); rnd(); rnd(); rnd()
    return (v0 ^ v1 ^ v2 ^ v3) & M64


assert siphash24(bytes(range(16)), bytes(range(15))) == 0xA129CA6149BE45E5, "siphash self-test"

SCALE = [1.0, 10.0, 100.0, 1000.0]


def cell(tier, lat, lon):
    return math.floor((lat + 90.0) * SCALE[tier]), math.floor((lon + 180.0) * SCALE[tier])


def cell_msg(tier, cx, cy):
    return bytes([tier]) + (cx & 0xFFFFFFFF).to_bytes(4, "little") + (cy & 0xFFFFFFFF).to_bytes(4, "little")


def tag_of(tier, cx, cy):
    return siphash24(K_TAG, cell_msg(tier, cx, cy)) & 0xFFFFFFFF


def crypt(tier, cx, cy, data):
    m = cell_msg(tier, cx, cy)
    key = siphash24(K_KA, m).to_bytes(8, "little") + siphash24(K_KB, m).to_bytes(8, "little")
    out = bytearray(data)
    for off in range(0, len(out), 8):
        ks = siphash24(key, b"k" + (off // 8).to_bytes(4, "little")).to_bytes(8, "little")
        for i in range(8):
            if off + i < len(out):
                out[off + i] ^= ks[i]
    return bytes(out)


def entry(tier, cx, cy, level, kind, text):
    t = text.encode("ascii")
    if len(t) > 78:
        raise ValueError("too long")
    if any(c < 0x20 or c > 0x7E for c in t):
        raise ValueError("not printable ascii")
    s = 0x5A
    for c in t:
        s = (s * 31 + c) & 0xFF
    s = (s + kind) & 0xFF
    plain = bytes([kind, s]) + t
    return tag_of(tier, cx, cy).to_bytes(4, "little") + bytes([(level << 4) | tier, len(plain)]) + crypt(tier, cx, cy, plain)


def main():
    entries = []
    places = days = 0
    seen = {}
    with open(SRC, encoding="utf-8") as f:
        for ln, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            try:
                if line.startswith("@"):
                    date, text = [p.strip() for p in line[1:].split("|", 1)]
                    mo, dy = [int(x) for x in date.split("-")]
                    entries.append(entry(0x0F, mo, dy, 1, 0, text))
                    days += 1
                    continue
                parts = [p.strip() for p in line.split("|")]
                _name, lat, lon, tier, kind = parts[0], float(parts[1]), float(parts[2]), int(parts[3]), int(parts[4])
                here = parts[5]
                near = parts[6] if len(parts) > 6 else ""
                if not (0 <= tier <= 3 and 1 <= kind <= 12):
                    raise ValueError("tier/kind out of range")
                for step in range(4):
                    t = tier - step
                    if t < 0:
                        break
                    cx, cy = cell(t, lat, lon)
                    level = 4 if step == 0 else t + 1     # 1 far (~100 km), 2 near (~10 km), 3 close (~1 km)
                    key = (t, cx, cy, level)
                    if key in seen:
                        if step == 0:
                            raise ValueError("shares its finest cell with line %d" % seen[key])
                        continue                      # a coarser cell already claimed: one voice is enough
                    seen[key] = ln
                    entries.append(entry(t, cx, cy, level, kind, here if step == 0 else (near if step == 1 else "")))
                places += 1
            except Exception as e:                    # never echo the text
                sys.exit("SPOILERS_places.txt line %d: %s" % (ln, e))

    entries.sort(key=lambda e: e[:4])                 # by tag: the order says nothing
    blob = b"".join(entries)
    with open(OUT, "w", encoding="ascii") as f:
        f.write("// Generated by tools/seal_places.py -- do not edit.\n")
        f.write("// %d places and %d days, sealed. The plain text is not in here.\n" % (places, days))
        f.write("#pragma once\n#include <stdint.h>\nnamespace mn {\n")
        f.write("static const uint8_t MN_PLACES[] = {\n")
        for i in range(0, len(blob), 20):
            f.write("  " + ",".join("0x%02X" % b for b in blob[i:i + 20]) + ",\n")
        f.write("};\nstatic const unsigned MN_PLACES_LEN = %d;\n}  // namespace mn\n" % len(blob))
    print("sealed %d places, %d days: %d entries, %d bytes" % (places, days, len(entries), len(blob)))


if __name__ == "__main__":
    main()
