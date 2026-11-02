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

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <portaudio.h>

#include "dsp/dsp.h"

namespace {

constexpr uint32_t kDefaultSr = 48000;
constexpr unsigned kFramesPerBuffer = 128;

// ---------------------------------------------------------------- ini parse

std::map<std::string, std::string> parse_ini(const std::string &path) {
    std::map<std::string, std::string> kv;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            const char *ws = " \t\r\n";
            s.erase(0, s.find_first_not_of(ws));
            s.erase(s.find_last_not_of(ws) + 1);
            return s;
        };
        kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return kv;
}

float getf(const std::map<std::string, std::string> &kv, const char *k, float dflt) {
    auto it = kv.find(k);
    return it == kv.end() ? dflt : strtof(it->second.c_str(), nullptr);
}

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

int callback(const void *in_, void *out_, unsigned long n,
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

    // Config: preset file first, CLI not overridden here (edit the preset).
    const auto kv = parse_ini(preset);
    const float gain = getf(kv, "gain", 0.7f);
    const float presence = getf(kv, "presence", 0.5f);
    const float master = getf(kv, "master", 0.8f);
    const float bass = getf(kv, "bass", 0.f);
    const float mid = getf(kv, "mid", 0.f);
    const float treble = getf(kv, "treble", 0.f);
    const std::string model = kv.count("model") ? kv.at("model") : "generic";

    g_engine.ts = zuko::tone_stack_init(static_cast<float>(sr), bass, mid, treble);
    if (model == "jcm800") {
        g_engine.amp.model = zuko::AmpModel::kJcm800;
        g_engine.amp.jcm_coeffs = zuko::jcm800_init(static_cast<float>(sr), gain, presence, master);
    } else {
        g_engine.amp.model = zuko::AmpModel::kGeneric;
    }

    const int in_dev = find_device(in_sub.c_str(), true);
    const int out_dev = find_device(out_sub.c_str(), false);
    if (in_dev == paNoDevice || out_dev == paNoDevice) {
        fprintf(stderr, "device not found (in=\"%s\" out=\"%s\"). Use --list.\n",
                in_sub.c_str(), out_sub.c_str());
        Pa_Terminate();
        return 1;
    }

    PaStreamParameters in_p{}, out_p{};
    in_p.device = in_dev;
    in_p.channelCount = 1;
    in_p.sampleFormat = paFloat32;
    in_p.suggestedLatency = Pa_GetDeviceInfo(in_dev)->defaultLowInputLatency;
    out_p.device = out_dev;
    out_p.channelCount = 2;
    out_p.sampleFormat = paFloat32;
    out_p.suggestedLatency = Pa_GetDeviceInfo(out_dev)->defaultLowOutputLatency;

    PaStream *stream = nullptr;
    err = Pa_OpenStream(&stream, &in_p, &out_p, static_cast<double>(sr),
                        kFramesPerBuffer, paClipOff, callback, nullptr);
    if (err != paNoError) { fprintf(stderr, "Pa_OpenStream: %s\n", Pa_GetErrorText(err)); Pa_Terminate(); return 1; }

    err = Pa_StartStream(stream);
    if (err != paNoError) { fprintf(stderr, "Pa_StartStream: %s\n", Pa_GetErrorText(err)); Pa_Terminate(); return 1; }

    const PaStreamInfo *info = Pa_GetStreamInfo(stream);
    printf("zuko-rt live | preset=%s model=%s gain=%.2f presence=%.2f master=%.2f "
           "EQ=%.0f/%.0f/%.0f dB | in=[%s] out=[%s] | %.1f ms latency | Ctrl+C to stop\n",
           preset.c_str(), model.c_str(), gain, presence, master, bass, mid, treble,
           Pa_GetDeviceInfo(in_dev)->name, Pa_GetDeviceInfo(out_dev)->name,
           info->outputLatency * 1000.f);

    getchar();  // block until Enter (Ctrl+C also stops)
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}
