#ifndef JCM800_H
#define JCM800_H

// Zuko (RP2350) — JCM800-style analytical preamp model.
//
// Topology (per channel):
//   input → headroom pad (input scale, loudness-compensated at the output)
//         → Stage 1: drive → asymmetric tanh clip
//         → coupling highpass (~720 Hz, 1st-order bilinear)
//         → [sag envelope follower on the stage-2 input, modulating drive2]
//         → Stage 2: drive → asymmetric tanh clip
//         → presence high-shelf (~4.5 kHz, RBJ)
//         → master scalar → headroom loudness compensation
//
// Dynamics controls:
//   headroom — pads the input before stage 1 and compensates the level after
//       the master. With headroom < 1 the tanh knee sits closer to the
//       guitar's dynamic range, so soft picking cleans up while hard picking
//       still saturates (the operating point moves toward the knee instead of
//       far past it). 1.0 = no pad (legacy behaviour).
//   sag — peak envelope follower (fast attack, slow release) on the stage-2
//       input; a loud signal temporarily REDUCES drive2 (tube-sag style), so
//       transients keep their bite while the sustained part opens up. 0 = off.
//
// Same portability constraints as dsp.h: only <cmath>/<cstddef>, no Arduino,
// no pico headers, no #ifdef. Per-call state owned by caller (ISR-safe, no
// allocation). Coefficients computed once by jcm800_init().

#include <cmath>
#include <cstddef>

#include "tone_stack.h"  // BiquadCoeffs, BiquadState, biquad_sample, detail::rbj_high_shelf

namespace zuko {

// Envelope reference for the sag modulator, in stage-2 input amplitude.
// The envelope is normalised as min(env / kSagEnvRef, 1): at and above this
// level the sag is at full depth. Stage-1 output is tanh-bounded to <= 1 and
// the 720 Hz coupling highpass cuts most of the fundamental, so a loud
// passage typically lands the stage-2 input envelope around 0.1-0.3.
constexpr float kSagEnvRef = 0.2f;

// Maximum loudness compensation applied for the headroom pad (+18 dB).
// Prevents extreme pads (e.g. headroom = 5) from demanding an insane output
// boost after the tanh. If the output clips, lower master instead.
constexpr float kHeadroomOutCap = 8.0f;

// Per-channel state. Zero-initialise once: Jcm800State st{}.
struct Jcm800State {
    float hp_x_prev = 0.f;  // x[n-1] for inter-stage highpass
    float hp_y_prev = 0.f;  // y[n-1] for inter-stage highpass
    BiquadState presence{};
    float sag_env   = 0.f;  // sag peak-follower memory (stage-2 input)
};

// Coefficients computed once by jcm800_init(). Immutable during processing.
struct Jcm800Coeffs {
    float drive1        = 0.f;  // stage 1 input drive
    float drive2        = 0.f;  // stage 2 input drive (modulated by sag)
    float hp_b0         = 0.f;  // inter-stage highpass numerator (b0 = -b1)
    float hp_pole       = 0.f;  // inter-stage highpass feedback pole coefficient
    float master        = 0.f;  // post-presence output scale
    float headroom_in   = 1.f;  // input pad before stage 1
    float headroom_out  = 1.f;  // loudness compensation after master (capped)
    float sag_depth     = 0.f;  // 0 = off; 1 = drive2 drops to zero at full envelope
    float sag_a_att     = 0.f;  // envelope one-pole attack coefficient
    float sag_a_rel     = 0.f;  // envelope one-pole release coefficient
    float sag_env_scale = 1.f / kSagEnvRef;  // env -> normalised 0..1
    BiquadCoeffs presence{};
};

// Compute JCM800 coefficients.
//   fs       — sample rate (Hz)
//   gain     [0,1] — preamp drive (0 = mild, 1 = high-gain)
//   presence [0,1] — presence shelf amount (0 = flat, 1 = +12 dB @ 4.5 kHz)
//   master   [0,1] — output level (1.0 ≈ generic preamp peak level)
//   headroom [0.05,1] — input pad before stage 1 (1 = off, lower = more
//              picking dynamics); loudness-compensated at the output up to
//              kHeadroomOutCap.
//   sag      [0,1] — envelope-driven drive2 reduction depth (0 = off)
//   sag_attack_ms / sag_release_ms — envelope ballistics (clamped 1..1000 ms)
inline Jcm800Coeffs jcm800_init(float fs,
                                 float gain     = 0.7f,
                                 float presence = 0.5f,
                                 float master   = 0.8f,
                                 float headroom = 1.0f,
                                 float sag      = 0.0f,
                                 float sag_attack_ms  = 10.0f,
                                 float sag_release_ms = 250.0f) {
    const float kPi = 3.14159265358979323846f;
    // drive 2..8 per stage
    const float d = 2.0f + gain * 6.0f;

    // Headroom pad: clamp to a sane range, compensate at the output (capped).
    const float hr_in  = headroom < 0.05f ? 0.05f : (headroom > 1.0f ? 1.0f : headroom);
    const float hr_out = (1.0f / hr_in > kHeadroomOutCap) ? kHeadroomOutCap : 1.0f / hr_in;

    // Sag ballistics: one-pole coefficient per sample, a = 1 - exp(-1/(tau*fs)).
    const float att_ms = sag_attack_ms  < 1.f ? 1.f : (sag_attack_ms  > 1000.f ? 1000.f : sag_attack_ms);
    const float rel_ms = sag_release_ms < 1.f ? 1.f : (sag_release_ms > 1000.f ? 1000.f : sag_release_ms);
    const float a_att = 1.0f - std::exp(-1000.0f / (att_ms * fs));
    const float a_rel = 1.0f - std::exp(-1000.0f / (rel_ms * fs));
    const float depth = sag < 0.0f ? 0.0f : (sag > 1.0f ? 1.0f : sag);

    // Inter-stage coupling highpass, bilinear transform, fc ≈ 720 Hz.
    // H(z) = b0 * (1 - z^-1) / (1 - hp_pole * z^-1),  b0 = 1/(1+K)
    // y[n] = b0*x[n] - b0*x[n-1] + hp_pole*y[n-1]
    const float K     = std::tan(kPi * 720.0f / fs);
    const float b0    = 1.0f / (1.0f + K);
    const float pole  = (1.0f - K) / (1.0f + K);  // ~ 0.91 @ 48 kHz

    // Presence: RBJ high-shelf, fc = 4500 Hz, 0..+12 dB
    const float pres_db = presence * 12.0f;
    BiquadCoeffs pres   = detail::rbj_high_shelf(fs, 4500.0f, pres_db);

    Jcm800Coeffs c;
    c.drive1 = d;
    c.drive2 = d;
    c.hp_b0 = b0;
    c.hp_pole = pole;
    c.master = master * 0.85f;
    c.headroom_in = hr_in;
    c.headroom_out = hr_out;
    c.sag_depth = depth;
    c.sag_a_att = a_att;
    c.sag_a_rel = a_rel;
    c.presence = pres;
    return c;
}

// 12AX7-style asymmetric soft-clip.
// Positive half clips harder (plate toward cutoff); negative softer.
// No per-drive gain compensation: higher drive → more output + more saturation.
inline float jcm_clip(float x, float drive) {
    if (x >= 0.0f)
        return std::tanh(drive * x * 1.1f);
    else
        return std::tanh(drive * x * 0.9f);
}

// Process one sample through the full JCM800 preamp chain.
inline float jcm800_sample(float x, const Jcm800Coeffs &c, Jcm800State &st) {
    // Headroom pad
    x *= c.headroom_in;

    // Stage 1
    float s1 = jcm_clip(x, c.drive1);

    // Inter-stage coupling highpass (~720 Hz)
    float hp = c.hp_b0 * s1 - c.hp_b0 * st.hp_x_prev + c.hp_pole * st.hp_y_prev;
    st.hp_x_prev = s1;
    st.hp_y_prev = hp;

    // Sag: peak envelope follower on |hp|; loud signal reduces drive2.
    float drive2 = c.drive2;
    if (c.sag_depth > 0.0f) {
        const float ax = std::fabs(hp);
        const float a  = ax > st.sag_env ? c.sag_a_att : c.sag_a_rel;
        st.sag_env += a * (ax - st.sag_env);
        float env_norm = st.sag_env * c.sag_env_scale;
        if (env_norm > 1.0f) env_norm = 1.0f;
        drive2 = c.drive2 * (1.0f - c.sag_depth * env_norm);
    }

    // Stage 2
    float s2 = jcm_clip(hp, drive2);

    // Presence shelf
    float out = biquad_sample(s2, c.presence, st.presence);

    return out * c.master * c.headroom_out;
}

// Apply jcm800_sample in-place over n samples.
inline void jcm800_block(float *buf, size_t n, const Jcm800Coeffs &c, Jcm800State &st) {
    for (size_t i = 0; i < n; ++i)
        buf[i] = jcm800_sample(buf[i], c, st);
}

}  // namespace zuko

#endif  // JCM800_H
