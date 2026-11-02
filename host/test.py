#!/usr/bin/env python3
"""Host harness WAV test.

Checks:
  1. Preamp engagement and bounded output (0 dB EQ, 1 kHz input).
  2. Bass boost amplitude: 100 Hz sine with +12 dB bass should be louder
     than the same sine with 0 dB EQ (post-preamp).
  3. Generates eq_demo.wav (bass +8 dB, treble +6 dB) for listening.
"""

import math
import struct
import subprocess
import sys
import wave

SR = 48000
SECONDS = 1.0
AMP = 0.5


def gen_sine(path, freq, amp=AMP, seconds=SECONDS):
    n = int(SR * seconds)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for i in range(n):
            v = amp * math.sin(2 * math.pi * freq * i / SR)
            s = int(v * 32767)
            frames += struct.pack("<hh", s, s)
        w.writeframes(bytes(frames))


def run_harness(*args):
    cmd = ["./harness"] + list(args)
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
    return r.returncode


def read_stereo(path):
    with wave.open(path, "rb") as w:
        assert w.getnchannels() == 2 and w.getsampwidth() == 2
        raw = w.readframes(w.getnframes())
        n = len(raw) // 4
        return [struct.unpack_from("<hh", raw, 4 * i) for i in range(n)]


def peak_amplitude(frames):
    return max(abs(s) for frame in frames for s in frame) / 32768.0


def main():
    failures = 0

    # ---- Check 1: preamp engagement at 0 dB EQ ----
    gen_sine("sine.wav", freq=1000.0)
    if run_harness("sine.wav", "out.wav") != 0:
        print("FAIL: harness exited non-zero")
        return 1

    src = read_stereo("sine.wav")
    dst = read_stereo("out.wav")
    if len(src) != len(dst):
        print("FAIL: frame count changed (%d -> %d)" % (len(src), len(dst)))
        return 1

    diff = sum(1 for a, b in zip(src, dst) if a != b)
    if diff < len(src) // 2:
        print("FAIL: fewer than half the frames differ — preamp may not be engaged "
              "(%d/%d differ)" % (diff, len(src)))
        failures += 1

    max_amp = peak_amplitude(dst)
    if max_amp > 1.0:
        print("FAIL: output exceeds full scale (max_amp=%.4f)" % max_amp)
        failures += 1

    if max_amp < 0.5 or max_amp > 0.95:
        print("FAIL: output amplitude %.4f outside expected range [0.5, 0.95]"
              % max_amp)
        failures += 1

    if failures == 0:
        print("PASS: preamp engaged — %d/%d frames differ, max amp %.4f"
              % (diff, len(src), max_amp))

    # ---- Check 2: bass boost increases amplitude at 100 Hz ----
    # Use a low amplitude so preamp stays near linear (small-signal regime).
    gen_sine("bass_sine.wav", freq=100.0, amp=0.05)
    if run_harness("bass_sine.wav", "bass_flat.wav", "0", "0", "0") != 0:
        print("FAIL: harness (bass flat) exited non-zero")
        return 1
    if run_harness("bass_sine.wav", "bass_boost.wav", "12", "0", "0") != 0:
        print("FAIL: harness (bass boost) exited non-zero")
        return 1

    amp_flat  = peak_amplitude(read_stereo("bass_flat.wav"))
    amp_boost = peak_amplitude(read_stereo("bass_boost.wav"))
    if amp_boost <= amp_flat * 1.5:
        print("FAIL: +12 dB bass did not increase 100 Hz amplitude enough "
              "(flat=%.4f boost=%.4f)" % (amp_flat, amp_boost))
        failures += 1
    else:
        ratio_db = 20 * math.log10(amp_boost / amp_flat)
        print("PASS: bass boost — flat=%.4f boost=%.4f ratio=%.1f dB"
              % (amp_flat, amp_boost, ratio_db))

    # ---- Demo WAV: bass +8 dB, treble +6 dB (V-shape EQ) ----
    if run_harness("sine.wav", "eq_demo.wav", "8", "0", "6") != 0:
        print("FAIL: harness (eq_demo) exited non-zero")
        return 1
    print("eq_demo.wav: 1 kHz stereo sine, preamp + bass +8 dB treble +6 dB "
          "— listen and compare to out.wav (preamp only)")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
