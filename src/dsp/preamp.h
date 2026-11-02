#ifndef PREAMP_H
#define PREAMP_H

// Zuko (RP2350) — preamp stage.
//
// Same portability constraints as dsp.h: only <cmath>/<cstddef>/<cstdint>,
// no Arduino.h, no pico/ headers, no #ifdef.

#include <cmath>
#include <cstddef>

namespace zuko {

// Default preamp parameters used by process_block.
constexpr float kPreampDrive = 4.0f;   // input gain before the waveshaper
constexpr float kPreampLevel = 0.85f;  // post-waveshaper output gain

// Transfer: y = level * tanh(drive * x).
// Analytically bounded: |y| < level for all finite x.
// Small-signal linear gain = level * drive (≈ 3.4 at the defaults).
inline float preamp_sample(float x, float drive, float level) {
    return level * std::tanh(drive * x);
}

// Apply preamp_sample in-place over n samples.
inline void preamp_block(float *buf, size_t n, float drive, float level) {
    for (size_t i = 0; i < n; ++i)
        buf[i] = preamp_sample(buf[i], drive, level);
}

}  // namespace zuko

#endif  // PREAMP_H
