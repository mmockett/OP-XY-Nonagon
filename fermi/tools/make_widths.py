#!/usr/bin/env python3
"""Writes tests/widths.h: the advance widths of printable ASCII in the OP-XY's
header face, Univers Next Pro Light, so tests/screen.cpp can check that every
caption fits its column. Only the widths are written; the font itself stays
where it is (it is stock content, never copied into the checkout).

    python tools/make_widths.py --font <UniversNextPro-Light.ttf>   (or set XY_FONT)"""
import argparse
import os
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[1]


def tables_of(data):
    count = struct.unpack(">H", data[4:6])[0]
    return {data[12 + 16 * i:16 + 16 * i].decode(): struct.unpack(">II", data[20 + 16 * i:28 + 16 * i]) for i in range(count)}


def glyph_of(data, cmap, code):
    """The glyph for a character, from the Unicode BMP subtable (format 4)."""
    for i in range(struct.unpack(">H", data[cmap + 2:cmap + 4])[0]):
        platform, encoding, offset = struct.unpack(">HHI", data[cmap + 4 + 8 * i:cmap + 12 + 8 * i])
        at = cmap + offset
        if struct.unpack(">H", data[at:at + 2])[0] != 4 or (platform, encoding) not in ((3, 1), (0, 3), (0, 4)):
            continue
        segments = struct.unpack(">H", data[at + 6:at + 8])[0] // 2
        ends = struct.unpack(f">{segments}H", data[at + 14:at + 14 + 2 * segments])
        starts_at = at + 16 + 2 * segments
        starts = struct.unpack(f">{segments}H", data[starts_at:starts_at + 2 * segments])
        deltas = struct.unpack(f">{segments}h", data[starts_at + 2 * segments:starts_at + 4 * segments])
        ranges_at = starts_at + 4 * segments
        ranges = struct.unpack(f">{segments}H", data[ranges_at:ranges_at + 2 * segments])
        for s in range(segments):
            if starts[s] <= code <= ends[s]:
                if ranges[s] == 0:
                    return (code + deltas[s]) & 0xFFFF
                where = ranges_at + 2 * s + ranges[s] + 2 * (code - starts[s])
                glyph = struct.unpack(">H", data[where:where + 2])[0]
                return (glyph + deltas[s]) & 0xFFFF if glyph else 0
        return 0
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", default=os.environ.get("XY_FONT"))
    args = parser.parse_args()
    if not args.font:
        parser.error("no font: pass --font PATH or set XY_FONT to the path of UniversNextPro-Light.ttf")
    data = pathlib.Path(args.font).read_bytes()
    t = tables_of(data)
    units = struct.unpack(">H", data[t["head"][0] + 18:t["head"][0] + 20])[0]
    long_metrics = struct.unpack(">H", data[t["hhea"][0] + 34:t["hhea"][0] + 36])[0]
    hmtx = t["hmtx"][0]
    advance = lambda g: struct.unpack(">H", data[hmtx + 4 * min(g, long_metrics - 1):hmtx + 4 * min(g, long_metrics - 1) + 2])[0]
    widths = [advance(glyph_of(data, t["cmap"][0], c)) for c in range(32, 127)]
    lines = ["// Written by tools/make_widths.py: the advance widths of ' ' to '~' in",
             "// Univers Next Pro Light, the OP-XY's header face, in font units.",
             "#pragma once", "namespace widths {", f"const unsigned units = {units};", "const unsigned advance[95] = {"]
    for i in range(0, 95, 16):
        lines.append("    " + ", ".join(str(w) for w in widths[i:i + 16]) + ",")
    lines += ["};", "}", ""]
    (ROOT / "tests" / "widths.h").write_text("\n".join(lines))
    print("wrote tests/widths.h")


if __name__ == "__main__":
    main()
