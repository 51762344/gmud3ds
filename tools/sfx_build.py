#!/usr/bin/env python3
"""Build the in-game sound effects as mono PCM16 RIFF/WAVE.

The 3DS DSP plays linear PCM directly, so the game side is only a loader plus
an ndsp channel setup (source/audio.c); every musical decision lives here.

The original WQX build had no sound at all, so these effects are the 3DS port's
own addition.  They deliberately stay in the same 1-bit spirit as the screen
art: square waves, short envelopes, no samples, no reverb, no stereo tricks.

Usage:
    python tools/sfx_build.py --preview build/sfx_preview
    python tools/sfx_build.py                       # data/audio/sfx_*.wav

Only the Python standard library is used.
"""

from __future__ import annotations

import argparse
import math
import os
import random
import wave
from array import array

SFX_RATE = 22050


# ---------------------------------------------------------------- primitives


def n_samples(seconds: float, rate: int) -> int:
    return int(round(seconds * rate))


def square(n: int, freq: float, rate: int, duty: float = 0.5) -> list[float]:
    """Band-unlimited square/pulse; the buzzer voice of the whole bank."""
    out = [0.0] * n
    step = freq / rate
    phase = 0.0
    for i in range(n):
        out[i] = 1.0 if (phase % 1.0) < duty else -1.0
        phase += step
    return out


def sine(n: int, freq: float, rate: int) -> list[float]:
    return [math.sin(2.0 * math.pi * freq * i / rate) for i in range(n)]


def lowpass(buf: list[float], cutoff: float, rate: int) -> list[float]:
    """One-pole low pass; takes the fizz off a square wave."""
    if cutoff <= 0:
        return buf
    dt = 1.0 / rate
    rc = 1.0 / (2.0 * math.pi * cutoff)
    a = dt / (rc + dt)
    out = [0.0] * len(buf)
    prev = 0.0
    for i, v in enumerate(buf):
        prev += a * (v - prev)
        out[i] = prev
    return out


def env(n: int, rate: int, attack: float = 0.004, release: float = 0.02,
        curve: float = 1.8) -> list[float]:
    """Exponential decay with linear attack/release ramps.

    Keeping both ramps non-zero is what stops a one-shot from clicking on the
    3DS speakers; `curve` shapes how fast the body dies away.
    """
    out = [0.0] * n
    an = max(1, int(attack * rate))
    rn = max(1, int(release * rate))
    for i in range(n):
        t = i / (n - 1) if n > 1 else 1.0
        e = (1.0 - t) ** curve
        if i < an:
            e *= i / an
        if i >= n - rn:
            e *= (n - 1 - i) / rn
        out[i] = e
    return out


def beep(freq: float, seconds: float, rate: int, duty: float = 0.5,
         curve: float = 1.8, cutoff: float = 6000.0) -> list[float]:
    n = n_samples(seconds, rate)
    buf = square(n, freq, rate, duty)
    e = env(n, rate, 0.004, min(0.02, seconds * 0.35), curve)
    buf = [buf[i] * e[i] for i in range(n)]
    return lowpass(buf, cutoff, rate) if cutoff else buf


def thud(freq: float, seconds: float, rate: int) -> list[float]:
    """A short pitched knock - the weight of a step, not a drum machine."""
    n = n_samples(seconds, rate)
    buf = sine(n, freq, rate)
    e = env(n, rate, 0.002, 0.03, 3.0)
    return [buf[i] * e[i] for i in range(n)]


def dc_block(buf: list[float]) -> list[float]:
    """Remove the offset a non-50% pulse carries; DC costs headroom and pops."""
    mean = sum(buf) / max(1, len(buf))
    return [x - mean for x in buf]


def whoosh(seconds: float, rate: int, f0: float = 380.0,
           f1: float = 2600.0, seed: int = 9) -> list[float]:
    """Noise through a sliding low pass: a door swinging open."""
    n = n_samples(seconds, rate)
    rnd = random.Random(seed)
    out = [0.0] * n
    prev = 0.0
    dt = 1.0 / rate
    for i in range(n):
        t = i / n
        cutoff = f0 * (f1 / f0) ** t
        rc = 1.0 / (2.0 * math.pi * cutoff)
        a = dt / (rc + dt)
        prev += a * (rnd.uniform(-1.0, 1.0) - prev)
        out[i] = prev
    for i in range(n):
        t = i / n
        # Swell in, then fall away; a pure linear fade sounds like a tape stop.
        out[i] *= math.sin(math.pi * min(1.0, t * 1.25)) ** 1.4
    return out


# ------------------------------------------------------------------ mixing


def seq(parts: list[list[float]], gap: float, rate: int) -> list[float]:
    out: list[float] = []
    silence = [0.0] * n_samples(gap, rate)
    for i, p in enumerate(parts):
        if i:
            out.extend(silence)
        out.extend(p)
    # 6 ms of tail so the DAC never jumps from a non-zero sample to silence.
    out.extend([0.0] * n_samples(0.006, rate))
    return out


def mix(*layers: list[float]) -> list[float]:
    n = max(len(x) for x in layers)
    out = [0.0] * n
    for layer in layers:
        for i, v in enumerate(layer):
            out[i] += v
    return out


def normalize(buf: list[float], peak: float) -> list[float]:
    top = max(abs(x) for x in buf) or 1.0
    return [x * (peak / top) for x in buf]


# ------------------------------------------------------------------ effects
# Every candidate is a function so the preview and the shipped bank come from
# exactly the same code - only the choice in FINAL changes.


def test_tone(rate: int) -> list[float]:
    """1 s of 440 Hz: proves the speaker path works before judging a 200 ms blip."""
    n = n_samples(1.0, rate)
    buf = sine(n, 440.0, rate)
    e = env(n, rate, 0.01, 0.06, 0.0)
    return [buf[i] * e[i] for i in range(n)]


def confirm_two_tone(rate: int) -> list[float]:
    """880 -> 1320 Hz: the classic 'yes, done' rise."""
    return seq([beep(880.0, 0.095, rate, 0.5, 1.6),
                beep(1320.0, 0.125, rate, 0.5, 2.0)], 0.0, rate)


def confirm_single(rate: int) -> list[float]:
    """One short high blip - the smallest possible acknowledgement."""
    return seq([beep(1568.0, 0.10, rate, 0.5, 2.4, 5000.0)], 0.0, rate)


def door_two_note(rate: int) -> list[float]:
    """G4 -> C4: a soft two-step "you are inside now"."""
    step = beep(392.0, 0.09, rate, 0.25, 1.5, 3500.0)
    body = mix(beep(262.0, 0.155, rate, 0.25, 2.0, 3000.0),
               thud(196.0, 0.125, rate))
    return seq([step, normalize(body, 0.9)], 0.0, rate)


def door_whoosh(rate: int) -> list[float]:
    """Hinge sweep plus a latch knock - more literal, less 8-bit."""
    sweep = normalize(whoosh(0.26, rate), 0.55)
    return seq([sweep, mix([0.0] * n_samples(0.02, rate), thud(160.0, 0.08, rate))],
               0.0, rate)


VARIANTS = {
    "confirm_two_tone": ("确认 · 双音上行 880→1320", confirm_two_tone),
    "confirm_single": ("确认 · 单声 1568", confirm_single),
    "door_two_note": ("进门 · 下行双音 G4→C4", door_two_note),
    "door_whoosh": ("进门 · 门轴+门闩", door_whoosh),
}

# The variant that ships as data/audio/sfx_<name>.wav.
FINAL = {
    "sfx_confirm.wav": "confirm_two_tone",
    "sfx_door.wav": "door_two_note",
}


def write_wav(path: str, buf: list[float], peak: float) -> None:
    buf = dc_block(buf)
    data = array("h", (int(max(-1.0, min(1.0, x)) * peak * 32767) for x in buf))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SFX_RATE)
        f.writeframes(data.tobytes())


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--preview", metavar="DIR",
                    help="write every candidate into DIR for listening")
    ap.add_argument("--out", default="data/audio",
                    help="output directory for the shipped effects")
    args = ap.parse_args(argv)

    if args.preview:
        for name, (label, fn) in VARIANTS.items():
            path = os.path.join(args.preview, name + ".wav")
            write_wav(path, fn(SFX_RATE), 0.90)
            print(f"{path}  {label}")
        path = os.path.join(args.preview, "_test_tone.wav")
        write_wav(path, test_tone(SFX_RATE), 0.90)
        print(f"{path}  440 Hz 1 s reference")
        return 0

    for filename, variant in FINAL.items():
        label, fn = VARIANTS[variant]
        path = os.path.join(args.out, filename)
        write_wav(path, fn(SFX_RATE), 0.90)
        print(f"{path}  {label}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(__import__("sys").argv[1:]))
