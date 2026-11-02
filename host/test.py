#!/usr/bin/env python3
"""Host harness WAV test: generate a sine, run it through the host harness
(zuko::process_block), and check the Phase 1 pass-through is bit-exact."""

import math
import struct
import subprocess
import sys
import wave

SR = 48000
SECONDS = 1.0
FREQ = 1000.0
AMP = 0.5


def gen_sine(path):
    n = int(SR * SECONDS)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for i in range(n):
            v = AMP * math.sin(2 * math.pi * FREQ * i / SR)
            s = int(v * 32767)
            frames += struct.pack("<hh", s, s)
        w.writeframes(bytes(frames))


def read_stereo(path):
    with wave.open(path, "rb") as w:
        assert w.getnchannels() == 2 and w.getsampwidth() == 2
        raw = w.readframes(w.getnframes())
        n = len(raw) // 4
        return [struct.unpack_from("<hh", raw, 4 * i) for i in range(n)]


def main():
    gen_sine("sine.wav")
    r = subprocess.run(["./harness", "sine.wav", "out.wav"],
                       capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        print("FAIL: harness exited %d" % r.returncode)
        return 1

    src = read_stereo("sine.wav")
    dst = read_stereo("out.wav")
    if len(src) != len(dst):
        print("FAIL: frame count changed (%d -> %d)" % (len(src), len(dst)))
        return 1

    diff = sum(1 for a, b in zip(src, dst) if a != b)
    if diff:
        print("FAIL: %d/%d frames differ (pass-through must be bit-exact)" % (diff, len(src)))
        return 1

    print("PASS: pass-through is bit-exact over %d stereo frames" % len(src))
    return 0


if __name__ == "__main__":
    sys.exit(main())
