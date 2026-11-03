#ifndef CAB_SIM_H
#define CAB_SIM_H

// Zuko (RP2350) — direct-form FIR cabinet simulator.
//
// Single-channel, float32, no allocation, ISR-safe.
// Caller owns CabSimState; zero-initialise once with {}.
//
// Same portability constraints as dsp.h: only <cmath>/<cstddef>/<cstdint>,
// no Arduino.h, no pico/ headers, no #ifdef.

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "cab_ir.h"

namespace zuko {

// Ring-buffer state for one channel's FIR delay line.
// taps = how many coefficients are actually used (1..kCabFirTaps). Set it via
// cab_set_taps(), which also computes the loudness compensation: a truncated
// peak-normalised IR loses DC gain (sum of the used coefficients), so the
// output is scaled back up — changing taps changes delay and tail, NOT volume.
struct CabSimState {
    float buf[kCabFirTaps] = {};
    int   head = 0;   // index of the oldest sample (write position)
    int   taps = kCabFirTaps;
    float comp = 1.0f;  // loudness compensation (sum_full / sum_used)
};

// Set the active tap count and compute the compensation factor (L2: equalises
// signal POWER for broadband input; DC-sum compensation over-boosts mids/HF
// because a lowpass IR carries its low-frequency response in the tail).
inline void cab_set_taps(CabSimState &state, int taps) {
    if (taps <= 0 || taps > kCabFirTaps) taps = kCabFirTaps;
    state.taps = taps;
    double full = 0.0, used = 0.0;
    for (int k = 0; k < kCabFirTaps; ++k) {
        full += double(kCabFirCoeffs[k]) * double(kCabFirCoeffs[k]);
        if (k < taps) used += double(kCabFirCoeffs[k]) * double(kCabFirCoeffs[k]);
    }
    state.comp = (used > 1e-18) ? float(std::sqrt(full / used)) : 1.0f;
}

// Process one sample through the cabinet FIR.
// h     — coefficient array of length kCabFirTaps, h[0] = most-recent tap.
// state — ring buffer carrying the delay line between calls; state.taps
//         selects how many of those coefficients are used.
inline float cab_sim_sample(float x, const float *h, CabSimState &state) {
    const int taps = (state.taps > 0 && state.taps <= kCabFirTaps)
                         ? state.taps : kCabFirTaps;
    // Write new sample into ring buffer at head, then advance head.
    state.buf[state.head] = x;
    int idx = state.head;
    state.head = (state.head + 1 == kCabFirTaps) ? 0 : state.head + 1;

    // Convolve: h[0]*x[n], h[1]*x[n-1], ...
    float acc = 0.0f;
    for (int k = 0; k < taps; ++k) {
        acc += h[k] * state.buf[idx];
        idx = (idx == 0) ? kCabFirTaps - 1 : idx - 1;
    }
    return acc * state.comp;
}

// Apply cab_sim_sample in-place over n samples.
inline void cab_sim_block(float *buf, size_t n,
                          const float *h, CabSimState &state) {
    for (size_t i = 0; i < n; ++i)
        buf[i] = cab_sim_sample(buf[i], h, state);
}

}  // namespace zuko

#endif  // CAB_SIM_H
