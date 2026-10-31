#!/usr/bin/env python3
"""Fetch every species' cry and pack it for the SD card.

    python tools/pack_cries.py                  # fetch (cached), convert, pack
    python tools/pack_cries.py --preview 25 pika.wav   # hear one conversion

Source: https://github.com/PokeAPI/cries. The LEGACY set (the cries from before
Gen 6 redid them all) where it exists -- 1..649 -- and LATEST past that. Legacy
is what these creatures sounded like on a handheld, which suits the panel.

**These are Nintendo / The Pokemon Company's audio**, like the trainer avatars
and unlike the PMD sprites (CC BY-NC). The PokeAPI repo says so in its LICENSE.
So the pack is a SEPARATE, OPTIONAL file (web/cries.pak), never mixed into the
sprite packs, and this script is the way to get it: anyone can fetch the cries
themselves. See CREDITS.md.

Output, per species: /cries/cNNN.raw on the card -- signed 8-bit mono PCM at
16 kHz, the I2S rate (audio.cpp), so the firmware plays it with no decoder and
no resampling. Silence is trimmed, the peak normalised, and anything past
CRY_MAX_S cut with a short fade. About 10 MB for all 809.

Needs: pip install soundfile numpy   (soundfile ships its own Ogg Vorbis
decoder, so there is no ffmpeg to install).
"""
import argparse
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CACHE = os.path.join(HERE, 'cries_cache')
OUT = os.path.join(HERE, 'sdcard', 'cries')
PAK = os.path.join(ROOT, 'web', 'cries.pak')
sys.path.insert(0, HERE)
from pack_bundle import write_pak  # noqa: E402  (same TPAK format the installer splits)

URL = 'https://raw.githubusercontent.com/PokeAPI/cries/main/cries/pokemon/%s/%d.ogg'
LEGACY_MAX = 649          # the legacy set stops at Gen 5
RATE = 16000              # must match SAMPLE_RATE in audio.cpp
CRY_MAX_S = 2.5           # the longest cries ramble; nobody waits for the end
# MUST match CRY_MAX_BYTES in sdmon.cpp, or the board refuses the file
CRY_MAX_BYTES = int(RATE * CRY_MAX_S)


def dex_count():
    src = open(os.path.join(ROOT, 'dex.h'), encoding='utf-8').read()
    return int(re.search(r'#define DEX_COUNT (\d+)', src).group(1))


def fetch(n):
    kind = 'legacy' if n <= LEGACY_MAX else 'latest'
    path = os.path.join(CACHE, kind, '%d.ogg' % n)
    if not os.path.exists(path):
        r = subprocess.run(['curl', '-fsSL', '-o', path, URL % (kind, n)])
        if r.returncode != 0:
            if os.path.exists(path):
                os.remove(path)
            raise SystemExit('fetch failed: %s' % (URL % (kind, n)))
    return path


def resample(x, sr):
    """To RATE. A windowed-sinc low-pass first, at the original rate, so
    downsampling does not fold everything above 8 kHz back in as hiss."""
    if sr == RATE:
        return x
    if sr > RATE:
        cut = 0.45 * RATE / sr            # just under the new Nyquist
        taps = 101
        k = np.arange(taps) - (taps - 1) / 2
        h = 2 * cut * np.sinc(2 * cut * k) * np.hamming(taps)
        x = np.convolve(x, h / h.sum(), mode='same')
    t = np.arange(0, len(x) * RATE / sr) * sr / RATE
    return np.interp(t, np.arange(len(x)), x)


def convert(path):
    x, sr = sf.read(path, dtype='float32', always_2d=True)
    x = resample(x.mean(axis=1).astype(np.float64), sr)
    loud = np.nonzero(np.abs(x) > 0.02 * (np.abs(x).max() or 1))[0]
    if len(loud) == 0:
        raise SystemExit('%s is silent' % path)
    x = x[loud[0]:loud[-1] + 1]
    if len(x) > CRY_MAX_BYTES:
        x = x[:CRY_MAX_BYTES]
        fade = int(0.04 * RATE)
        x[-fade:] *= np.linspace(1, 0, fade)
    x = x / (np.abs(x).max() or 1) * 0.95
    return np.round(x * 127).astype(np.int8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--preview', nargs=2, metavar=('DEX', 'WAV'),
                    help='convert one cry and write it as a WAV, to listen to')
    args = ap.parse_args()
    for kind in ('legacy', 'latest'):
        os.makedirs(os.path.join(CACHE, kind), exist_ok=True)

    if args.preview:
        pcm = convert(fetch(int(args.preview[0])))
        sf.write(args.preview[1], pcm.astype(np.int16) * 256, RATE, subtype='PCM_16')
        print('%s: %.2f s' % (args.preview[1], len(pcm) / RATE))
        return

    count = dex_count()
    with ThreadPoolExecutor(16) as ex:
        paths = list(ex.map(fetch, range(1, count + 1)))
    os.makedirs(OUT, exist_ok=True)
    files, total, longest = [], 0, 0
    for n, p in enumerate(paths, start=1):
        pcm = convert(p)
        assert len(pcm) <= CRY_MAX_BYTES
        out = os.path.join(OUT, 'c%03d.raw' % n)
        pcm.tofile(out)
        files.append(out)
        total += len(pcm)
        longest = max(longest, len(pcm))
    write_pak(PAK, files, prefix='cries/')
    print('%d cries, %.1f MB, longest %.2f s -> %s'
          % (len(files), total / 1048576, longest / RATE, os.path.normpath(PAK)))


if __name__ == '__main__':
    main()
