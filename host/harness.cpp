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
#include <string>
#include <vector>

#include "dsp/dsp.h"

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
        for (double x = -1.0; x <= 1.0; x += 0.05) {
            float got = zuko::preamp_sample(float(x), drive, level);
            double ref = double(level) * std::tanh(double(drive) * x);
            double tol = 8.0 * std::abs(double(got)) * 1.19e-7;
            if (tol < 1e-7) tol = 1e-7;
            if (std::abs(got - ref) > tol) {
                printf("FAIL preamp_sample(x=%.3f): got %.9g want %.9g\n", x, got, ref);
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

    // process_block (preamp engaged): output must match preamp_sample references
    {
        float L[32], R[32];
        for (int i = 0; i < 32; ++i) { L[i] = float(i) / 32.f; R[i] = -L[i]; }
        float L2[32], R2[32];
        memcpy(L2, L, sizeof(L)); memcpy(R2, R, sizeof(R));
        zuko::process_block(L2, R2, 32);
        for (int i = 0; i < 32; ++i) {
            float eL = zuko::preamp_sample(L[i], zuko::kPreampDrive, zuko::kPreampLevel);
            float eR = zuko::preamp_sample(R[i], zuko::kPreampDrive, zuko::kPreampLevel);
            if (L2[i] != eL || R2[i] != eR) {
                printf("FAIL process_block[%d]: L got %.9g want %.9g, "
                       "R got %.9g want %.9g\n",
                       i, double(L2[i]), double(eL), double(R2[i]), double(eR));
                ++failures;
                break;
            }
        }
    }

    printf(failures ? "SELFTEST: %d FAILURES\n" : "SELFTEST: PASS (%d failures)\n", failures);
    return failures;
}

int run(const char *in_path, const char *out_path) {
    Wav wav;
    if (!read_wav(in_path, &wav)) {
        fprintf(stderr, "harness: cannot read %s as 16-bit PCM WAV\n", in_path);
        return 2;
    }
    const size_t frames = wav.samples.size() / wav.channels;

    // De-interleave to float in [-1, 1], process in 32-frame blocks (the
    // firmware block size), write back out.
    std::vector<float> L(frames), R(frames);
    auto to_float = [](int16_t s) { return float(s) / 32768.f; };
    if (wav.channels == 2) {
        for (size_t i = 0; i < frames; ++i) {
            L[i] = to_float(wav.samples[2 * i]);
            R[i] = to_float(wav.samples[2 * i + 1]);
        }
    } else {
        for (size_t i = 0; i < frames; ++i) L[i] = R[i] = to_float(wav.samples[i]);
    }

    constexpr size_t kBlock = 32;
    for (size_t off = 0; off < frames; off += kBlock) {
        zuko::process_block(L.data() + off, R.data() + off,
                            (frames - off < kBlock) ? frames - off : kBlock);
    }

    wav.samples.resize(frames * (wav.channels >= 2 ? 2 : 1));
    for (size_t i = 0; i < frames; ++i) {
        auto clip = [](float v) -> int16_t {
            if (v > 0.99997f) v = 0.99997f;
            if (v < -1.0f) v = -1.0f;
            return int16_t(v * 32768.f);
        };
        if (wav.channels >= 2) {
            wav.samples[2 * i] = clip(L[i]);
            wav.samples[2 * i + 1] = clip(R[i]);
        } else {
            wav.samples[i] = clip(L[i]);
        }
    }
    if (!write_wav(out_path, wav)) {
        fprintf(stderr, "harness: cannot write %s\n", out_path);
        return 2;
    }
    printf("harness: %zu frames, %u ch, %u Hz -> %s\n",
           frames, wav.channels, wav.sample_rate, out_path);
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--selftest") return selftest();
    if (argc == 3) return run(argv[1], argv[2]);
    fprintf(stderr, "usage: harness in.wav out.wav | harness --selftest\n");
    return 2;
}
