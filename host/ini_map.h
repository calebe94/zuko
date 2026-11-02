#ifndef ZUKO_INI_MAP_H
#define ZUKO_INI_MAP_H

// Host-only helper shared by harness and rt: INI parsing plus the 0-100
// parameter scaling. ALL .ini parameters are 0-100 for intuitiveness; the
// conversions below are the single source of truth for what they mean.
//
//   gain            0-100  -> per-stage drive 2..50   (gain_pct * 0.08 internal)
//   bass/mid/treble 0-100  -> -12..+12 dB (50 = neutral)
//   presence        0-100  -> 0..+12 dB shelf
//   master          0-100  -> 0..1 volume
//
// gain above 100 is allowed on purpose (drive beyond 50 = extreme crush;
// tanh-bounded, mathematically safe). See presets/jcm800-punk.ini.

#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace zuko_ini {

inline std::map<std::string, std::string> parse_ini(const std::string &path) {
    std::map<std::string, std::string> out;
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return out;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len == 0 || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        std::string key(line), val(eq + 1);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!key.empty() && key.front() == ' ') key.erase(0, 1);
        while (!val.empty() && val.back() == ' ') val.pop_back();
        while (!val.empty() && val.front() == ' ') val.erase(0, 1);
        if (!key.empty()) out[key] = val;
    }
    fclose(f);
    return out;
}

inline float get_f(const std::map<std::string, std::string> &kv,
                   const char *key, float dflt) {
    auto it = kv.find(key);
    return it == kv.end() ? dflt : strtof(it->second.c_str(), nullptr);
}

// 0-100 -> jcm800_init() gain argument (drive = 2 + gain*6 per stage).
inline float pct_to_gain(float pct) { return pct * 0.08f; }

// 0-100 -> dB, 50 = neutral, +/-12 dB at the ends.
inline float pct_to_db(float pct) { return pct * 0.24f - 12.0f; }

// 0-100 -> 0..1.
inline float pct_to_unit(float pct) { return pct / 100.0f; }

}  // namespace zuko_ini

#endif  // ZUKO_INI_MAP_H
