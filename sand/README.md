# Sand

A metal plate covered in sand, bowed or struck. Each note sets the plate
ringing in one of its Chladni figures. At first the sand lies all over the
plate, on ground that moves, so it is thrown up and lands with a crackling
patter on top of the tone. Within a second or two the grains drift to the
figure's nodal lines, where the plate is still, and stop there: the patter
dies away and a glassy, gently beating tone is left. A new figure finds the
sand on moving ground again, so it crackles and then settles into the new
figure; a steep plate keeps it running downhill.

The sand also weighs on the plate where it lies. Heavy sand on the moving
ground flattens and damps the plate a little, so the tone clears as the sand
leaves it. The rattle is made by the sand itself: it is every grain's
landings, each a click and a knock on the plate's own modes, so it is only as
dense as the sand still on moving ground.

Sand starts from Ernst Chladni's sound figures. In *Entdeckungen über die
Theorie des Klanges* (1787) he showed that a plate clamped at its centre,
strewn with sand and bowed at its edge, throws the sand off the parts that
move and gathers it on the still nodal lines, a different figure for each
note it rings. Robert Hooke had seen flour do the same on a bowed glass plate
in 1680; Chladni made the figures a method and published them.

**Status: experimental.** Sand passes the SDK's tests but has not yet been
played on an OP-XY. Its CPU figures (below) are the SDK's host estimate and a
count from the compiled code, not measurements on the device.

## Controls

Press any encoder to switch pages. Page 1 holds what is played most; page 2
sits under it, one alternate per encoder.

| Encoder | Page 1 | Page 2 |
| --- | --- | --- |
| 1 | figure | plate |
| 2 | bow | settle |
| 3 | sand | tilt |
| 4 | grain | shake |

| Control | Default | Caption | What it does |
| --- | --- | --- | --- |
| figure | 2:3+ | the mode pair, e.g. `2:3+` | The Chladni figure the plate rings in, one of twelve from a single diagonal (1:2+) to a flower (4:6−). The note sounds at its pitch, and six of the plate's higher modes ring above it, each at least 10% above the last: low figures sound like a bell, high ones like a dense gong. |
| bow | 60 | amount | From a strike to a full bow. At 0 a felt mallet strikes the plate on each note and it rings like a bell, decaying at its material's pace. As bow rises the bow takes over: it sustains the tone, damps the strike, adds the note's 2nd to 4th harmonics (−6, −10 and −15 dB at 100) and throws the sand harder, into thinner lines. |
| sand | 29 | grains, e.g. `29` | How much sand is poured, 0 to 48 grains. At 0 the plate is bare. |
| grain | sand | `dust`, `sand`, `grit`, `stone` | The grains' size, and the sound of a landing: from a soft tick of dust to the ringing knock of a stone. Heavier grains knock the plate harder and weigh more on it. |
| plate | metal | `metal`, `glass`, `wood` | What the plate is made of: how long it rings, how its upper modes fade, how fast it beats and how many harmonics the bow draws from it. |
| settle | 0.35 | friction, e.g. `0.35` | How sticky the sand is. Slippery sand bounces far and wanders before it finds the narrow quiet lines; sticky sand needs more to lift it, lands dead and settles fast. |
| tilt | 0deg | degrees, e.g. `5deg` | Tips the plate on its column, its far edge down, up to 10 degrees. Hops slide downhill, a steep plate pulls even resting sand down, and the sand piles against the low rim, where the plate moves, so it keeps trickling. |
| shake | 0.0g | the largest jolt, e.g. `0.4g` | Noise into every mode of the plate and random jolts to every grain, up to 1.5 times gravity. The figure blurs and the sand never settles. |

Measured on a held A3, each control at 0, 25, 50, 75 and 100 with the others
at their defaults (the median of five seeds). Level is the tone's RMS from
2.5 to 4.5 s; the rattle is the output above 2.5 kHz, at its peak in the
first half second and from 3 to 4.5 s; "settles" is when the rattle stays
20 dB below its start.

| Control | 0 | 25 | 50 | 75 | 100 |
| --- | --- | --- | --- | --- | --- |
| figure | 1:2+, overtones to 3.8×, settles 2.2 s | 1:4+, to 2.4×, 1.0 s | 2:5+, to 2.0×, 0.9 s | 1:6−, to 2.0×, 0.7 s | 4:6−, to 1.9×, 0.9 s |
| bow | struck: −57 dB by 2.5 s (it rings down), rattle −37 dB | −30 dB, rattle −43 dB | −27 dB, −41 dB | −25 dB, −40 dB | −23 dB, −41 dB |
| sand | 0 grains: no rattle (−78 dB) | 12: −44 dB, settles 1.5 s | 24: −42 dB, 1.3 s | 36: −39 dB, 1.5 s | 48: −38 dB, 1.6 s |
| grain | dust: clicks at 5.5 kHz | sand: 3.3 kHz | grit: 2.0 kHz | stone: 1.2 kHz | stone: 0.7 kHz, ringing (Q 4) |
| plate | metal: rings 6 s | metal | glass: 3 s, purest | wood: 0.6 s, roughest | wood |
| settle | μ 0.10: settles 2.8 s | 0.23: 2.1 s | 0.35: 2.0 s | 0.48: 0.8 s | 0.60: 0.9 s |
| tilt | 0 deg: rattle after 3 s −77 dB | 3 deg: −54 dB | 5 deg: −76 dB | 8 deg: −50 dB | 10 deg: −41 dB (a steady trickle) |
| shake | 0.0 g: −77 dB after 3 s | 0.1 g: −74 dB | 0.4 g: −48 dB | 0.8 g: −40 dB, +2 dB level | 1.5 g: −37 dB, +4.5 dB level |

Tilt and shake colour the sound, so they default to off. A little tilt can
leave a few grains rattling against the rim (25 above), a little more often
pours them clear of it (50); from about 75 the plate trickles steadily.
Every grain is a little different (each needs 0.7 to 1.3 times the plate's
lift to leave it, as in real sand), so the grains settle one by one and the
rattle fades rather than stopping.

**Playing it.** Sand's notes share one plate's sand, across tracks too. A
note started while another is still sounding takes over its sand, so a bowed
legato line or a chord added to a held note keeps its settled figure and
stays clean; a note with nothing sounding pours fresh sand. A strike throws
even settled sand, so struck notes (bow near 0) crackle every time. The plate
is mono, as every Open-XY synth voice is; for width, send it to the OP-XY's
chorus or reverb.

## Presets

| Preset | Role | Sound |
| --- | --- | --- |
| Chladni | the defaults | A bowed metal plate in 2:3+: a crackle on each new phrase that settles over a second and a half into a clear, beating tone. |
| Glass Pad | pad | A glass plate in 1:3+ with a little dust: chords bloom with a soft patter and settle almost at once into pure glass. |
| Bowed Metal | lead, bass | A hard-bowed metal plate in 3:4+: a bright, harmonic tone with a grainy onset; it holds a bass line as well as a melody. |
| Bell Keys | keys | Struck metal in 1:2+, ringing like a bell, with a sprinkle of dust thrown by each strike. |
| Sand Shaker | percussion | A struck wooden box of grit: each note is a knock and a burst of rattle that settles quickly, like a shaker. |
| Drone | drone | A heavily bowed metal plate in 4:6−, lightly shaken, so a little dust keeps hissing over a dense, slowly beating chord. |
| Hourglass | texture | Fine sand on a tilted plate in 3:5+: it runs downhill and keeps trickling at the low edge. |
| Gravel | characterful | Stones on metal in 1:4+: a long, knocking rattle, and the note clears as the stones find the lines. |

## How it works

**The plate.** The plate is a square with free edges, modelled as Chladni's
figures usually are: mode (m, n, ±) has the shape
cos(mπx) cos(nπy) ± cos(nπx) cos(mπy) and a frequency proportional to
m² + n², the + and − pair split a little. Its modes are damped resonators in
phasor form, as in Hexagon's Hollow. Once a block, each mode's complex
amplitude decays by the plate's ring time, moves toward the bow's level (as
fast as it is bowed), and is pushed by a strike, shake, the bow's rosin and
any grain that lands. A sine table then sounds it, ramped across the block,
the upper modes two at a time. The bowed mode has a near-degenerate twin a
fraction of a hertz above it (0.4 to 1.1 Hz on metal, by figure), as a real
plate's paired modes split, so the tone beats gently. Over a block the twin
barely turns against it, so it is sounded as part of the bowed mode, a slowly
turning phasor added to its amplitude; being the weaker, it leaves the pair's
frequency at the note. The bow's stick and slip add the note's 2nd to 4th
harmonics, one waveform read at the bowed mode's phase.

**The sand.** Twelve grains are looked at each block, so each of the 48 is
looked at every 5.8 ms. A grain on the plate feels the plate's acceleration
where it sits: each mode's amplitude times its frequency times its shape
there, plus a steep plate's pull downhill. Where that beats the grain's
weight by more than static friction allows, it is thrown: higher, longer and
further the more the plate beats it. It lands at a random spot nearby, but a
spot where the figure's mode shakes harder is often refused (more often with
more settle), so the sand drifts to the nodal lines and stays; a hop onto the
clamp lands against its washer. The plate's upper modes and the bow's
harmonics throw sand at a quarter of their strength: in a real bowed plate
they are much quieter than the bowed mode, and at full strength they would
keep the sand from ever forming a figure.

**The rattle.** Each landing is an impulse at a random point in the block,
as loud as the grain fell, through one two-pole band-pass set by grain. A
landing also knocks every mode of the plate by its shape where it lands, so
the rattle rings the plate's own partials. Once the rattle has rung out, the
output skips it.

**The weight.** Every round of looks, the sand on the plate (not in the air)
is weighed against each mode's shape. That flattens the mode (by up to 2%),
adds to its loss and, for the bowed mode, quietens it.

**The shared sand.** `Shared` holds one copy of the plate's sand and the
note it belongs to, refreshed every fourth round of looks. A note that starts
while that note still renders copies its sand; `destroy` lets it go.

## Level

Sand's levels match the Hexagon Suite's synths. A held middle C at the
defaults sits within 2 dB of Hollow's Choir (−26.5 dBFS RMS), and an
eight-note chord peaks between 0.50 and 0.75 of full scale on every preset.
Each voice's rattle is held within a tenth of full scale, so stones landing
together on many notes keep that headroom. Pitch is within 2 cents of the
note from C2 to C6 on every preset but Sand Shaker (struck, heavily loaded
wood, up to 5 cents flat at C2).

## Screen

![Sand's screen, an engraved Chladni plate on its stand: sand dances on the plate as two notes are bowed, gathers on the nodal lines and dims; the figure changes and the sand dances again before forming a cross; then the plate turns to glass and tips on its column, the sand running to its low edge](docs/sand.gif)

The apparatus, as an engraving of Chladni's *Entdeckungen* would draw it,
seen from the front and a little to the left: the most recent note's plate.

- **The plate** is held at its centre by the stand's knurled clamp nut, as
  Chladni held his, so every figure, and every mode that rings with it, is
  still at the centre, and no sand lies on the clamp. Its top carries the
  figure's nodal lines.
- **The stand** is a turned column on a round base with three levelling
  screws. **Tilt** tips the whole plate on the column, up to 10 degrees, and
  the sand runs downhill across it.
- **The sand** is up to 48 grains, as many as the sound throws, by the
  same physics. Grains in the air are white, larger and drawn above the
  plate; moving grains are bright; settled grains dim and stop. A grain
  settled on the figure's line lies on a drift of sand along it. **Sand**
  sets how much: none leaves the plate bare with the lines only scribed, a
  little lies in short drifts, and plenty draws the figure in long, heaped
  ones. **Grain** sets how big, from dust in long fine drifts to stones
  lying as themselves.
- **The bow** lies across the plate's right edge where the figure is bowed.
  More **bow** engages more of it, lying further over the plate; while notes
  are held it brightens and draws slowly across. Below 15 it lifts clear of
  the edge, and below 50 a felt mallet comes in, striking the plate on each
  note and rising to hover.
- **Plate** changes the plate itself: brass with a ruled border, thin glass
  with a bright edge and glints, or a thick wooden board showing its grain.
  **Settle** changes its finish: polished below 35, with a sheen across it;
  matte above 65, with a rosined tooth. **Shake** draws the whole plate
  twice more, aside by 3 to 11 pixels, as an engraver draws a thing that
  trembles; while it sounds, the jolts throw its sand into the air and break
  up the drifts.

*Shown: Chladni, a note gathering its sand into drifts; a second note as
Sand rises to full, heaping the figure; then a note with Shake turned up.*

![Each control at 0, 50 and 100, the others at their defaults](docs/controls.png)

The same sheet at the screen's own size, 480 by 222, is
[docs/controls-1x.png](docs/controls-1x.png). The drawing is made for that
size: no line is finer than about a pixel, no grain is smaller than 2.6
pixels across, the metal plate's ruled border sits 8 pixels in from its edge,
and a sticky plate's tooth is a zigzag a few pixels tall rather than a fine
hatch.

The screen cannot read the sound, so it runs its own 48 grains through the
same functions (`sand.h`), with the bowed mode swelling at the bow's pace
while notes are held and a strike ringing down. Its grains are not the
sound's grains, but they move as they do.

A settled plate redraws nothing. Only part of the sand is redrawn each
frame, moving grains on alternate frames and settled ones every sixth, so
the sand follows a tipping plate within a fifth of a second and never
redraws all at once. Of the screen's 136 nodes, 4 to 6 change a frame on
average at the default Sand (at 60 and 30 frames a second), 8 to 10 at full
Sand and 11 to 14 at full Shake with a note held; at most 30 when a note
pours fresh sand, and up to 40 on the frames a turning tilt moves the plate
by half a degree.

## Files

- `engine.cpp`, `core.h`: the voice: the plate's modes, the bow, the strike,
  the sand's looks, the rattle and the shared sand.
- `sand.h`: the physics the sound and the screen share: the mode shapes,
  the materials, and how a grain is thrown, flies and lands.
- `plate.h`, `sine.h`: the figures, mode shapes, nodal outlines, click
  filters and the bow's harmonic waveform; the sine table.
- `display.cpp`, `display.h`, `view.h`: the screen, and the view it is
  drawn in with its node numbers.
- `panel.svg.fragment`: the screen's artwork.
- `tools/make_plate.py`: writes `plate.h`, `view.h`, `panel.svg.fragment`
  and the Figure labels and node count in `definition.json`. Run it after
  changing a figure or a shape, and commit all four;
  `python tools/make_plate.py --sheet <file>.svg` also draws every figure's
  outlines for checking by eye. Pure Python, no packages.
- `tests/sound.cpp`: the sound checks, run by `sdk.py test`: that the rattle
  dies as the sand settles and returns with a new figure or a fresh note,
  that a legato note keeps the settled sand and a struck one throws it, that
  settle, shake and sand do what they say, the pitch, level and headroom
  above, and that every corner of the controls stays bounded.

## CPU

Not yet measured on an OP-XY. The SDK's host estimate (`sdk.py test`) is
about 7,400 to 10,300 Blackfin instructions per voice block at the heaviest
setting (the best of several runs; the host's timing varies with its load).
Counted from the compiled code, the sample loops alone take about 10,000
instructions a block (three pairs of plate modes at 42 instructions a
sample, the bowed mode at 30), and about 13,400 with the rattle's output;
with the sand's looks and the modes' block maths, a block of heavy rattle
comes to around 16,000, and a settled one to around 12,500. Compiled for the
device, Sand takes 66.6 KiB of the shared module memory.
