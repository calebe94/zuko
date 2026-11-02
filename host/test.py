#!/usr/bin/env python3
"""Host harness WAV test: generate a sine, run it through the host harness
(zuko::process_block with preamp stage), and verify preamp properties.
out.wav is the preamp-processed WAV — listen to it to hear the saturation."""

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

    # Preamp must change the signal: expect most frames to differ from input
    diff = sum(1 for a, b in zip(src, dst) if a != b)
    if diff < len(src) // 2:
        print("FAIL: fewer than half the frames differ — preamp may not be engaged "
              "(%d/%d differ)" % (diff, len(src)))
        return 1

    # tanh is bounded: output must never exceed full scale
    max_amp = max(abs(s) for frame in dst for s in frame) / 32768.0
    if max_amp > 1.0:
        print("FAIL: output exceeds full scale (max_amp=%.4f)" % max_amp)
        return 1

    # With drive=4.0, level=0.85, a 0.5-amplitude sine peaks at
    # 0.85 * tanh(4.0 * 0.5) ≈ 0.819 — check we are in that ballpark
    if max_amp < 0.5 or max_amp > 0.95:
        print("FAIL: output amplitude %.4f outside expected range [0.5, 0.95]" % max_amp)
        return 1

    print("PASS: preamp engaged — %d/%d frames differ, max amp %.4f "
          "(out.wav = preamp demo, listen to hear saturation)" % (diff, len(src), max_amp))
    return 0


if __name__ == "__main__":
    sys.exit(main())
