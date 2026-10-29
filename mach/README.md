# Mach

A flyby. Every note is an object flying past you in a straight line at a
Mach number you choose, sounding a source pitched by the key. You hear the
source at every retarded time τ that solves t = τ + |r(τ)| / c: as loud as it
was near, as dark as the air it crossed. Below Mach 1 that is a Doppler glide.
Near Mach 1 the approach piles up into a zip. Above it you hear nothing until
the Mach cone reaches you, then a boom, and then the source **forwards and
backwards at once**, the two images parting from the moment the cone left.

A source moving toward a listener is heard higher, and moving away lower: the
Doppler effect, which Christian Doppler described in 1842. A source faster
than sound outruns its own waves, which pile up into a cone trailing behind
it, the Mach cone, its half-angle asin(1/M) at Mach number M. Where the cone
sweeps over a listener it is heard as a sonic boom. In 1887 Ernst Mach and
Peter Salcher photographed the shock waves round a bullet flying faster than
sound. Mach plays the whole flyby: the glide, the cone, the boom and what is
heard inside the cone.

Tune pitches the source so the flight sounds the key where you want it:
before the pass, at it, or after it. So a supersonic bell can land its
backward ring on the key, a swell into the note.

The engine never sees a note-off. Holding a note keeps the OP-XY's amp
envelope open on the whole pass, including what arrives after the object has
gone. Releasing fades it, as sound already in the air cannot be called back.

**Status: experimental.** Mach passes the SDK's tests but has not yet been
played on an OP-XY. Its CPU figures are estimates, not measurements on the
device.

## Controls

Press any encoder to switch pages. Page 1 is the flight, page 2 what the air
and the ground do to it.

| Encoder | Page 1 | Page 2 |
| --- | --- | --- |
| 1 | mach | boom |
| 2 | pass | air |
| 3 | source | tune |
| 4 | dur | echo |

| Control | Range | Default | Caption | What it does |
| --- | --- | --- | --- | --- |
| mach | 0.3 to 3, Mach 1 at the middle | 1.67 | `M1.67` | The object's speed. Below 1 a glide down through the key, steeper near 1. Above 1 the cone: a boom, then two images, the backward one higher. At 5/3 they land two octaves apart, at 2 a twelfth, at 3 an octave. |
| pass | 3 to 300 m, exponentially | 12 m | `12m` | The closest distance. Near, the glide swoops past in a quarter of a second; far, it takes a second and more. Near is up to 6 dB louder at the pass itself, but brief. |
| source | organ, pad, voice, bell, siren | bell | the name | What flies. **Organ:** drawbars 8′ to 1′. **Pad:** a bowed spectrum on two oscillators 0.3% apart. **Voice:** three formants whose vowel opens from "oo" to "ah" and closes to "oh" over the flight, so the backward image sings it in reverse. **Bell:** a minor-third bell (hum, prime, tierce, nominal), struck as the flight begins and again as it passes. **Siren:** winds up into its note, a semitone below over the first 30% of every 0.9 s. |
| dur | 0.5 to 8 s, exponentially | 2.5 s | `2.5s` | The flight's own time: the window it sounds in, and so how long a note sounds (about twice dur; 11.6 s at 8 s). |
| boom | off, 0 to 100 | 30 | `off`, or the shock's rise: `756` | The N-wave as the cone passes. Low, a soft thump: a 250 Hz rise, three times the shock's length, its level made up. High, the full crack: a 10 kHz rise (250 Hz × 40^boom). A chord's booms share out, so eight at once sum to 2.7, not 8. |
| air | 0 to 100 | 30 | `open`, or the low-pass's corner at 100 m: `9.6k` | Distance darkens the sound: a one-pole low-pass on each image, its corner 250 Hz × 2^(10 (1 − air)) × 30 m / r. Live. |
| tune | start to pass to end | end | `-0.5s`, `pass`, `+0.5s` | Where the flight sounds the key exactly (within half a cent), in seconds of its own time from the pass, at most half a second either side. It also places the window so that part is the note's bulk: at the end a short approach and a long departure settling onto the key; at the start a long approach, so above Mach 1 the backward image's swell lands on it. |
| echo | off, then 1.5 to 63 m | off | `off`, or the ground's depth under you: `9.7m` | A reflecting ground under the listener. Its image of the flight passes at d + 2h with its own roots and its own boom, a flanged, delayed copy (the ground takes the highs: the bell's low ring, one darker table, the voice's first formant). Full strength from a third up. |

Page 1, tune and echo are fixed at note-on, as a flight is. Boom's level and
air act live.

Measured on one held C4, each control at 0, 25, 50, 75 and 100 with the
others at Init's (`tools/control_table.py`, from the audio and the voice's own
roots):

| | 0 | 25 | 50 | 75 | 100 | measured |
| --- | --- | --- | --- | --- | --- | --- |
| **mach** | `M0.30` +10 to 0 st | `M0.65` +25 to 0 st | `M1.00` +7 to 0 st | `M2.00` +2 to 0 st; back +19 to +29 | `M3.00` +1 to 0 st; back +12 to +21 | the forward image's glide against the key (and the backward image's), 1st to 99th percentile |
| **pass** | `3.0m` 0.27 s, −31 LUFS | `9.5m` 0.31 s, −27 LUFS | `30m` 0.39 s, −26 LUFS | `95m` 0.66 s, −25 LUFS | `300m` 1.32 s, −26 LUFS | how long the glide takes (its middle 80%), and the loudest 400 ms |
| **source** | organ 440 Hz | pad 396 Hz | voice 378 Hz | bell 257 Hz | siren 278 Hz | spectral centroid |
| **dur** | `0.5s` 1.1 s | `1.0s` 2.1 s | `2.0s` 4.3 s | `4.0s` 8.2 s | `8.0s` 11.6 s | how long it sounds, within 40 dB of its loudest |
| **boom** | `off` −20 dBFS, 579 Hz | `628` −10 dBFS, 88 Hz | `1.6k` −12 dBFS, 129 Hz | `4.0k` −13 dBFS, 211 Hz | `10k` −15 dBFS, 368 Hz | the first 30 ms: peak and centroid (off: the tone's) |
| **air** | `open` −8 dB | `14k` −8 dB | `2.4k` −9 dB | `424` −9 dB | `75` −14 dB | the sound's share above 500 Hz (the bell's partials lie mostly below it; the organ's centroid falls from 499 Hz to 343 at 50 and 306 at 100) |
| **tune** | `-0.5s` fwd −24, back 0 st | `-0.3s` fwd −24, back 0 st | `pass` fwd −17, back +7 st | `+0.3s` fwd 0, back +24 st | `+0.5s` fwd 0, back +24 st | the pitch each image ends on, against the key |
| **echo** | `off` none | `3.8m` +17 ms | `9.7m` +45 ms | `25m` +116 ms | `63m` +296 ms | the ground's first sound, after the direct sound's |

At Init (Mach 1.67) the forward image glides down onto the key and the
backward one rises two octaves; tune chooses which of them ends on the key.

## Presets

| Preset | Role | Sound | 8 notes, peak |
| --- | --- | --- | --- |
| Init | Supersonic bell melody | Mach 1.67 at 12 m: a soft thump, the bell struck as it passes settling onto the key, its backward ring two octaves up | 0.84 |
| Flyby Pad | Pad | Mach 0.45, 40 m, 4 s: each chord note swoops in sharp and settles onto its pitch, with a little ground echo | 0.72 |
| Reverse Swell | A swell into a note | Mach 3, tune at the start: the bell's ring heard backwards swells onto the key while the forward strike rings an octave down | 0.45 |
| Boom Drum | Boom percussion | Mach 2.5 at 6 m, half a second: a full crack and a short organ tail | 0.58 |
| Siren Lead | Lead | Mach 0.35 at 6 m: a siren winding up into each note as it passes close | 0.64 |
| Drone Pass | Drone | Mach 0.3 at 150 m, 8 s: a distant organ crossing slowly, darkened by the air, flanged by the ground | 0.46 |
| Choir Jet | Pad, chords | Mach 1.4 at 60 m with echo: a choir's vowel forwards and backwards at once, booming twice | 0.73 |
| Doppler Bass | Bass | Mach 0.8 at 3 m: a zip down into each note, in tune within 6 cents by the note's end | 0.68 |

The peaks are the raw sum of an eight-note C major chord across three
octaves, full scale 1; Hexagon's chords peak at 0.59 to 0.77. Init's and
Choir Jet's are their booms, eight of them on one sample, shared out.

Each preset also plays a phrase (`tools/render_phrases.py`: a melody, a chord
progression or a bass line), measured (`tools/measure.py`, `tools/pitch.py`)
beside Hexagon's Moth Bell, Hollow Choir, Wane Clean and Orbit Drawbars
through the same harness:

| | Mach | Hexagon |
| --- | --- | --- |
| Loudest 400 ms of a phrase | −18.8 LUFS (Drone Pass) to −26.8 (Doppler Bass); Init −26.7 | −22.6 to −24.9 |
| Pitch at a note's end | Init: long notes within 2 cents, 0.5 s notes +13; Siren within 11; Bass within 6 | within 2 cents |
| Spectral centroid of a phrase | 52 Hz (Boom Drum) to 585 Hz (Siren) | 445 to 810 Hz |
| Crest factor | 9.6 dB (Siren) to 23 dB (Reverse Swell) | 11 to 14 dB |
| DC | at most 7×10⁻⁵ | at most 3×10⁻⁴ |

Mach is more dynamic than Hexagon by design: a flyby swells, passes and
fades, and a supersonic one starts with a boom, so its crest factor is higher
and Init sits about 2 dB under Hexagon's melodies at its loudest.

## Screen

The whole screen is one object: a photograph after Ernst Mach and Peter
Salcher's 1887 schlieren images of a bullet in flight. Its round field, a
200 px circle, frames the left and right while its middle fills the screen,
mounted on a card.

![The screen through a session: a supersonic bell, the organ's rifle bullet with a full crack and the plate, a subsonic pass](docs/mach.gif)

Every control acts on something in the photograph, and
each change reads at a glance on the screen itself
([`docs/controls-1x.png`](docs/controls-1x.png): each control at 0, 50 and
100, at 1:1; [`docs/controls.png`](docs/controls.png) the same at 2x).

![Each control at 0, 50 and 100, the others at Init's, at 1:1](docs/controls-1x.png)

It is drawn for 480 x 222 pixels seen at 1:1: no lines closer than about
3 px, nothing finer than a pixel, no text under 8 px. The print's edge is one
solid 1.5 px band and its mount one line 7 px out; the card has a single
rule; six sound waves sit 10 px apart; the wake is 2.4 to 10 px wide with
16 px eddies; the heard marks are 8 px rings; and nothing is drawn fainter
than grey 5 of 15.

| Control | In the photograph |
| --- | --- |
| mach | The cone: the bow and tail shocks at asin(1/M). Below Mach 1 there is none, and the sound waves nest ahead of the projectile |
| pass | The listener, an ear trumpet on a post hung up into the field: its mouth 10 px under the line of flight at 3 m, 80 px at 300 m, on a scale of the distance's logarithm |
| source | The projectile, solid, one a 19th-century range might fire: a rifle bullet (organ), a round-nose bullet (pad), a Minié ball (voice), a musket ball (bell), a spitzer (siren). The blunter its nose, the further its bow shock stands off |
| dur | The turbulent wake: from a 30 px wisp to a 126 px trail, broadening as its eddies grow |
| boom | The shocks: a faint hairline when off, an 8 px band as a thump, a bright fine line as the crack |
| air | The sound-wave rings: the thicker the air, the dimmer they fade and the nearer they stop (it absorbs the outer waves, from 58 px out to 21: six waves to two) |
| tune | The trigger, a fine wire on two posts across the line of flight, at the point that sounds the key |
| echo | A polished plate under the listener, and the shock it reflects |

**Idle,** the photograph is still: the projectile at its closest pass, with
the waves it sent out behind it. A control change eases in over about 0.15 s.

**A note** launches the flight, fixed at its note-on as the engine fixes it,
on a scale where the whole flight spans the field and sound crosses it on the
same scale. Waves spread from where the projectile was and its wake churns.
A thin ring flashes at the listener's ear as each boom is heard. Two marks
on the line of flight show where the sound you hear now left the projectile;
after the cone they part, one forwards and one backwards. Releasing every
note eases the photograph back. Reduced motion shows the end state at once.

The screen cannot hear the engine: it derives the flight from the controls
and time through `flight.h`, which the engine shares.

**Redrawing little.** Nodes changed a frame (`tests/screen.cpp`, at 60 and
30 fps):

| Scenario | average | while a flight shows | peak |
| --- | --- | --- | --- |
| One supersonic flight | 2.5 | 4.9 | 12 |
| A session of turns and notes | 3.7 | 5.2 | 12 |
| Echo | 2.6 | 3.9 | 13 |
| Subsonic | 2.0 | 4.9 | 9 |
| The GIF's tour | 4.1 | 6.6 | 12 |

The peaks fall at a note-on. The screen uses 35 nodes, eight of them the
header, and four reshaping outlines of 64 points (the bow and tail shocks,
the reflected shock and the wake), re-keyed only when they change. Fills are
kept to the small solid parts (the projectile, the listener's post, the
trigger's posts) and the print's static edge.

## How it works

- **Roots.** Each image of the flight has up to two retarded times. The voice
  solves h(σ) = (s − σ)² − (Mσ)² − d² = 0 for each every 64 samples, by
  Newton steps in 64-bit integers made of 32-bit words (`wide.h`), predicted
  from the last two sub-steps, so one step usually settles it to within
  1 µs. The pitch moves across each sub-step to the rate at its end.
- **The fold.** Above Mach 1, both roots are born at the cone, σ* =
  −d / (M √(M² − 1)), from the fold's own law, (σ − σ*)² = fold (s − s*),
  and tracked from there. The images come in over fold / 30 (at least 40 ms
  after the boom). Every partial fades out before 0.45 of the sample rate, by
  the higher of its pitches across a sub-step, so the fold cannot click.
- **Distance, compression and air.** Distance falls as √(d / r), so far
  images stay heard. A root heard faster than its source runs is louder by
  1.25ρ / (0.25 + ρ), up to a quarter again. Air is a one-pole low-pass on
  each root.
- **Tune.** The source is pitched so the heard pitch at the landing point is
  the key, and the window is placed so that part is the note's bulk.
- **Echo.** The ground's image passes at d + 2h, at 0.8 of the level from a
  third up, with its own roots and its own boom.
- **Chords.** A chord's notes fly one path and are born at one instant, so
  each starts at a phase hashed from its key, the bell's strikes do the same,
  and the k-th boom at once plays at 1/k (`Shared`).
- **Output.** A soft knee above 0.7 of half scale, then a DC blocker at 6 Hz.

## Level

A voice peaks within half of full scale (`outputPeakQ31`), which the knee
holds; Init's phrase peaks at 0.33. The CPU's sample loops (below) hold
steps and amplitudes over a sub-step; while a partial sweeps through the top
of the band or a pitch changes fast (at the fold) they ramp sample by sample,
so nothing steps audibly.

## CPU

These are estimates: Mach has not been measured on an OP-XY.

- **`sdk.py test`** estimates about 8,400 to 8,800 Blackfin instructions a
  voice block at the heaviest setting of its sweep (it varies from run to
  run).
- **The heaviest corners**, host-timed the same way: every source with echo
  at Mach 1.67 and at Mach 3, tune at the pass (four roots), comes to 11,800
  to 13,000; the bell's, 11,800 to 12,800.
- **Counted from the compiled code** (the pinned `bfin-elf-g++ -O2`), every
  sample loop is a zero-overhead hardware loop without stack traffic, a
  function of its own: the bell 52 instructions a sample, its reflection 26;
  a wavetable 50; the pad, or a reflection, 46; the voice 61 and its
  reflection 37 (each counting a branch taken once a period). One Newton step
  is about 190, and a root's sub-step about 600 with it. The bell's echo
  corner therefore comes to about (52 + 52 + 26 + 26) × 64 = 10,000 for the
  loops, 2,400 for the four roots' solves and 1,000 for the output, about
  13,400.
- **No division, float or 64-bit helper** runs in the audio path: the
  firmware links none (`sdk.py test --firmware` compiles it), so products,
  quotients and roots are made of 32-bit words. Division by reciprocal is
  per sub-step or per block.

## Tests

`sdk.py test` passes, with 42 sound checks and 8 screen checks
(`tests/sound.cpp`, `tests/screen.cpp`).

- **Subsonic:** one root, within 50 ns of the exact retarded time; its heard
  pitch follows f / (1 − M cos θ) within 0.2% (within 0.04 Hz).
- **Tune** lands within 0.4 cents at Mach 0.5 to 3.
- **Supersonic:** silence, then the boom ahead of the tone, then two roots,
  one running backwards, both within 200 ns of the exact retarded times, at
  Mach 1.2, 2, 2.5 and 3. With the bell, the backward image's ring swells
  and the forward image's dies away.
- **The fold** does not click at Mach 1.1 to 3, or with echo: above 21 kHz it
  carries no more energy than steady playing, where a 10% step would show.
- **Every source** sounds, stays within its declared peak and ends.
- **Screen:** every frame is valid, and churn stays within budget.

## Files

| File | |
| --- | --- |
| `engine.cpp`, `core.h` | the voice: roots, sources, tuning, booms, its sample loops |
| `wide.h` | 64-bit products, quotients and roots from 32-bit words |
| `flight.h` | the controls' laws and the flight's timeline, shared with the screen |
| `display.cpp`, `display.h`, `plate.h` | the photograph |
| `tables.h` ← `tools/make_tables.py` | wavetables, sine, the bell's rings, the voice's pulse, small curves |
| `panel.svg.fragment`, `plate.h` ← `tools/make_panel.py` | the artwork |
| `tests/sound.cpp`, `tests/screen.cpp` | host checks; `screen` also prints frames (`tour` for the GIF, `still:` for the contact sheet) |
| `tools/render.cc`, `render_phrases.py`, `measure.py`, `pitch.py`, `control_table.py` | renders, their measurements and the control table |
| `tools/frame2svg.py`, `make_gif.py`, `make_contact.py` | frames to SVG, `docs/mach.gif`, and the contact sheets `docs/controls-1x.png` (1:1) and `docs/controls.png` (2x) |

Each tool's usage is at the head of its file. The renders and measurements
take a host build of `tools/render.cc`, and the GIF and contact sheets one of
`tests/screen.cpp`; both lines are in those files. The images also need
Google Chrome and ffmpeg.
