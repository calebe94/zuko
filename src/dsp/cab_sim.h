#ifndef CAB_SIM_H
#define CAB_SIM_H

// Zuko (RP2350) — direct-form FIR cabinet simulator.
//
// Single-channel, float32, no allocation, ISR-safe.
// Caller owns CabSimState; zero-initialise once with {}.
//
// Same portability constraints as dsp.h: only <cmath>/<cstddef>/<cstdint>,
// no Arduino.h, no pico/ headers, no #ifdef.

#include <cstddef>
#include <cstdint>

#include "cab_ir.h"

namespace zuko {

// Ring-buffer state for one channel's FIR delay line.
// n_taps must equal kCabFirTaps (the template keeps the size at compile time).
struct CabSimState {
    float buf[kCabFirTaps] = {};
    int   head = 0;  // index of the oldest sample (write position)
};

// Process one sample through the cabinet FIR.
// h     — coefficient array of length kCabFirTaps, h[0] = most-recent tap.
// state — ring buffer carrying the delay line between calls.
inline float cab_sim_sample(float x, const float *h, CabSimState &state) {
    // Write new sample into ring buffer at head, then advance head.
    state.buf[state.head] = x;
    int idx = state.head;
    state.head = (state.head + 1 == kCabFirTaps) ? 0 : state.head + 1;

    // Convolve: h[0]*x[n], h[1]*x[n-1], ...
    float acc = 0.0f;
    for (int k = 0; k < kCabFirTaps; ++k) {
        acc += h[k] * state.buf[idx];
        idx = (idx == 0) ? kCabFirTaps - 1 : idx - 1;
    }
    return acc;
}

// Apply cab_sim_sample in-place over n samples.
inline void cab_sim_block(float *buf, size_t n,
                          const float *h, CabSimState &state) {
    for (size_t i = 0; i < n; ++i)
        buf[i] = cab_sim_sample(buf[i], h, state);
}

}  // namespace zuko

#endif  // CAB_SIM_H
