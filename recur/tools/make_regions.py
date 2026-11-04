"""Points every preset at Recur's own sample and writes the init preset.

    python tools/make_regions.py --sdk ../../open-xy

Each presets/<name>.json keeps its controls and envelope and gets one sample
region naming samples/break.wav as the build installs it
(content/samples/recur/break.wav, SDK 1.10 "samples"), so a preset opens with
the break loaded. presets/default.json is the SDK's generated init preset
(definition.json defaults) with the same region. Run it after changing the
sample, the defaults or a preset.
"""
import argparse
import json
from pathlib import Path
import sys
import wave

ROOT = Path(__file__).resolve().parent.parent
SAMPLE = 'break'
# Middle C plays the break as recorded, at its own tempo.
KEY = 60


def region(frames):
    """One region over the whole sample, in the form stock saves a sample engine's preset."""
    return {
        'framecount': frames,
        'gain': 0,
        'hikey': KEY,
        'loop.crossfade': 0,
        'loop.end': frames,
        'loop.onrelease': True,
        'loop.start': 0,
        'pitch.keycenter': KEY,
        'reverse': False,
        'sample': f'content/samples/recur/{SAMPLE}.wav',
        'sample.end': frames,
        'sample.start': 0,
        'tune': 0,
    }


def write(path, preset):
    path.write_text(json.dumps(preset, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--sdk', type=Path, default=ROOT.parent.parent / 'open-xy',
                        help='Open-XY SDK checkout (1.10 or later)')
    args = parser.parse_args()
    with wave.open(str(ROOT / 'samples' / f'{SAMPLE}.wav'), 'rb') as w:
        frames = w.getnframes()
    regions = [region(frames)]
    presets = ROOT / 'presets'
    for path in sorted(presets.glob('*.json')):
        if path.stem == 'default':
            continue
        preset = json.loads(path.read_text(encoding='utf-8'))
        preset['regions'] = regions
        write(path, preset)
    # The init preset: the SDK's generated one, from this machine without its own default.
    sys.path.insert(0, str(args.sdk.resolve()))
    from tooling.adapters import default_preset
    from tooling.machines import machine_at
    default = presets / 'default.json'
    previous = default.read_bytes() if default.exists() else None
    default.unlink(missing_ok=True)
    try:
        preset = default_preset(machine_at(ROOT))
    except Exception:
        if previous is not None:
            default.write_bytes(previous)
        raise
    preset['regions'] = regions
    write(default, preset)
    print(f'{len(list(presets.glob("*.json")))} presets point at content/samples/recur/{SAMPLE}.wav ({frames} frames)')


if __name__ == '__main__':
    main()
