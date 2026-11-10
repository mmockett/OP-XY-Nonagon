# Machine development

This repository holds the Nonagon Suite: nine machines, one per folder
(`sand`, `cocoa`, `ghost`, `coin`, `recur`, `fermi`, `mach`, `butterfly`,
`drop`). Read README.md and each machine's own README first.

All nine build against the [Open-XY SDK](https://github.com/etcetc000/open-xy)
on `staging`. Sand, Cocoa, Coin, Fermi, Mach and Butterfly are on SDK 1.12:
they play in stereo, opened on an existing control and exactly mono at its
default (left = mono + side, right = mono - side). Drop is on 1.10. Ghost and
Recur are sample engines with their own screens, on SDK 1.10 (`stockShift`,
the sample overview's `gain`, the amp envelope and Recur's own `samples`). Do not
edit the SDK to make a machine work; propose an SDK change separately.

Follow the SDK's [machine API](https://github.com/etcetc000/open-xy/blob/staging/docs/api.md)
and [UX conventions](https://github.com/etcetc000/open-xy/blob/staging/docs/ux.md).
Run `python ../open-xy/sdk.py test --machine <folder>` and
`--firmware` for every machine you change, and keep generated outputs, firmware
and stock content outside the repository.

## Screens: check them the way the OP-XY draws them

The browser preview and enlarged contact sheets do not show what the device
does. Screens that looked right there have broken on the OP-XY: captions ran
into the next column, a wrong outline drew a full-height streak, a spoon's bowl
came apart from its handle, and a hint's words ran together. Before calling a
screen done:

- **Judge at 1:1.** The screen is 480 × 222 px. Look at frames at their real
  size (each machine's `docs/controls-1x.png`), not enlarged. No detail under
  about 3–4 px, no lines or hatching closer than about 3 px, no 1 px dots, no
  text under about 8 px, and no difference shown only by a slightly dimmer
  grey.
- **Measure text in the device's font at its real size.** The OP-XY draws text
  in Univers Next Pro Light; character counts mean nothing. Header values are
  20 px from x `65 + 120 i` and must fit in 53 px; titles are 10 px from
  `5 + 120 i` and must fit in 56 px (SDK `docs/ux.md`). Measure every caption
  a machine can show (every control swept finely, every enum label, every
  title) with `tools/caption_width.py` (set `XY_FONT` to the font's file), and
  make a too-wide caption fail the machine's tests. Lay out any other text,
  such as a "load a sample" hint, from measured widths with clear gaps (about
  8 px or more) between its parts.
- **Follow the device's rules for reshaping outlines.** On the OP-XY an
  outline draws a long streak or sliver when it has no area (a line traced out
  and back over itself, or a turn straight back), when neighbouring points are
  within an eighth of a pixel, or when its closing edge is under a quarter of a
  pixel. Draw lines as thin closed ribbons and start each outline where both
  its edges are longest. Each machine's tests check every outline over a sweep
  of its controls and a note's animation; keep that check passing.
- **Keep joined parts joined.** Build an object whose parts must meet (a
  spoon's bowl and handle, a disk's face and rim) as one outline, or as nodes
  sharing one pivot and transform, so the device cannot place them apart.
- **Keep the device fast.** On average no more than about 20 nodes should
  change a frame, with a low peak, at every control setting (the heaviest
  included). Nothing that moves is filled; an idle screen changes nothing.
  The device only fills a node whose artwork has a fill, and its SVG loader
  takes one subpath of at most 64 points per path.

Ghost and Recur draw their samples with `sample_view.h` (and its artwork,
`tools/sample_view.py`), shared byte for byte with each other and with
Grains, as `amp_curve.h` is. Change every copy together, and run
`python3 tools/check_shared.py` (it also checks `../opxy-grains-release` when
that is beside this repository).

When a change reaches the device, ask for a photo of the screen, and check
each machine at its heaviest settings before calling it finished.

## The amp envelope on the synths' screens

The seven synths' screens follow the track's stock amp envelope (SDK 1.10's
`input.amp`) through `tools/amp.h`, which reads stock's times and shapes from
`tools/amp_curve.h`. A machine builds from its own folder, so each synth
holds byte-identical copies of both: edit them in `tools/`, copy them to every
synth, and check with
`for m in sand cocoa coin fermi mach butterfly drop; do cmp tools/amp.h $m/amp.h; cmp tools/amp_curve.h $m/amp_curve.h; done`.
Its functions are `static`, so the copies never clash in one firmware.
The envelope scales or times each screen's own note-driven motion; it adds
no decoration. With the envelope unknown a screen draws exactly as before,
which each synth's `tests/envelope.cpp` checks against a checksum of its
frames; a host test that zeroes its `Input` gets a known envelope of zeros,
so set `input.amp` to `envelope_unavailable` where a test means the old
timing. The curves are stock OS 1.2.1's: a larger release word is a SHORTER
release. Keep both headers to 32-bit multiplies and shifts by constants: the
macOS firmware build has no arithmetic runtime for 64-bit ones.
