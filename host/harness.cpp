// Zuko (RP2350) — host-native DSP harness.
//
// Compiles the SAME sources as the firmware (src/dsp/dsp.h) natively on the
// Mac. Two modes:
//
//   ./harness in.wav out.wav   run zuko::process_block over a 16-bit WAV
//   ./harness --selftest       check waveshape/fir_at against double-precision
//                              references computed here
//
// No hardware, no Arduino, no Pico SDK — this is the Phase 2 prerequisite:
// the DSP can be validated on the host before it ever runs on the board.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include "dsp/dsp.h"
#include "ini_map.h"

namespace {

struct Wav {
    uint16_t channels;
    uint32_t sample_rate;
    std::vector<int16_t> samples;  // interleaved
};

uint32_t rd32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}
uint16_t rd16(const uint8_t *p) {
    return uint16_t(p[0] | (p[1] << 8));
}

bool read_wav(const std::string &path, Wav *out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(size);
    if (fread(buf.data(), 1, size, f) != size_t(size)) { fclose(f); return false; }
    fclose(f);

    if (size < 44 || memcmp(buf.data(), "RIFF", 4) || memcmp(buf.data() + 8, "WAVE", 4))
        return false;

    // Walk chunks so we tolerate odd extra chunks (LIST etc.).
    long pos = 12;
    bool have_fmt = false, have_data = false;
    uint16_t channels = 0, bits = 0;
    uint32_t rate = 0;
    while (pos + 8 <= size) {
        const uint8_t *id = buf.data() + pos;
        uint32_t len = rd32(buf.data() + pos + 4);
        if (pos + 8 + long(len) > size) return false;
        if (!memcmp(id, "fmt ", 4)) {
            const uint8_t *p = buf.data() + pos + 8;
            channels = rd16(p + 2);
            rate = rd32(p + 4);
            uint16_t audio_fmt = rd16(p);
            bits = rd16(p + 14);
            if (audio_fmt != 1) return false;  // PCM only
            have_fmt = true;
        } else if (!memcmp(id, "data", 4)) {
            if (!have_fmt) return false;
            if (bits != 16) return false;
            size_t n = len / 2;
            out->samples.resize(n);
            memcpy(out->samples.data(), buf.data() + pos + 8, len);
            out->channels = channels;
            out->sample_rate = rate;
            have_data = true;
        }
        pos += 8 + long(len) + (len & 1);
    }
    return have_fmt && have_data;
}

bool write_wav(const std::string &path, const Wav &wav) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t data_len = uint32_t(wav.samples.size() * 2);
    uint32_t riff_len = 36 + data_len;
    uint8_t hdr[44];
    memcpy(hdr, "RIFF", 4); memcpy(hdr + 4, &riff_len, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    uint32_t fmt_len = 16; memcpy(hdr + 16, &fmt_len, 4);
    uint16_t fmt_pcm = 1; memcpy(hdr + 20, &fmt_pcm, 2);
    memcpy(hdr + 22, &wav.channels, 2);
    memcpy(hdr + 24, &wav.sample_rate, 4);
    uint32_t byte_rate = wav.sample_rate * wav.channels * 2; memcpy(hdr + 28, &byte_rate, 4);
    uint16_t align = wav.channels * 2; memcpy(hdr + 32, &align, 2);
    uint16_t bits = 16; memcpy(hdr + 34, &bits, 2);
    memcpy(hdr + 36, "data", 4); memcpy(hdr + 40, &data_len, 4);
    bool ok = fwrite(hdr, 1, 44, f) == 44 &&
              fwrite(wav.samples.data(), 2, wav.samples.size(), f) == wav.samples.size();
    fclose(f);
    return ok;
}

// (INI parsing moved to ini_map.h, shared with rt.)

int selftest() {
    int failures = 0;

    // waveshape vs double-precision reference. tanhf differs from tanh by a
    // few ULPs, so the tolerance is a few float ULPs of the result, not zero.
    for (double x = -1.0; x <= 1.0; x += 0.05) {
        float got = zuko::waveshape(float(x), 4.0f, 0.1f);
        double ref = std::tanh(4.0 * (x + 0.1)) - std::tanh(4.0 * 0.1);
        double tol = 8.0 * std::abs(double(got)) * 1.19e-7;  // ~8 ULP, scaled to magnitude
        if (std::abs(got - ref) > (tol < 1e-7 ? 1e-7 : tol)) {
            printf("FAIL waveshape(x=%.3f): got %.9g want %.9g\n", x, got, ref);
            ++failures;
        }
    }

    // FIR moving average on a ramp: exact float answer at n=40 is 24.5.
    float x[64], h[zuko::kFirTaps];
    for (int i = 0; i < 64; ++i) x[i] = float(i);
    for (int k = 0; k < zuko::kFirTaps; ++k) h[k] = 1.0f / zuko::kFirTaps;
    float y = zuko::fir_at(x, h, 40, zuko::kFirTaps);
    if (std::abs(y - 24.5f) > 1e-4f) {
        printf("FAIL fir_at: got %.9g want 24.5\n", y);
        ++failures;
    }

    // preamp_sample vs double-precision reference
    {
        const float drive = zuko::kPreampDrive;
        const float level = zuko::kPreampLevel;
        for (double xv = -1.0; xv <= 1.0; xv += 0.05) {
            float got = zuko::preamp_sample(float(xv), drive, level);
            double ref = double(level) * std::tanh(double(drive) * xv);
            double tol = 8.0 * std::abs(double(got)) * 1.19e-7;
            if (tol < 1e-7) tol = 1e-7;
            if (std::abs(got - ref) > tol) {
                printf("FAIL preamp_sample(x=%.3f): got %.9g want %.9g\n", xv, got, ref);
                ++failures;
            }
        }
        // Saturation: at large input the output must approach level to within 0.1%
        float sat = zuko::preamp_sample(10.0f, drive, level);
        if (std::abs(sat - level) > 0.001f) {
            printf("FAIL preamp saturation: got %.9g, want near %.9g\n",
                   double(sat), double(level));
            ++failures;
        }
    }

    // process_block stability: preamp → tone stack → cab sim must be bounded.
    // Preamp clamps to ±kPreampLevel; the FIR is linear so it can amplify,
    // but the cab IR is normalised to unit peak (DC sum < 1 for a typical LP).
    // Bound: allow up to 2× kPreampLevel to be safe.
    {
        const float kFs = 48000.f;
        zuko::ToneStackCoeffs unity = zuko::tone_stack_init(kFs, 0.f, 0.f, 0.f);
        zuko::DspState state{};
        const float bound = 2.0f * zuko::kPreampLevel;
        bool bounded = true;
        for (int blk = 0; blk < 64 && bounded; ++blk) {
            float L[32], R[32];
            for (int i = 0; i < 32; ++i) {
                float t = float(blk * 32 + i) / float(64 * 32 - 1);
                L[i] = t < 0.5f ? (2.f * t - 1.f) : (1.f - 2.f * (t - 0.5f));
                R[i] = -L[i];
            }
            zuko::process_block(L, R, 32, state, unity);
            for (int i = 0; i < 32 && bounded; ++i) {
                if (std::abs(L[i]) > bound || std::abs(R[i]) > bound) {
                    printf("FAIL process_block stability: |L[%d]|=%.9g or |R[%d]|=%.9g "
                           "exceeded %.4g\n",
                           blk*32+i, double(std::abs(L[i])),
                           blk*32+i, double(std::abs(R[i])), double(bound));
                    bounded = false;
                }
            }
        }
        if (!bounded) ++failures;
    }

    // ---- Tone stack self-tests ----
    const float kFs = 48000.f;
    static constexpr float kTsPi = 3.14159265358979323846f;

    // Test 1: unity gain — all bands at 0 dB must pass signal unchanged.
    {
        const int N = 2048, SETTLE = 512;
        const float f = 1000.f, A = 0.5f;
        zuko::ToneStackCoeffs c = zuko::tone_stack_init(kFs, 0.f, 0.f, 0.f);
        zuko::ToneStackState  st{};
        float peak = 0.f;
        for (int i = 0; i < N; ++i) {
            float xs = A * std::sin(2.f * kTsPi * f * i / kFs);
            xs = zuko::biquad_sample(xs, c.bass,   st.bass);
            xs = zuko::biquad_sample(xs, c.mid,    st.mid);
            xs = zuko::biquad_sample(xs, c.treble, st.treble);
            if (i >= SETTLE && std::abs(xs) > peak) peak = std::abs(xs);
        }
        const float tol = A * 0.005f;
        if (std::abs(peak - A) > tol) {
            printf("FAIL tone_stack unity: steady-state peak=%.9g want=%.9g "
                   "(tol=%.9g)\n", double(peak), double(A), double(tol));
            ++failures;
        }
    }

    // Test 2: +12 dB mid boost at 800 Hz must measure within 0.5 dB of 12 dB.
    {
        const int N = 4096, SETTLE = 1024;
        const float f = 800.f, A = 0.1f;
        zuko::ToneStackCoeffs c = zuko::tone_stack_init(kFs, 0.f, 12.f, 0.f);
        zuko::ToneStackState  st{};
        float peak = 0.f;
        for (int i = 0; i < N; ++i) {
            float xs = A * std::sin(2.f * kTsPi * f * i / kFs);
            xs = zuko::biquad_sample(xs, c.bass,   st.bass);
            xs = zuko::biquad_sample(xs, c.mid,    st.mid);
            xs = zuko::biquad_sample(xs, c.treble, st.treble);
            if (i >= SETTLE && std::abs(xs) > peak) peak = std::abs(xs);
        }
        const double measured_db = 20.0 * std::log10(double(peak) / double(A));
        if (std::abs(measured_db - 12.0) > 0.5) {
            printf("FAIL tone_stack +12dB mid@800Hz: measured %.4f dB, "
                   "want 12.0 ± 0.5 dB\n", measured_db);
            ++failures;
        }
    }

    // Test 3: stability — bounded output for a ±1 full-scale triangle sweep.
    {
        const int N = 8192;
        zuko::ToneStackCoeffs c = zuko::tone_stack_init(kFs, 0.f, 0.f, 0.f);
        zuko::DspState state{};
        float L[32], R[32];
        bool bounded = true;
        for (int off = 0; off < N && bounded; off += 32) {
            for (int i = 0; i < 32; ++i) {
                float t = float(off + i) / float(N - 1);
                L[i] = t < 0.5f ? (2.f * t - 1.f) : (1.f - 2.f * (t - 0.5f));
                R[i] = -L[i];
            }
            zuko::process_block(L, R, 32, state, c);
            for (int i = 0; i < 32; ++i) {
                if (std::abs(L[i]) > 1.1f || std::abs(R[i]) > 1.1f) {
                    printf("FAIL tone_stack stability: |L[%d]|=%.9g or |R[%d]|=%.9g "
                           "exceeded 1.1\n", off + i, double(std::abs(L[i])),
                           off + i, double(std::abs(R[i])));
                    bounded = false;
                    break;
                }
            }
        }
        if (!bounded) ++failures;
    }

    // ---- Cab sim self-tests ----

    // Test 1: impulse response
    {
        zuko::CabSimState cs{};
        const int CHECK = 8;
        bool ok = true;
        for (int n = 0; n < zuko::kCabFirTaps + CHECK; ++n) {
            float in = (n == 0) ? 1.0f : 0.0f;
            float out = zuko::cab_sim_sample(in, zuko::kCabFirCoeffs, cs);
            if (n < zuko::kCabFirTaps) {
                float want = zuko::kCabFirCoeffs[n];
                if (out != want) {
                    printf("FAIL cab_sim impulse[%d]: got %.9g want %.9g\n",
                           n, double(out), double(want));
                    ok = false;
                    if (n >= CHECK) break;
                }
            }
        }
        if (!ok) ++failures;
    }

    // Test 2: DC gain
    {
        double dc_sum = 0.0;
        for (int k = 0; k < zuko::kCabFirTaps; ++k)
            dc_sum += double(zuko::kCabFirCoeffs[k]);

        zuko::CabSimState cs{};
        const int SETTLE = zuko::kCabFirTaps + 512;
        float last = 0.0f;
        for (int n = 0; n < SETTLE; ++n)
            last = zuko::cab_sim_sample(1.0f, zuko::kCabFirCoeffs, cs);

        double gain_db = 20.0 * std::log10(std::abs(double(last) / dc_sum));
        if (std::abs(gain_db) > 0.1) {
            printf("FAIL cab_sim DC gain: steady-state=%.9g dc_sum=%.9g "
                   "error=%.4f dB (want < 0.1 dB)\n",
                   double(last), dc_sum, gain_db);
            ++failures;
        }
    }

    // Test 3: CPU cost estimate (HOST ONLY, wall-clock).
    {
        const int ITER = 1 << 18;
        zuko::CabSimState cs{};
        volatile float sink = 0.0f;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITER; ++i)
            sink = zuko::cab_sim_sample(0.5f, zuko::kCabFirCoeffs, cs);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        (void)sink;

        double elapsed_s = double(t1.tv_sec  - t0.tv_sec) +
                           double(t1.tv_nsec - t0.tv_nsec) * 1e-9;
        double ns_per_sample = elapsed_s / ITER * 1e9;
        double est_cycles_per_sample = ns_per_sample * 245.76e6 / 1e9;
        double est_cycles_per_block  = est_cycles_per_sample * 32.0;
        printf("[cab_sim timing ESTIMATE — host clang, NOT silicon]\n"
               "  %.1f ns/sample  =>  %.0f cycles/sample @ 245.76 MHz (estimate)\n"
               "  %.0f cycles/32-sample block (estimate, budget 131072)\n",
               ns_per_sample, est_cycles_per_sample, est_cycles_per_block);
    }

    // ---- JCM800 self-tests ----
    {
        const float kPi = 3.14159265358979323846f;

        // JCM800 Test 1: stability — full-scale ±1 triangle sweep, presence=0.
        // With tanh saturation, output is bounded by ±1 before master; after
        // master=0.85 through unity EQ and cab LP the output must stay < 2.0.
        {
            const float bound = 2.0f;
            zuko::Jcm800Coeffs jc = zuko::jcm800_init(kFs, 1.0f, 0.0f, 1.0f);
            zuko::ToneStackCoeffs unity = zuko::tone_stack_init(kFs, 0.f, 0.f, 0.f);
            zuko::AmpConfig amp;
            amp.model      = zuko::AmpModel::kJcm800;
            amp.jcm_coeffs = jc;
            zuko::DspState state{};
            bool bounded = true;
            for (int blk = 0; blk < 64 && bounded; ++blk) {
                float L[32], R[32];
                for (int i = 0; i < 32; ++i) {
                    float t = float(blk * 32 + i) / float(64 * 32 - 1);
                    L[i] = t < 0.5f ? (2.f * t - 1.f) : (1.f - 2.f * (t - 0.5f));
                    R[i] = -L[i];
                }
                zuko::process_block(L, R, 32, state, unity, amp);
                for (int i = 0; i < 32 && bounded; ++i) {
                    if (std::abs(L[i]) > bound || std::abs(R[i]) > bound) {
                        printf("FAIL jcm800 stability: |L[%d]|=%.9g or |R[%d]|=%.9g "
                               "exceeded %.4g\n",
                               blk*32+i, double(std::abs(L[i])),
                               blk*32+i, double(std::abs(R[i])), double(bound));
                        bounded = false;
                    }
                }
            }
            if (!bounded) ++failures;
        }

        // JCM800 Test 2: gain monotonicity.
        // 1000 Hz sine at amplitude 0.05 through jcm800_sample (presence=0, master=0.85).
        // 5 gain points: 0.2, 0.4, 0.6, 0.8, 1.0 — RMS must be strictly increasing.
        {
            const float A = 0.05f;
            const float f = 1000.0f;
            const int N = 4096, SETTLE = 1024;
            const float gains[5] = { 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };
            float prev_rms = -1.0f;
            bool mono = true;
            for (int g = 0; g < 5 && mono; ++g) {
                zuko::Jcm800Coeffs jc = zuko::jcm800_init(kFs, gains[g], 0.0f, 1.0f);
                zuko::Jcm800State st{};
                double sum_sq = 0.0;
                for (int i = 0; i < N; ++i) {
                    float xs = A * std::sin(2.f * kPi * f * i / kFs);
                    float ys = zuko::jcm800_sample(xs, jc, st);
                    if (i >= SETTLE) sum_sq += double(ys) * double(ys);
                }
                float rms = float(std::sqrt(sum_sq / (N - SETTLE)));
                if (rms <= prev_rms) {
                    printf("FAIL jcm800 gain monotonicity: gain=%.1f RMS=%.6g "
                           "not > prev RMS=%.6g\n",
                           double(gains[g]), double(rms), double(prev_rms));
                    mono = false;
                }
                prev_rms = rms;
            }
            if (!mono) ++failures;
        }

        // JCM800 Test 3: inter-stage coupling highpass.
        // At small signal (near-linear), 50 Hz should be significantly more
        // attenuated than 440 Hz. 1st-order HP at 720 Hz: theoretical ratio
        // ≈ −17.5 dB. Require measured ratio < −10 dB.
        {
            const float A = 0.001f;
            const int N = 8192, SETTLE = 2048;
            zuko::Jcm800Coeffs jc = zuko::jcm800_init(kFs, 0.1f, 0.0f, 1.0f);
            double rms50 = 0.0, rms440 = 0.0;
            {
                zuko::Jcm800State st{};
                for (int i = 0; i < N; ++i) {
                    float xs = A * std::sin(2.f * kPi * 50.f * i / kFs);
                    float ys = zuko::jcm800_sample(xs, jc, st);
                    if (i >= SETTLE) rms50 += double(ys) * double(ys);
                }
                rms50 = std::sqrt(rms50 / (N - SETTLE));
            }
            {
                zuko::Jcm800State st{};
                for (int i = 0; i < N; ++i) {
                    float xs = A * std::sin(2.f * kPi * 440.f * i / kFs);
                    float ys = zuko::jcm800_sample(xs, jc, st);
                    if (i >= SETTLE) rms440 += double(ys) * double(ys);
                }
                rms440 = std::sqrt(rms440 / (N - SETTLE));
            }
            double ratio_db = 20.0 * std::log10(rms50 / rms440);
            if (ratio_db > -10.0) {
                printf("FAIL jcm800 HP coupling: 50Hz/440Hz ratio=%.2f dB, "
                       "want < -10 dB\n", ratio_db);
                ++failures;
            } else {
                printf("[jcm800 HP coupling: 50Hz/440Hz = %.2f dB (want < -10 dB)]\n",
                       ratio_db);
            }
        }

        // JCM800 Test 4: headroom/sag regression guard. With the defaults
        // (headroom = 1, sag = 0) jcm800_sample must reproduce the legacy
        // chain (two tanh stages around the coupling highpass) exactly.
        {
            const float drive = 2.0f + 0.7f * 6.0f;
            zuko::Jcm800Coeffs jc = zuko::jcm800_init(kFs, 0.7f, 0.5f, 0.8f);
            zuko::Jcm800State st{};
            zuko::BiquadState ref_presence{};
            float ref_hp_x = 0.f, ref_hp_y = 0.f;
            bool ok = true;
            for (int i = 0; i < 4096 && ok; ++i) {
                const float xs = 0.3f * std::sin(2.f * kPi * 440.f * i / kFs)
                               + 0.1f * std::sin(2.f * kPi * 123.f * i / kFs);
                const float got = zuko::jcm800_sample(xs, jc, st);
                // Legacy reference: the pre-headroom/sag topology.
                const float s1 = zuko::jcm_clip(xs, drive);
                const float hp = jc.hp_b0 * s1 - jc.hp_b0 * ref_hp_x
                               + jc.hp_pole * ref_hp_y;
                ref_hp_x = s1;
                ref_hp_y = hp;
                const float s2 = zuko::jcm_clip(hp, drive);
                const float ref = zuko::biquad_sample(s2, jc.presence, ref_presence)
                                * jc.master;
                if (std::abs(got - ref) > 1e-6f) {
                    printf("FAIL jcm800 legacy identity: i=%d got %.9g want %.9g\n",
                           i, double(got), double(ref));
                    ok = false;
                }
            }
            if (!ok) ++failures;
        }

        // JCM800 Test 5: headroom preserves picking dynamics. The RMS ratio
        // of a loud (0.4) vs soft (0.05) 440 Hz sine must improve by well
        // over 2x with the pad engaged (host ESTIMATE of perceived dynamics).
        {
            const int N = 4096, SETTLE = 1024;
            auto rms_at = [&](float headroom, float amp) {
                zuko::Jcm800Coeffs jc = zuko::jcm800_init(kFs, 0.7f, 0.0f, 1.0f, headroom);
                zuko::Jcm800State st{};
                double ss = 0.0;
                for (int i = 0; i < N; ++i) {
                    const float xs = amp * std::sin(2.f * kPi * 440.f * i / kFs);
                    const float ys = zuko::jcm800_sample(xs, jc, st);
                    if (i >= SETTLE) ss += double(ys) * double(ys);
                }
                return float(std::sqrt(ss / (N - SETTLE)));
            };
            const float r_off = rms_at(1.0f, 0.40f) / rms_at(1.0f, 0.05f);
            const float r_on  = rms_at(0.20f, 0.40f) / rms_at(0.20f, 0.05f);
            if (!(r_on > 2.0f * r_off) || !(r_on > 1.5f)) {
                printf("FAIL jcm800 headroom dynamics: ratio off=%.3f on=%.3f "
                       "(want on > 2x off and > 1.5)\n",
                       double(r_off), double(r_on));
                ++failures;
            } else {
                printf("[jcm800 headroom dynamics: loud/soft RMS ratio "
                       "off=%.3f on=%.3f (host ESTIMATE)]\n",
                       double(r_off), double(r_on));
            }
        }

        // JCM800 Test 6: sag reduces the sustained level of a loud note
        // (drive2 drops while the envelope is high).
        {
            const int N = 8192, SETTLE = 2048;
            auto loud_rms = [&](float sag) {
                zuko::Jcm800Coeffs jc = zuko::jcm800_init(kFs, 0.7f, 0.0f, 1.0f,
                                                          0.3f, sag);
                zuko::Jcm800State st{};
                double ss = 0.0;
                for (int i = 0; i < N; ++i) {
                    const float xs = 0.4f * std::sin(2.f * kPi * 440.f * i / kFs);
                    const float ys = zuko::jcm800_sample(xs, jc, st);
                    if (i >= SETTLE) ss += double(ys) * double(ys);
                }
                return float(std::sqrt(ss / (N - SETTLE)));
            };
            const float rms_off = loud_rms(0.0f);
            const float rms_on  = loud_rms(0.6f);
            if (!(rms_on < rms_off * 0.98f)) {
                printf("FAIL jcm800 sag loud-level: sag on=%.6g off=%.6g "
                       "(want on < 0.98x off)\n", double(rms_on), double(rms_off));
                ++failures;
            } else {
                printf("[jcm800 sag loud-level: on=%.6g off=%.6g (want on < 0.98x off)]\n",
                       double(rms_on), double(rms_off));
            }
        }

        // JCM800 Test 7: stability at extreme settings — headroom = 0.05
        // (max +18 dB output compensation), sag = 1, full-scale triangle
        // sweep through the full chain. Bounded by master * cap + margin.
        // presence = 0 so the static shelf gain does not enter the bound
        // (this test targets the sag envelope path, not the shelf).
        {
            const float bound = 0.85f * zuko::kHeadroomOutCap * 1.05f;
            zuko::Jcm800Coeffs jc = zuko::jcm800_init(kFs, 1.0f, 0.0f, 1.0f,
                                                      0.05f, 1.0f);
            zuko::ToneStackCoeffs unity = zuko::tone_stack_init(kFs, 0.f, 0.f, 0.f);
            zuko::AmpConfig amp;
            amp.model      = zuko::AmpModel::kJcm800;
            amp.jcm_coeffs = jc;
            zuko::DspState state{};
            bool bounded = true;
            for (int blk = 0; blk < 64 && bounded; ++blk) {
                float L[32], R[32];
                for (int i = 0; i < 32; ++i) {
                    float t = float(blk * 32 + i) / float(64 * 32 - 1);
                    L[i] = t < 0.5f ? (2.f * t - 1.f) : (1.f - 2.f * (t - 0.5f));
                    R[i] = -L[i];
                }
                zuko::process_block(L, R, 32, state, unity, amp);
                for (int i = 0; i < 32 && bounded; ++i) {
                    if (std::abs(L[i]) > bound || std::abs(R[i]) > bound) {
                        printf("FAIL jcm800 extreme stability: |L[%d]|=%.9g "
                               "exceeded %.4g\n",
                               blk * 32 + i, double(std::abs(L[i])), double(bound));
                        bounded = false;
                    }
                }
            }
            if (!bounded) ++failures;
        }
    }

    printf(failures ? "SELFTEST: %d FAILURES\n" : "SELFTEST: PASS (%d failures)\n", failures);
    return failures;
}

// Internal WAV processing helper (shared by run and run_v2).
static void deinterleave(const Wav &wav, std::vector<float> &L, std::vector<float> &R) {
    const size_t frames = wav.samples.size() / wav.channels;
    L.resize(frames); R.resize(frames);
    auto to_float = [](int16_t s) { return float(s) / 32768.f; };
    if (wav.channels >= 2) {
        for (size_t i = 0; i < frames; ++i) {
            L[i] = to_float(wav.samples[2 * i]);
            R[i] = to_float(wav.samples[2 * i + 1]);
        }
    } else {
        for (size_t i = 0; i < frames; ++i) L[i] = R[i] = to_float(wav.samples[i]);
    }
}

static void interleave(Wav &wav, const std::vector<float> &L, const std::vector<float> &R) {
    const size_t frames = L.size();
    wav.samples.resize(frames * (wav.channels >= 2 ? 2 : 1));
    auto clip = [](float v) -> int16_t {
        if (v > 0.99997f) v = 0.99997f;
        if (v < -1.0f) v = -1.0f;
        return int16_t(v * 32768.f);
    };
    for (size_t i = 0; i < frames; ++i) {
        if (wav.channels >= 2) {
            wav.samples[2 * i]     = clip(L[i]);
            wav.samples[2 * i + 1] = clip(R[i]);
        } else {
            wav.samples[i] = clip(L[i]);
        }
    }
}

// Original run: backward-compatible with old 3-arg positional style.
int run(const char *in_path, const char *out_path,
        float bass_db = 0.f, float mid_db = 0.f, float treble_db = 0.f) {
    Wav wav;
    if (!read_wav(in_path, &wav)) {
        fprintf(stderr, "harness: cannot read %s as 16-bit PCM WAV\n", in_path);
        return 2;
    }
    const size_t frames = wav.samples.size() / wav.channels;
    std::vector<float> L, R;
    deinterleave(wav, L, R);

    const zuko::ToneStackCoeffs eq =
        zuko::tone_stack_init(float(wav.sample_rate), bass_db, mid_db, treble_db);
    zuko::DspState state{};
    constexpr size_t kBlock = 32;
    for (size_t off = 0; off < frames; off += kBlock)
        zuko::process_block(L.data() + off, R.data() + off,
                            (frames - off < kBlock) ? frames - off : kBlock,
                            state, eq);

    interleave(wav, L, R);
    if (!write_wav(out_path, wav)) {
        fprintf(stderr, "harness: cannot write %s\n", out_path);
        return 2;
    }
    printf("harness: %zu frames, %u ch, %u Hz -> %s "
           "(bass=%.1f mid=%.1f treble=%.1f dB)\n",
           frames, wav.channels, wav.sample_rate, out_path,
           double(bass_db), double(mid_db), double(treble_db));
    return 0;
}

// Extended run: supports model selection, gain/presence/master, dynamics
// (headroom/sag), and INI presets.
int run_v2(const char *in_path, const char *out_path,
           float bass_db, float mid_db, float treble_db,
           float gain, float presence, float master,
           float headroom, float sag, float sag_att_ms, float sag_rel_ms,
           zuko::AmpModel model, int cab_taps) {
    Wav wav;
    if (!read_wav(in_path, &wav)) {
        fprintf(stderr, "harness: cannot read %s as 16-bit PCM WAV\n", in_path);
        return 2;
    }
    const size_t frames = wav.samples.size() / wav.channels;
    std::vector<float> L, R;
    deinterleave(wav, L, R);

    const float fs = float(wav.sample_rate);
    const zuko::ToneStackCoeffs eq = zuko::tone_stack_init(fs, bass_db, mid_db, treble_db);

    zuko::AmpConfig amp;
    amp.model = model;
    if (model == zuko::AmpModel::kJcm800)
        amp.jcm_coeffs = zuko::jcm800_init(fs, gain, presence, master,
                                           headroom, sag, sag_att_ms, sag_rel_ms);

    zuko::DspState state{};
    zuko::cab_set_taps(state.cab_L, cab_taps);
    zuko::cab_set_taps(state.cab_R, cab_taps);
    constexpr size_t kBlock = 32;
    for (size_t off = 0; off < frames; off += kBlock)
        zuko::process_block(L.data() + off, R.data() + off,
                            (frames - off < kBlock) ? frames - off : kBlock,
                            state, eq, amp);

    interleave(wav, L, R);
    if (!write_wav(out_path, wav)) {
        fprintf(stderr, "harness: cannot write %s\n", out_path);
        return 2;
    }
    const char *model_str = (model == zuko::AmpModel::kJcm800) ? "jcm800" : "generic";
    printf("harness: %zu frames, %u ch, %u Hz -> %s "
           "(model=%s gain=%.2f presence=%.2f master=%.2f hr=%.2f sag=%.2f "
           "sag_ms=%.0f/%.0f bass=%.1f mid=%.1f treble=%.1f dB cabsim_taps=%d)\n",
           frames, wav.channels, wav.sample_rate, out_path,
           model_str, double(gain), double(presence), double(master),
           double(headroom), double(sag), double(sag_att_ms), double(sag_rel_ms),
           double(bass_db), double(mid_db), double(treble_db), cab_taps);
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--selftest") return selftest();

    if (argc < 3) {
        fprintf(stderr,
                "usage: harness in.wav out.wav [bass_db [mid_db [treble_db]]]\n"
                "       harness in.wav out.wav [--preset file.ini] [--bass N] "
                "[--mid N] [--treble N]\n"
                "                              [--gain N] [--presence N] "
                "[--master N] [--model generic|jcm800]\n"
                "                              [--headroom N] [--sag N] "
                "[--sag-attack MS] [--sag-release MS]\n"
                "       harness --selftest\n");
        return 2;
    }

    const char *in_path  = argv[1];
    const char *out_path = argv[2];

    // Detect new-style args (any arg starting with '--').
    bool new_style = false;
    for (int i = 3; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] == '-') { new_style = true; break; }
    }

    if (!new_style) {
        // Backward compat: harness in.wav out.wav [bass [mid [treble]]]
        float bass = 0.f, mid = 0.f, treble = 0.f;
        if (argc >= 4) bass   = std::stof(argv[3]);
        if (argc >= 5) mid    = std::stof(argv[4]);
        if (argc >= 6) treble = std::stof(argv[5]);
        return run(in_path, out_path, bass, mid, treble);
    }

    // New-style: defaults, then apply --preset, then CLI overrides.
    // ALL parameters are 0-100 (see ini_map.h); converted at the end.
    float bass = 50.f, mid = 50.f, treble = 50.f;      // 50 = 0 dB neutral
    float gain = 9.f, presence = 50.f, master = 80.f;  // ~old defaults 0.7/0.5/0.8
    float headroom = 100.f, sag = 0.f;                 // 100 = pad off, 0 = sag off
    float sag_att_ms = 10.f, sag_rel_ms = 250.f;
    int cab_taps = zuko::kCabFirTaps;                  // full IR
    zuko::AmpModel model = zuko::AmpModel::kGeneric;

    // First pass: load --preset
    for (int i = 3; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--preset") {
            auto ini = zuko_ini::parse_ini(argv[i + 1]);
            bass     = zuko_ini::get_f(ini, "bass",     bass);
            mid      = zuko_ini::get_f(ini, "mid",      mid);
            treble   = zuko_ini::get_f(ini, "treble",   treble);
            gain     = zuko_ini::get_f(ini, "gain",     gain);
            presence = zuko_ini::get_f(ini, "presence", presence);
            master   = zuko_ini::get_f(ini, "master",   master);
            headroom = zuko_ini::get_f(ini, "headroom", headroom);
            sag      = zuko_ini::get_f(ini, "sag",      sag);
            sag_att_ms = zuko_ini::get_f(ini, "sag_attack_ms",  sag_att_ms);
            sag_rel_ms = zuko_ini::get_f(ini, "sag_release_ms", sag_rel_ms);
            cab_taps = int(zuko_ini::get_f(ini, "cabsim_taps", float(cab_taps)));
            auto it = ini.find("model");
            if (it != ini.end() && it->second == "jcm800")
                model = zuko::AmpModel::kJcm800;
            break;
        }
    }

    // Second pass: CLI overrides (also 0-100)
    for (int i = 3; i < argc; ++i) {
        std::string arg(argv[i]);
        if      (arg == "--bass"     && i + 1 < argc) bass     = std::stof(argv[++i]);
        else if (arg == "--mid"      && i + 1 < argc) mid      = std::stof(argv[++i]);
        else if (arg == "--treble"   && i + 1 < argc) treble   = std::stof(argv[++i]);
        else if (arg == "--gain"     && i + 1 < argc) gain     = std::stof(argv[++i]);
        else if (arg == "--presence" && i + 1 < argc) presence = std::stof(argv[++i]);
        else if (arg == "--master"   && i + 1 < argc) master   = std::stof(argv[++i]);
        else if (arg == "--headroom" && i + 1 < argc) headroom = std::stof(argv[++i]);
        else if (arg == "--sag"      && i + 1 < argc) sag      = std::stof(argv[++i]);
        else if (arg == "--sag-attack"  && i + 1 < argc) sag_att_ms = std::stof(argv[++i]);
        else if (arg == "--sag-release" && i + 1 < argc) sag_rel_ms = std::stof(argv[++i]);
        else if (arg == "--cab"      && i + 1 < argc) cab_taps = std::stoi(argv[++i]);
        else if (arg == "--model"    && i + 1 < argc) {
            ++i;
            model = (std::string(argv[i]) == "jcm800")
                    ? zuko::AmpModel::kJcm800
                    : zuko::AmpModel::kGeneric;
        }
        else if (arg == "--preset") ++i;  // already consumed
    }

    // Convert 0-100 -> internal units.
    bass     = zuko_ini::pct_to_db(bass);
    mid      = zuko_ini::pct_to_db(mid);
    treble   = zuko_ini::pct_to_db(treble);
    gain     = zuko_ini::pct_to_gain(gain);
    presence = zuko_ini::pct_to_unit(presence);
    master   = zuko_ini::pct_to_unit(master);
    headroom = zuko_ini::pct_to_headroom(headroom);
    sag      = zuko_ini::pct_to_unit(sag);
    return run_v2(in_path, out_path, bass, mid, treble, gain, presence, master,
                  headroom, sag, sag_att_ms, sag_rel_ms, model, cab_taps);
}
