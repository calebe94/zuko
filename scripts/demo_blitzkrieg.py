#!/usr/bin/env python3
"""Blitzkrieg-Bop-inspired demo riff generator for the Zuko host harness.

Synthesizes a guitar-style riff with Karplus-Strong plucked strings
(analytical synthesis, stdlib only — no samples, no NAM, nothing external),
plus a simple rock drum kit (kick = decaying sine sweep, snare/hat = filtered
noise), so the firmware DSP chain (preamp + tone stack + cab sim) has real
material to chew on while the hardware is not here.

Outputs:
  riff_clean.wav  — raw synth mix (what "the guitar" plays)
  Then run: ./harness riff_clean.wav riff_zuko.wav
  riff_zuko.wav — the same riff through the firmware chain.

Usage: python3 scripts/demo_blitzkrieg.py [out_dir]
"""

import math
import struct
import sys
import wave
from pathlib import Path

SR = 48000

# ---------------------------------------------------------------- riff data

E5 = (82.41, 123.47, 164.81)   # E2 B2 E3
G5 = (98.00, 146.83, 196.00)
A5 = (110.00, 164.81, 220.00)
D5 = (146.83, 220.00, 293.66)

# One 8th-note entry: (chord or None, accent). None = rest.
# Inspired by the Ramones chug, NOT a transcription: chug-chug on the root,
# quick move to IV/V, back down. Repeat, then a small turnaround.
RIFF = [
    (E5, 1), (E5, 0), (E5, 0), (E5, 0), (G5, 0), (G5, 0), (A5, 0), (A5, 0),
    (E5, 1), (E5, 0), (E5, 0), (E5, 0), (G5, 0), (G5, 0), (D5, 0), (E5, 1),
]

BPM = 178.0
EIGHTH = 60.0 / BPM / 2.0  # seconds per 8th note

# ---------------------------------------------------------------- synthesis

def karplus(freqs, dur, decay, rng):
    """Plucked-string mix of `freqs` via Karplus-Strong."""
    n = int(SR * dur)
    out = [0.0] * n
    for f in freqs:
        period = max(2, int(SR / f))
        buf = [rng.uniform(-1.0, 1.0) for _ in range(period)]
        for i in range(n):
            v = decay * 0.5 * (buf[i % period] + buf[(i + 1) % period])
            buf[i % period] = v
            out[i] += v
    k = 1.0 / len(freqs)
    return [s * k for s in out]


def render_guitar(rng):
    """The full riff, one 8th note at a time, palm-mute chug style."""
    samples = []
    for chord, accent in RIFF:
        dur = EIGHTH * (1.9 if accent else 0.85)  # accents ring, chugs mute
        decay = 0.996 if accent else 0.960        # short decay = chug
        note = karplus(chord, dur, decay, rng)
        # hard-trim/pad to exactly one 8th so the grid stays tight
        keep = int(SR * EIGHTH)
        if len(note) > keep:
            note = note[:keep]
        else:
            note += [0.0] * (keep - len(note))
        amp = 0.9 if accent else 0.62
        samples.extend(s * amp for s in note)
    return samples


def render_drums(rng, total_n):
    """Simple rock beat: kick on 1&3, snare on 2&4, 8th hats."""
    out = [0.0] * total_n
    beat = 60.0 / BPM

    def add(n_start, sig, gain):
        for i, s in enumerate(sig):
            j = n_start + i
            if j < total_n:
                out[j] += s * gain

    kick_n = int(SR * 0.12)
    kick = []
    phase = 0.0
    for i in range(kick_n):
        f = 95.0 * math.exp(-i / (SR * 0.02)) + 45.0
        phase += 2 * math.pi * f / SR
        kick.append(math.sin(phase) * math.exp(-i / (SR * 0.045)))
    snare_n = int(SR * 0.15)
    snare = [(rng.uniform(-1, 1) * 0.7 + 0.3 * math.sin(2 * math.pi * 190 * i / SR))
             * math.exp(-i / (SR * 0.03)) for i in range(snare_n)]
    hat_n = int(SR * 0.04)
    hat = [rng.uniform(-1, 1) * math.exp(-i / (SR * 0.006)) for i in range(hat_n)]

    n_beats = int(total_n / (SR * beat)) + 1
    for b in range(n_beats):
        t = int(b * beat * SR)
        add(t, kick, 0.85)                      # every beat: kick pulse
        if b % 2 == 1:
            add(t, snare, 0.5)                  # backbeat snare
        for e in range(2):                      # 8th-note hats
            add(t + int(e * beat * SR / 2), hat, 0.18)
    return out


def normalize(sig, peak=0.95):
    m = max(1e-9, max(abs(s) for s in sig))
    return [s * peak / m for s in sig]


def write_wav(path, sig):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for s in sig:
            s = max(-1.0, min(0.99997, s))
            frames += struct.pack("<h", int(s * 32768))
        w.writeframes(bytes(frames))


def main():
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("host")
    out_dir.mkdir(parents=True, exist_ok=True)
    import random
    rng = random.Random(1976)  # fixed seed: same riff every run

    print("rendering guitar (Karplus-Strong)...")
    guitar = render_guitar(rng)
    print("rendering drums...")
    drums = render_drums(rng, len(guitar))
    mix = normalize([g * 0.8 + d for g, d in zip(guitar, drums)], 0.9)

    dur = len(mix) / SR
    write_wav(out_dir / "riff_clean.wav", mix)
    print(f"wrote {out_dir/'riff_clean.wav'} ({dur:.1f}s, mono 48 kHz, {len(RIFF)} 8ths)")


if __name__ == "__main__":
    main()
