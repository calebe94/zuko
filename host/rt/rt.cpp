// Zuko (RP2350) — real-time host simulator.
//
// Runs the SAME DSP chain as the firmware (src/dsp/) on the Mac in real time:
// guitar -> USB audio interface (e.g. Tank G) -> preamp/JCM800 -> tone stack ->
// cab sim -> headphones/output. PortAudio duplex callback, 128-frame buffers
// (~2.7 ms interface latency at 48 kHz).
//
// Usage:
//   ./rt --list
//   ./rt [--preset presets/jcm800.ini] [--in NAME] [--out NAME] [--sr 48000]
//
// --in/--out are substring matches against the PortAudio device names, so
// "--in Tank --out Headphones" picks the Tank G input and a headphone output.

#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <portaudio.h>

#include "dsp/dsp.h"
#include "../ini_map.h"

namespace {

constexpr uint32_t kDefaultSr = 48000;
constexpr unsigned kFramesPerBuffer = 64;   // ~1.3 ms per callback
constexpr float kSuggestedLatency = 0.004f; // 4 ms; CoreAudio clamps if the HW can't

// ---------------------------------------------------------------- DSP state

struct RtEngine {
    zuko::DspState state{};
    zuko::ToneStackCoeffs ts{};
    zuko::AmpConfig amp{};

    float process(float x) {
        if (amp.model == zuko::AmpModel::kJcm800)
            x = zuko::jcm800_sample(x, amp.jcm_coeffs, state.jcm800_L);
        else
            x = zuko::preamp_sample(x, zuko::kPreampDrive, zuko::kPreampLevel);
        x = zuko::biquad_sample(x, ts.bass, state.ts_L.bass);
        x = zuko::biquad_sample(x, ts.mid, state.ts_L.mid);
        x = zuko::biquad_sample(x, ts.treble, state.ts_L.treble);
        x = zuko::cab_sim_sample(x, zuko::kCabFirCoeffs, state.cab_L);
        return x;
    }
};

RtEngine g_engine;

// Lock-free SPSC ring linking the two streams. When input and output are on
// DIFFERENT USB devices their clocks drift apart; a single duplex stream dies
// with CoreAudio err=-50 on that combination. Two streams + ring decouples them.
// Policy: underrun -> output silence; overflow -> drop oldest input.
constexpr size_t kRingSize = 4096;  // ~85 ms at 48 kHz; drift absorbs via drop
static_assert((kRingSize & (kRingSize - 1)) == 0, "power of two");

struct Ring {
    std::atomic<size_t> read_{0}, write_{0};
    float buf[kRingSize]{};

    void push(float v) {
        const size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        if (w - r == kRingSize) read_.store(r + 1, std::memory_order_release);  // overflow: drop oldest
        buf[w & (kRingSize - 1)] = v;
        write_.store(w + 1, std::memory_order_release);
    }
    float pop() {
        const size_t r = read_.load(std::memory_order_relaxed);
        const size_t w = write_.load(std::memory_order_acquire);
        if (r == w) return 0.f;  // underrun: silence
        read_.store(r + 1, std::memory_order_release);
        return buf[r & (kRingSize - 1)];
    }
};

Ring g_ring;

int input_callback(const void *in_, void *, unsigned long n,
                   const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *) {
    const float *in = static_cast<const float *>(in_);
    for (unsigned long i = 0; i < n; ++i)
        g_ring.push(g_engine.process(in[i]));
    return paContinue;
}

int output_callback(const void *, void *out_, unsigned long n,
                    const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *) {
    float *out = static_cast<float *>(out_);
    for (unsigned long i = 0; i < n; ++i) {
        const float y = g_ring.pop();
        out[2 * i] = y;
        out[2 * i + 1] = y;
    }
    return paContinue;
}

int duplex_callback(const void *in_, void *out_, unsigned long n,
                    const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *) {
    const float *in = static_cast<const float *>(in_);
    float *out = static_cast<float *>(out_);
    for (unsigned long i = 0; i < n; ++i) {
        const float y = g_engine.process(in[i]);
        out[2 * i] = y;      // mono source -> both output channels
        out[2 * i + 1] = y;
    }
    return paContinue;
}

int find_device(const char *spec, bool is_input) {
    // spec may be a numeric device index ("1") or a name substring ("Tank").
    if (!spec || !*spec) return is_input ? Pa_GetDefaultInputDevice()
                                         : Pa_GetDefaultOutputDevice();
    if (isdigit(static_cast<unsigned char>(spec[0]))) {
        char *end = nullptr;
        const long idx = strtol(spec, &end, 10);
        if (end && *end == '\0' && idx >= 0 && idx < Pa_GetDeviceCount()) {
            const PaDeviceInfo *d = Pa_GetDeviceInfo(static_cast<int>(idx));
            const int ch = is_input ? d->maxInputChannels : d->maxOutputChannels;
            if (ch >= 1) return static_cast<int>(idx);
        }
        return paNoDevice;
    }
    const int n = Pa_GetDeviceCount();
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo *d = Pa_GetDeviceInfo(i);
        if (!d) continue;
        if (is_input && d->maxInputChannels < 1) continue;
        if (!is_input && d->maxOutputChannels < 1) continue;
        if (strstr(d->name, spec)) return i;
    }
    return paNoDevice;
}

}  // namespace

int main(int argc, char **argv) {
    std::string preset = "presets/clean.ini", in_sub, out_sub;
    uint32_t sr = kDefaultSr;
    bool want_list = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        if (a == "--list") want_list = true;
        else if (a == "--preset") preset = next();
        else if (a == "--in") in_sub = next();
        else if (a == "--out") out_sub = next();
        else if (a == "--sr") sr = static_cast<uint32_t>(strtol(next().c_str(), nullptr, 10));
        else { fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }

    PaError err = Pa_Initialize();
    if (err != paNoError) { fprintf(stderr, "Pa_Initialize: %s\n", Pa_GetErrorText(err)); return 1; }

    if (want_list) {
        const int n = Pa_GetDeviceCount();
        for (int i = 0; i < n; ++i) {
            const PaDeviceInfo *d = Pa_GetDeviceInfo(i);
            printf("[%2d] %-50s in:%d out:%d\n", i, d->name,
                   d->maxInputChannels, d->maxOutputChannels);
        }
        Pa_Terminate();
        return 0;
    }

    // Config: preset file, ALL parameters 0-100 (see ../ini_map.h).
    const auto kv = zuko_ini::parse_ini(preset);
    const float gain_pct     = zuko_ini::get_f(kv, "gain", 9.f);
    const float presence_pct = zuko_ini::get_f(kv, "presence", 50.f);
    const float master_pct   = zuko_ini::get_f(kv, "master", 80.f);
    const float bass_pct     = zuko_ini::get_f(kv, "bass", 50.f);
    const float mid_pct      = zuko_ini::get_f(kv, "mid", 50.f);
    const float treble_pct   = zuko_ini::get_f(kv, "treble", 50.f);
    const float headroom_pct = zuko_ini::get_f(kv, "headroom", 100.f);
    const float sag_pct      = zuko_ini::get_f(kv, "sag", 0.f);
    const float sag_att_ms   = zuko_ini::get_f(kv, "sag_attack_ms", 10.f);
    const float sag_rel_ms   = zuko_ini::get_f(kv, "sag_release_ms", 250.f);
    const int cab_taps = int(zuko_ini::get_f(kv, "cabsim_taps", float(zuko::kCabFirTaps)));
    const float gain     = zuko_ini::pct_to_gain(gain_pct);
    const float presence = zuko_ini::pct_to_unit(presence_pct);
    const float master   = zuko_ini::pct_to_unit(master_pct);
    const float headroom = zuko_ini::pct_to_headroom(headroom_pct);
    const float sag      = zuko_ini::pct_to_unit(sag_pct);
    const float bass     = zuko_ini::pct_to_db(bass_pct);
    const float mid      = zuko_ini::pct_to_db(mid_pct);
    const float treble   = zuko_ini::pct_to_db(treble_pct);
    const std::string model = kv.count("model") ? kv.at("model") : "generic";

    g_engine.ts = zuko::tone_stack_init(static_cast<float>(sr), bass, mid, treble);
    zuko::cab_set_taps(g_engine.state.cab_L, cab_taps);
    zuko::cab_set_taps(g_engine.state.cab_R, cab_taps);
    if (model == "jcm800") {
        g_engine.amp.model = zuko::AmpModel::kJcm800;
        g_engine.amp.jcm_coeffs = zuko::jcm800_init(
            static_cast<float>(sr), gain, presence, master,
            headroom, sag, sag_att_ms, sag_rel_ms);
    } else {
        g_engine.amp.model = zuko::AmpModel::kGeneric;
    }

    const int in_dev = find_device(in_sub.c_str(), true);
    const int out_dev = find_device(out_sub.c_str(), false);
    if (in_dev == paNoDevice || out_dev == paNoDevice) {
        fprintf(stderr, "device not found:\n");
        if (in_dev == paNoDevice)
            fprintf(stderr, "  --in \"%s\": no device with an INPUT matching this (see --list)\n", in_sub.c_str());
        if (out_dev == paNoDevice)
            fprintf(stderr, "  --out \"%s\": no device with an OUTPUT matching this (see --list)\n", out_sub.c_str());
        Pa_Terminate();
        return 1;
    }

    PaStreamParameters in_p{}, out_p{};
    in_p.device = in_dev;
    in_p.channelCount = 1;
    in_p.sampleFormat = paFloat32;
    in_p.suggestedLatency = kSuggestedLatency;
    out_p.device = out_dev;
    out_p.channelCount = 2;
    out_p.sampleFormat = paFloat32;
    out_p.suggestedLatency = kSuggestedLatency;

    // Same hardware name (e.g. Tank G exposes separate in/out devices under one
    // name) -> one duplex stream, lowest latency. Different devices -> two
    // independent streams joined by the ring: their USB clocks drift, and a
    // single duplex stream across them dies with CoreAudio err=-50.
    const bool same_hw = strcmp(Pa_GetDeviceInfo(in_dev)->name,
                                Pa_GetDeviceInfo(out_dev)->name) == 0;

    PaStream *in_stream = nullptr, *out_stream = nullptr;
    if (same_hw) {
        err = Pa_OpenStream(&in_stream, &in_p, &out_p, static_cast<double>(sr),
                            kFramesPerBuffer, paClipOff, duplex_callback, nullptr);
        if (err != paNoError) { fprintf(stderr, "Pa_OpenStream: %s\n", Pa_GetErrorText(err)); Pa_Terminate(); return 1; }
        err = Pa_StartStream(in_stream);
        if (err != paNoError) { fprintf(stderr, "Pa_StartStream: %s\n", Pa_GetErrorText(err)); Pa_Terminate(); return 1; }
        const PaStreamInfo *info = Pa_GetStreamInfo(in_stream);
        printf("zuko-rt live | preset=%s model=%s gain=%.0f presence=%.0f master=%.0f "
               "hr=%.0f sag=%.0f EQ=%.0f/%.0f/%.0f cab=%d taps | in=[%s] out=[%s] | %.1f ms latency | Enter/Ctrl+C to stop\n",
               preset.c_str(), model.c_str(), gain_pct, presence_pct, master_pct,
               headroom_pct, sag_pct,
               bass_pct, mid_pct, treble_pct, cab_taps,
               Pa_GetDeviceInfo(in_dev)->name, Pa_GetDeviceInfo(out_dev)->name,
               info->outputLatency * 1000.f);
    } else {
        err = Pa_OpenStream(&in_stream, &in_p, nullptr, static_cast<double>(sr),
                            kFramesPerBuffer, paClipOff, input_callback, nullptr);
        if (err != paNoError) { fprintf(stderr, "Pa_OpenStream(in): %s\n", Pa_GetErrorText(err)); Pa_Terminate(); return 1; }
        err = Pa_OpenStream(&out_stream, nullptr, &out_p, static_cast<double>(sr),
                            kFramesPerBuffer, paClipOff, output_callback, nullptr);
        if (err != paNoError) { fprintf(stderr, "Pa_OpenStream(out): %s\n", Pa_GetErrorText(err)); Pa_Terminate(); return 1; }
        err = Pa_StartStream(in_stream);
        if (err != paNoError) { fprintf(stderr, "Pa_StartStream(in): %s\n", Pa_GetErrorText(err)); Pa_Terminate(); return 1; }
        err = Pa_StartStream(out_stream);
        if (err != paNoError) { fprintf(stderr, "Pa_StartStream(out): %s\n", Pa_GetErrorText(err)); Pa_Terminate(); return 1; }
        const PaStreamInfo *oi = Pa_GetStreamInfo(out_stream);
        printf("zuko-rt live (split-clock) | preset=%s model=%s gain=%.0f presence=%.0f master=%.0f "
               "hr=%.0f sag=%.0f EQ=%.0f/%.0f/%.0f cab=%d taps | in=[%s] out=[%s] | ~%.1f ms latency | Enter/Ctrl+C to stop\n",
               preset.c_str(), model.c_str(), gain_pct, presence_pct, master_pct,
               headroom_pct, sag_pct,
               bass_pct, mid_pct, treble_pct, cab_taps,
               Pa_GetDeviceInfo(in_dev)->name, Pa_GetDeviceInfo(out_dev)->name,
               oi->outputLatency * 1000.f);
    }

    getchar();  // block until Enter (Ctrl+C also stops)
    Pa_StopStream(in_stream);
    Pa_CloseStream(in_stream);
    if (out_stream) { Pa_StopStream(out_stream); Pa_CloseStream(out_stream); }
    Pa_Terminate();
    return 0;
}
