# Ghost

An apparition between the ears. Each note is sung by a pair of tuned
resonators, one for each ear. The note's harmonics are divided between the
ears, it beats slowly against itself across them, and it stands, circles and
whispers somewhere round your head.

Either ear alone hears a thin, strange tone. Together they hear the whole
note: a haunted pad, a choir from nowhere, a drone that breathes, a melody
whose fundamental is only in your head. Breath, a hummed vowel or your own
recording sets it ringing. On speakers and in mono it folds down to a plain,
full note: nothing cancels and nothing turns into a notch. Headphones show it
best.

Ghost starts from two phantoms. One is the binaural pitch that no single ear
hears: in the Huggins pitch (Cramer and Huggins, 1958) both ears get the same
noise except for a narrow band whose phase is shifted in one, and a faint
tone is heard at that band, though neither ear alone carries it. Ghost
divides a note between the ears in the same spirit, so the whole note exists
only where the ears fuse it, and its beat is a binaural beat. The other is
the stage's Pepper's ghost, devised by Henry Dircks and staged by John Henry
Pepper in London in 1862: a pane of glass leaning over the stage reflects an
actor hidden from the audience, so a transparent figure seems to stand among
the cast. Ghost's screen is that stage.

Ghost is an OP-XY sample engine with stereo output, id `ghost`. It needs the
SDK's sample-overview change, which gives sample engines their own screen and
is still in review as
[etcetc000/open-xy pull request 182](https://github.com/etcetc000/open-xy/pull/182).
It plays without a sample, on breath and hum alone. A loaded sample sets the
resonators ringing in its share of what excites them, bed's value: none at
0%, half at the 50% default, all of it at 100%, so the ghost takes on the
recording's grain, envelope and colour; without one, bed is a whisper of
Ghost's own.

**Status: experimental.** Ghost passes the SDK's tests and runs on an
OP-XY, and its CPU is measured there
(below).

## Controls

Press any encoder to switch pages. Shift + turn makes fine steps.

| Encoder | Page 1 | Page 2 |
| --- | --- | --- |
| 1 | ring | beat |
| 2 | voice | orbit |
| 3 | split | near |
| 4 | trade | bed |

| Control | Range | Default | Caption | What it does |
| --- | --- | --- | --- | --- |
| ring | 0.15 s to 12 s, exponential | 2.1 s | ring time, `0.15s` to `12.0s` | How long the resonance rings: from a breathy whisper to glass. Long rings also swell in. |
| voice | breath, oo, oh, ah, eh, ee | ah | the vowel, `air` (breath) to `ee` | What excites the resonators: breath alone at 0, then a hummed voice through two formants for each vowel, the loop brightening toward ee. |
| split | 0 to 100% | 60% | `60%` | How far the harmonics divide between the ears, even to one and odd to the other. Past 75% the fundamental goes too, so the pitch is only in your head. |
| trade | still, then a swap and back from every 24 s down to every 1.5 s (12 s at 25, 5 s at 50) | 7 s | `still`, `7.0s`, `1.5s` | How fast the halves swap ears, so the note drifts inside the skull, from a slow tide to a flicker. |
| beat | 0 to 6 Hz, curved (0.9 Hz at 25, 2.2 Hz at 50) | 0.7 Hz | `0.7Hz` | The right ear sharp by this much: a binaural beat on the fundamental, and on each harmonic its multiple. |
| orbit | front; spread to 100%; then a circle from every minute down to every 2 s (14 s at 50) | 80% spread | `front`, `80%`, `2.0s` | Its first quarter spreads the notes from the front round your head. The rest circles them, close by the right ear and far on the left. |
| near | 3 m to 10 cm, exponential | 76 cm | `3.0m`, `76cm`, `9cm` | Distance: far is quieter and much darker (4 octaves darker at 3 m); near is brighter, louder, and much louder in the facing ear, as a whisper at the ear is. |
| bed | 0 to 100%, equal power | 50% | the share: `50%` with a sample at the default, `25%` without | The loaded sample against breath and hum: none of what excites the resonators at 0, half at 50, all of it at 100. The hum gives way faster (as its share to the fourth: a quarter at an even share) and the vowel's formants thin, so the recording, not the hum, sets the note ringing, in its own colour. A little whitening (half its samples, at random, at 0.4) adds the grain that rings every harmonic of the note. With no sample, Ghost's own whisper, rising as bed squared (a quarter at the default, all of it at 100): white breath in gusts, a syllable every 60 to 250 ms, and the note comes and goes with them. |

Each control measured on a held A3, the others at the defaults (Seance)
(`tools/controls.cpp`):

| Control | 0 | 25 | 50 | 75 | 100 |
| --- | --- | --- | --- | --- | --- |
| ring: harmonic to noise | 6 dB | 8 dB | 12 dB | 13 dB | 16 dB |
| ring: rise to within 3 dB of its level (bed's gusts still) | 0 s | 0.05 s | 0.10 s | 0.15 s | 0.65 s |
| voice: spectral centroid | 1199 Hz | 590 Hz | 726 Hz | 1020 Hz | 1400 Hz |
| voice: harmonic to noise | 14 dB | 14 dB | 11 dB | 14 dB | 15 dB |
| split: each of the first 8 harmonics louder in its own ear, at least (trade still) | −2.5 dB | +3.3 dB | +5.6 dB | +16.0 dB | +27.2 dB |
| split: the fundamental against the third harmonic, in the odd ear | −16 dB | −12 dB | −10 dB | −7 dB | −19 dB |
| trade: a full swap and back (beat and bed's gusts still) | still | 12.2 s | 5.1 s | 2.5 s | 1.5 s (set) |
| beat: the right ear above the left | 0 Hz | 0.96 Hz | 2.32 Hz | 3.98 Hz | 5.95 Hz |
| orbit: where a note begun 67° right starts, as the left ear's delay | 0 (front) | +14 samples | +13 | (circling) | (circling) |
| orbit: a circle (set) | none | none | 14 s | 4.3 s | 2.0 s |
| orbit: how far the left ear's delay travels over 4 s | 0 | 2 samples | 25 samples | 29 | 30 |
| near: level (a note at the right; split and trade still) | −32.2 dBFS | −26.4 | −24.1 | −23.8 | −22.3 |
| near: right over left | +10.2 dB | +7.5 | +7.0 | +9.8 | +13.8 |
| near: spectral centroid | 689 Hz | 853 Hz | 816 Hz | 826 Hz | 927 Hz |
| bed, no sample (the whisper's share 0, 6, 25, 56, 100%): harmonic to noise | 21 dB | 17 dB | 13 dB | 12 dB | 12 dB |
| bed, no sample: how far the level swells and sinks (25 ms, 10th to 90th percentile) | 4.2 dB | 3.9 dB | 4.7 dB | 7.0 dB | 8.7 dB |
| bed, no sample: spectral centroid | 716 Hz | 753 Hz | 872 Hz | 964 Hz | 933 Hz |
| bed, a recorded voice loaded (its share 0 to 100%): harmonic to noise | 21 dB | 14 dB | 10 dB | 6 dB | 9 dB |
| bed, a recorded voice loaded: spectral centroid | 726 Hz | 677 Hz | 765 Hz | 767 Hz | 795 Hz |

The other controls are measured at the defaults, bed's whisper at its
quarter share included; ring's rise and trade's swaps with its gusts
still, which would hide them.

`tools/controls.cpp --fifths` measures each at 21 points and finds its
flattest fifth of travel. Each control moves through at least one of its
measures over every fifth of its range, except where a measure saturates
by design: orbit's travel is a full swing once it circles faster than every
4 s, and its rate is the change there.

The OP-XY's envelopes, LFOs, filter and effects still apply. The factory
envelope (`machine.json`) has a gentle attack and a long release, for pads.

## Presets

"Chord" is the peak of an eight-note chord (C3 E3 G3 B3 D4 F#4 A4 C5) struck
together, where 1.0 is full scale, with no sample and with one.

| Preset | Role | Sound | Chord |
| --- | --- | --- | --- |
| Seance | The init sound: a haunted pad | A sung "ah", 2 s ring, harmonics split 60% and trading every 7 s, a 0.7 Hz beat, the notes spread round you; bed at half | 0.69 / 0.56 |
| Whisper | Whispers at your ears | Breath alone, a short ring (a pitched whisper), each note in its own place, 12 cm away; bed at half | 0.87 / 0.81 |
| Choir | A choir from nowhere | A sung "oh", long ring, split 80%, the voices circling slowly, at a distance; bed at half | 0.53 / 0.37 |
| Revenant | A drone | "oo", 10 s ring, a 2.2 Hz beat breathing between the ears, held in front; bed at 40% | 0.59 / 0.66 |
| Wraith | A presence that circles | Breath turning to "oo", circling every 3.1 s and passing close; bed at half | 0.72 / 0.72 |
| Skull | The note that isn't there | "eh", the fundamental gone, the halves trading every 8.4 s: a melody heard only between the ears; bed at half | 0.74 / 0.69 |
| Glass | Ringing, bell-like | "ee", 8 s ring, a slight beat: a glass harmonica; bed at 40%, to keep it clear | 0.64 / 0.67 |
| Haunting | A recording, as an apparition | The bed at full, rung at the note, split 70%, spread and drifting; with no sample, a whispering ghost | 0.74 / 0.67 |

Every preset with no sample, measured on a held A3 (`tools/measure.cpp`):

| Preset | Level (dBFS RMS) | Crest | Harmonic to noise | Centroid | Mono fold-down | Its worst third-octave | Coherence between the ears, now |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Seance | −26.7 | 16.3 dB | 14 dB | 827 Hz | −4.0 dB | −4.3 dB | 0.22 |
| Whisper | −25.4 | 15.9 dB | 8 dB | 1903 Hz | −3.0 dB | −5.0 dB | 0.66 |
| Choir | −25.4 | 15.0 dB | 15 dB | 631 Hz | −3.1 dB | −4.9 dB | 0.39 |
| Revenant | −24.8 | 9.6 dB | 18 dB | 364 Hz | −2.8 dB | −2.9 dB | 0.08 |
| Wraith | −26.2 | 15.8 dB | 11 dB | 1194 Hz | −2.2 dB | −3.9 dB | 0.44 |
| Skull | −25.0 | 11.8 dB | 14 dB | 1052 Hz | −1.8 dB | −2.6 dB | 0.22 |
| Glass | −25.5 | 12.6 dB | 22 dB | 526 Hz | −2.7 dB | −3.8 dB | 0.72 |
| Haunting | −25.5 | 14.0 dB | 12 dB | 719 Hz | −2.1 dB | −4.2 dB | 0.26 |

With a sample loaded, each preset's held A3 against none, at its own bed:
the average third-octave move shows how far the sample changes it. Two samples:
a breathy, wordless voice (close to Ghost's own breath), and a loop of kick,
hats and a tone.

| Preset | Level, none / the voice (dBFS) | Centroid, none / the voice | Harmonic to noise, none / the voice | Third-octave move, the voice | Third-octave move, the loop |
| --- | --- | --- | --- | --- | --- |
| Seance | −26.7 / −27.9 | 828 / 680 Hz | 14 / 6 dB | 9.3 dB | 14.5 dB |
| Whisper | −25.4 / −26.5 | 1965 / 1340 Hz | 8 / 5 dB | 2.8 dB | 6.4 dB |
| Choir | −27.5 / −28.2 | 515 / 602 Hz | 14 / 7 dB | 12.6 dB | 14.6 dB |
| Revenant | −24.7 / −26.0 | 438 / 469 Hz | 8 / 5 dB | 9.2 dB | 13.3 dB |
| Wraith | −26.4 / −26.3 | 1137 / 798 Hz | 10 / 7 dB | 4.7 dB | 6.9 dB |
| Skull | −24.9 / −26.4 | 1022 / 756 Hz | 14 / 8 dB | 6.6 dB | 14.9 dB |
| Glass | −25.1 / −26.3 | 567 / 586 Hz | 10 / 8 dB | 9.8 dB | 12.8 dB |
| Haunting | −27.4 / −25.4 | 742 / 716 Hz | 12 / 5 dB | 9.6 dB | 15.9 dB |

Whisper and Wraith are breath, so a breathy recording changes them least;
a loop's rhythm comes through even there. A recording sits up to 1.5 dB
lower than song, as breath does, since it peaks higher for its power.

- **The mono fold-down** loses about 3 dB, which is what two decorrelated ears
  lose when summed, not a cancellation. Its worst third of an octave is at
  most 7 dB down.
- **"Now"** is coherence between the ears over 0.37 s windows at 220–880 Hz.
  It is low because each harmonic sits in one ear, or turns between them:
  that is the effect.

`tools/render.cpp` plays a short phrase on each preset and writes it as a
stereo WAV.

## How it works

Each note borrows a pair of resonators from a pool of 16 that every Ghost
voice on every track shares. Sample engines get no call when a voice ends,
so slots are freed by census: a slot not rendered for two cycles is free.
A resonator is a delay line one period long, fed back through a gentle
low-pass: a string that rings for ring's time.

**What excites it.** Breath, a sawtooth hum through two formant filters,
and the bed run at half rate, since they lie well under 11 kHz; the
resonators interpolate them up. Each ear has its own hum, because a
resonator driven by a periodic pulse rings at the pulse's pitch, and the
right ear's has to beat. The bed has its sign flipped at random for 70% of
its samples, which whitens it while keeping its envelope, so a recording
rings the note instead of replaying its own partials. With no sample, the
bed is white breath in gusts, each gust a random strength (a quarter of them
silent) eased in over about 12 ms, and up to 70% of the note's level rides
the same gusts.

**split.** Each ear's output is its resonator plus or minus itself half a
period back, which keeps the even harmonics or the odd. Mono is always the
whole note, so nothing cancels. Past 75%, the odd ear's excitation is
notched at the note (the even ear's half-period tap leaves no fundamental).
The notch is wide, so the resonator's peak, a few cents off, still falls in
it.

**trade, beat and orbit.**
- trade swings the split through zero to its opposite.
- beat sharpens the right resonator and its hum.
- orbit's head model gives the far ear up to 0.27 ms of delay and a head
  shadow (14 kHz down by 2.5 to 4 octaves). Level differs between the ears
  by up to 12 dB, with their power kept constant. Behind is 1.6 octaves
  darker. Distance darkens both ears by up to 4 octaves at 3 m (as the cube
  of how far, so little nearer than a metre) and lowers the level by 4 dB.

**DC.** A loop resonates at DC as well as at the note, so each loop takes
its own 0.2 Hz average off its feedback, too slow to touch the note.

All of this is fixed point, with no division in the sample loop. Each note
caches its period and loop response.

## Level

A follower holds every setting, pitch and bed near Hexagon's level, about
−24 dBFS RMS a voice. It falls fast, rises slowly and moves at most 18 dB.
The excitation is scaled by the loop's true gain at the note, so the lines
never clip. Every preset's held A3 is within 3 dB of Hexagon's level, with
a sample or without (the tables above); trade moves it a little, since each
ear's head shadow takes more from the half with the higher harmonics. Breath
sits 2.6 dB lower than song, and a recording up to 1.5 dB lower, because
they peak higher and sound louder at the same power. An eight-note chord
peaks at 0.53 to 0.87 of full scale, and 0.37 to 0.81 with a sample.

## Screen

![Ghost's screen: Pepper's ghost, a held note](docs/ghost.gif)

Pepper's ghost: a Victorian theatre's stage in front elevation, in a few
bold lines, deep from front to back and grey by depth, nearest brightest:
- in front, two pilasters with capitals and bases, the arch they carry with
  a cartouche at its head, the apron, and seven footlights along it;
- a step behind, heavy swagged curtains meeting under the cartouche with a
  tassel there, and a drape each side tied back with a tasselled cord;
- in the middle, the great pane of glass, its top edge and two glints;
- at the back, the floor receding to the back wall, and the wall's corners.

No two of these lines cross, and every outline keeps the OP-XY's drawing
rules, which go beyond the SDK's: one subpath of at most 64 points, no two
neighbouring points within an eighth of a pixel (they merge), no closing
edge under a quarter of a pixel (it draws as a sliver), and some area with
no turn straight back (either draws as streaks). `tools/make_panel.py`
checks the artwork, and `tools/frames.cpp` every frame it draws. The apparition
leads, brighter and bolder than all of it, and like each figure on the stage
it stands over a black silhouette, so it hides what is behind it: the
curtains, the glass, its twin, the chalk path at its feet. Each nimbus ring
is black inside too, so the stage behind a head never shows through it. Nothing drawn ever reaches the
footlights or the pilasters (`tools/frames.cpp clear` checks this over
30,976 settings). Nothing is finer than about 4 px apart. The contact sheets
are at 1:1 (`docs/controls-1x.png`) and twice that (`docs/controls.png`).

The note's apparition stands in the glass where the sound is in your head:
across the stage for left and right, upstage and dimmer behind you,
downstage and larger when near. It is faint at rest, as a preview of where
the next note will sound. It brightens on a note in 0.15 s and fades on
release over ring's time (at most 2.5 s).

![Each control at 0, 50 and 100, the others at the default, a sample loaded; then no sample](docs/controls.png)

Every control acts on the apparition or the stage:

| Control | On the screen |
| --- | --- |
| ring | A nimbus round each head: none at 0, up to three rings at 100. The figure hides the rings behind it. While notes are held they breathe at the beat's rate. |
| voice | Its pose, from a bowed, hooded figure (breath) to one singing with arms raised (ee), and its mouth, shaped to the vowel. |
| split | The glass's two faces give two apparitions, each with its own mouth and nimbus: one at 0, the nearer hiding part of the other at 50, standing apart at 100 (72 px between them at full size). |
| trade | The twins lean toward each other, up to 20°, lifted so their hems stay clear of the footlights. While held they swing through each other at trade's pace. |
| beat | The apparition's outline trembles, up to 6 px, the tremble running at the beat's rate while held. |
| orbit | The chalk path on the floor where it can walk: a mark at the front at 0, an arc as the notes spread, the whole circle once they circle. The apparition walks it while held. |
| near | Its size and depth, from small at the back wall to large behind the footlights. The chalk path follows it. |
| bed | Its robe frays, up to 8 px with its share: into the sample's own waveform (the 64 peaks of its overview), or with no sample into the whisper's ragged gusts. The fray scrolls while held. |

A second and third held note bring two more apparitions, each where its
own note began.

Ghost is a sample engine, and with no sample loaded it says so as Grains
does: "load a sample", then "shift +" and the Sample key's icon, in Grains'
words, face and greys. It doesn't cover the stage, because Ghost plays on
without a sample (breath and hum, and bed's own whisper), so the hint sits on
one line under the apron, in front of the footlights, where nothing on the
stage ever reaches. The hint goes as soon as a sample is loaded. The
contact sheets' last row shows it.

The display cannot read the audio, so it draws everything from the
controls, note activity and time, through the same `place()` (`ghost.h`) the
engine uses. Each apparition's place is seeded from the note's serial, so it
matches the sound in kind, not note for note.

The screen uses 4 reshaping outlines of 64 points (the apparition, its twin,
its mouth and the chalk path), 24 nodes (8 of them the header bar), all 16
text slots, 46 static outlines (37 of the stage, 9 of the hint's icon) and
84 bytes of state. Churn over a scripted
run (`tools/frames.cpp`: every control, a page switch, three notes circling,
the bed, reduced motion) is below. The idle screen is still: nothing changes
a second after the last fade. Reduced motion snaps the apparition and stills
its tremble, hem and circling.

| | Mean nodes changed a frame | While notes are held | Peak |
| --- | --- | --- | --- |
| 60 fps | 4.9 | 9.2 | 16 (a page switch) |
| 30 fps | 5.2 | 9.3 | 16 |

Sample engines have no browser preview, so the screen's frames, churn, GIF
and contact sheets come from `tools/frames.cpp` on the host.

## CPU

Measured on an OP-XY (OS 1.2.1) with the SDK's `profile benchmark`, three runs, at
`v0.1.0`: Ghost's sample callback at its default settings, playing the SDK's
generated two-second test sample, costs **3.05%** of the 580,352-cycle audio
block. This test plays one setting; heavier settings cost more.

The figures below are host estimates made during development.

Counted from the inner loops of `sdk.py compile`'s Blackfin disassembly,
per sample:

| Stage | Instructions a sample |
| --- | --- |
| Breath (half rate) | 11.5 |
| Each ear's exciter | 20.5 |
| Each resonator | 41.5 |
| The near ear's head stage | 15 |
| The far ear's head stage | 35 |
| Level follower | 3 |
| Stereo bed (when bed is on, with a sample) | 10.5 |
| The notch (past 75% split) | 16.5 |

With about 1.1k a block of per-block work, a voice comes to:
- **about 13k instructions a block at the default (Seance), 13.7k with a
  sample loaded,** which the bed then reads;
- **about 14.9k at the heaviest setting** (a stereo bed, split past 75%,
  beat, trade and orbit all moving).

Without a sample, bed's whisper replaces the sample read with one
multiply a half-rate sample, so it is lighter than a loaded bed.

The host estimate (`tools/bench.cpp`, timed against the SDK's reference loop
as the SDK estimates synths) reads 20k and 21.6k, because the portable
`q31::multiply` is one instruction on the Blackfin but a slow path on the
host. With a plain multiply in its place it reads 8.7k and 9.7k.

Memory is 226 KiB, mostly the pool's lines (16 notes × 8 KiB). A note's
first block clears its lines, a one-off of about 4k stores.

## On the OP-XY

Not yet checked on the device:

- **Anything that narrows the stereo image weakens the effect,** though it
  never breaks the sound, since mono is a full note: a track filter or
  envelope that does not act alike on both channels, pan, the mixer, the
  master compressor, a wet send reverb (it fills in between the ears), or a
  headphone output that is not truly stereo.
- **The pool's census** assumes the OP-XY stops rendering a voice once its
  envelope ends. If it renders silent voices, lines stay held.
- **The LFOs** can target every control. Fast modulation of split or orbit
  is untested.

## Files

- `sample.cpp`, `core.h`: the voice.
- `ghost.h`: the controls' laws and the apparition's place, shared by the
  engine and the screen.
- `tables.h`: the sine, exp2, head-shadow and formant tables, written by
  `tools/gen_tables.py`.
- `display.cpp`, `display.h`, `captions.h`, `figure.h`: the screen.
- `panel.svg.fragment`: the screen's artwork, written with `figure.h` (the
  apparition's two poses) by `tools/make_panel.py`. Run the script after
  changing a shape, and commit all three.
- `tests/sound.cpp`, with `tests/analysis.h`: the sound checks, run by
  `sdk.py test`.
- `docs/`: the screen's GIF and contact sheets.

`tests/sound.cpp` checks:

- **Every preset,** with no sample and with one: a held A3 within −29 to
  −21 dBFS, never over half scale, and an eight-note chord under 0.95.
- **The mono fold-down:** no third of an octave more than 10 dB down, and at
  most 6 dB lost in all.
- **Tuning:** within 3 cents at 110, 220, 440 and 880 Hz.
- **split:** each harmonic at least 10 dB louder in its own ear at 0.74, the
  fold-down keeping every harmonic, and the fundamental at least 15 dB down
  at full.
- **trade:** at full it swaps the halves and back every 1.5 s.
- **beat:** 0.7 detunes the right ear by 3.57 Hz.
- **The head:** a near source on the right is louder there and later on the
  left, the far ear's highs are shadowed, and behind is darker.
- **orbit:** it closes a circle in 2 s.
- **Captions:** every value each control can show, and every title, fits its
  header column in the device's face (values within 53 px at 20 px, titles
  within 56 px at 10 px; `tests/header_font.h`, from `tools/header_font.py`).
- **A loaded sample** changes the default sound (Seance, bed at 50%): its
  third-octave spectrum moves at least 6 dB on average (6.8 dB as tested).
  Every preset also passes the level and chord checks with a sample loaded.
- **bed, the pool and mono:** the bed plays, and breath plays without one;
  without a sample, bed at full is at least 5 dB noisier and swells and
  sinks at least 3 dB more; a seventeenth note waits for a line; mono
  output works.

The tools are not part of the machine. The compile command for each is at
the top of its file.

- `tools/controls.cpp` and `tools/measure.cpp` print the tables above.
- `tools/render.cpp` writes each preset's phrase as a WAV into a folder you
  give it.
- `tools/bench.cpp` gives the host CPU estimate.
- `tools/frames.cpp` checks the screen frame by frame, against the SDK's
  rules and the OP-XY's, counts its churn and writes frames as JSON;
  `frames clear` sweeps every control, with a sample and without, and checks
  that nothing drawn reaches the footlights, the hint or the pilasters. `tools/frame2svg.py` draws a frame as SVG; it needs
  `XY_FONT` set to the path of the OP-XY's face, UniversNextPro-Light.ttf,
  which is not in this repository. `tools/make_gif.py` makes `docs/ghost.gif`
  and `tools/make_sheet.py` makes `docs/controls.png` and `docs/controls-1x.png`
  from those frames.
