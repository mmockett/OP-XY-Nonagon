# Samples

## break.wav

Made for Recur. It is four bars of a funk break at 120 BPM, played on
[MD KIT](https://github.com/mmockett/OP-XY-MNMD), a drum machine from
OP-XY-MNMD (also Max Mockett's project, MIT): MD KIT's engine renders every
hit on the host (`machines/mdkit/engine.cpp`, OP-XY-MNMD b43e907). The drum
part is written out by hand in the pattern of the well-known "Amen, Brother"
groove; no record or third-party sound was sampled. A small room, a
compressor and some drive are applied in the same render.

`tools/break/make_break.py --mnmd <OP-XY-MNMD> --sdk <open-xy>` renders it,
through `tools/break/hits.cpp`. It is exactly 8 seconds of 16-bit mono at
44.1 kHz, peaking at -4 dBFS, with its tail wrapped round so the loop has no
seam.

Licence: MIT, as the rest of this repository (see [LICENSE](../../LICENSE)).
