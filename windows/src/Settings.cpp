#include "Settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace sq::app {

const char* label(BandType t) {
    switch (t) {
    case BandType::Peak: return "Peak";
    case BandType::LowShelf: return "Low Shelf";
    case BandType::HighShelf: return "High Shelf";
    case BandType::LowPass: return "Low Pass";
    case BandType::HighPass: return "High Pass";
    }
    return "";
}

const char* label(Upmix u) {
    switch (u) {
    case Upmix::Stereo: return "Stereo";
    case Upmix::Surround51: return "Virtual 5.1";
    case Upmix::Surround71: return "Virtual 7.1";
    }
    return "";
}

bool hasGain(BandType t) { return t == BandType::Peak || t == BandType::LowShelf || t == BandType::HighShelf; }

std::vector<const char*> channelNames(Upmix u) {
    switch (u) {
    case Upmix::Stereo: return {"L", "R"};
    case Upmix::Surround51: return {"L", "R", "C", "Ls", "Rs"};
    case Upmix::Surround71: return {"L", "R", "C", "Ls", "Rs", "Lb", "Rb"};
    }
    return {};
}

std::vector<EqBand> SoundSettings::tenBand() {
    static const double kFreqs[10] = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    std::vector<EqBand> bands;
    for (int i = 0; i < 10; ++i) {
        EqBand b;
        b.type = i == 0 ? BandType::LowShelf : i == 9 ? BandType::HighShelf : BandType::Peak;
        b.frequency = kFreqs[i];
        b.q = (i == 0 || i == 9) ? 0.7 : 1.0;
        bands.push_back(b);
    }
    return bands;
}

std::vector<VirtualSource> SoundSettings::defaultSources() {
    static const double kAz[kSourceCount] = {-30, 30, 0, -110, 110, -150, 150, 0};
    std::vector<VirtualSource> v(kSourceCount);
    for (int i = 0; i < kSourceCount; ++i) v[i].azimuth = kAz[i];
    return v;
}

sq_params SoundSettings::toParams(bool enabled) const {
    sq_params p;
    sq_params_default(&p);
    p.enabled = enabled ? 1 : 0;
    p.preampDb = float(preampDb);
    p.eqEnabled = eqEnabled ? 1 : 0;
    const int count = std::min<int>(int(bands.size()), kMaxBands);
    p.numBands = count;
    for (int i = 0; i < count; ++i) {
        const EqBand& b = bands[i];
        p.bands[i] = {b.enabled ? 1 : 0, int(b.type), float(b.frequency), float(b.gainDb), float(b.q)};
    }
    p.mode = int(mode);
    p.spatialEnabled = spatialEnabled ? 1 : 0;
    p.upmix = int(upmix);
    for (int i = 0; i < std::min<int>(int(sources.size()), kSourceCount); ++i) {
        const VirtualSource& s = sources[i];
        p.sources[i] = {float(s.azimuth), float(s.elevation), float(s.distance), float(s.gain)};
    }
    p.width = float(width);
    p.xtcEnabled = crosstalkEnabled ? 1 : 0;
    p.xtcStrength = float(crosstalkStrength);
    p.speakerSpanDeg = float(speakerSpan);
    p.roomSize = float(roomSize);
    p.reverbMix = float(reverbMix);
    p.bassBoostDb = float(bassBoostDb);
    p.dialogueBoost = float(dialogueBoost);
    p.levelerEnabled = levelerEnabled ? 1 : 0;
    p.levelerTargetDb = float(levelerTargetDb);
    p.limiterEnabled = limiterEnabled ? 1 : 0;
    p.limiterCeilingDb = float(limiterCeilingDb);
    p.outputGainDb = float(outputGainDb);
    return p;
}

bool SoundSettings::operator==(const SoundSettings& o) const {
    std::string a, b;
    writeSettings(*this, a);
    writeSettings(o, b);
    return a == b;
}

// -------------------------------------------------------------------------------------------------
// Built-in presets (same list and values as BuiltInPresets.swift).

std::vector<Preset> builtInPresets() {
    std::vector<Preset> all;
    auto make = [&](const char* name, auto edit) {
        Preset p;
        p.name = name;
        p.builtIn = true;
        edit(p.settings);
        all.push_back(p);
    };
    make("Flat", [](SoundSettings&) {});
    make("Bass Boost", [](SoundSettings& s) {
        s.bands[0].gainDb = 6; s.bands[1].gainDb = 4; s.bands[2].gainDb = 2;
        s.bassBoostDb = 4; s.preampDb = -4;
    });
    make("Treble Lift", [](SoundSettings& s) {
        s.bands[7].gainDb = 2; s.bands[8].gainDb = 4; s.bands[9].gainDb = 5; s.preampDb = -4;
    });
    make("Loudness", [](SoundSettings& s) {
        s.bands[0].gainDb = 5; s.bands[1].gainDb = 3; s.bands[8].gainDb = 2; s.bands[9].gainDb = 4;
        s.preampDb = -4;
    });
    make("Vocal & Dialogue", [](SoundSettings& s) {
        s.bands[0].gainDb = -2; s.bands[6].gainDb = 2; s.bands[7].gainDb = 1.5;
        s.dialogueBoost = 0.6; s.levelerEnabled = true;
    });
    make("Podcast", [](SoundSettings& s) {
        s.bands[0].gainDb = -6; s.bands[1].gainDb = -3; s.bands[6].gainDb = 2;
        s.dialogueBoost = 0.8; s.levelerEnabled = true; s.levelerTargetDb = -18;
    });
    make("Cinema", [](SoundSettings& s) {
        s.spatialEnabled = true; s.upmix = Upmix::Surround71;
        s.roomSize = 0.55; s.reverbMix = 0.25;
        s.bands[0].gainDb = 4; s.bassBoostDb = 3; s.dialogueBoost = 0.35; s.levelerEnabled = true;
        s.preampDb = -3;
    });
    make("Music Hall", [](SoundSettings& s) {
        s.spatialEnabled = true; s.upmix = Upmix::Stereo;
        s.sources[0].distance = 2.5; s.sources[1].distance = 2.5;
        s.sources[0].azimuth = -40; s.sources[1].azimuth = 40;
        s.roomSize = 0.85; s.reverbMix = 0.4;
    });
    make("Studio Speakers", [](SoundSettings& s) {
        s.spatialEnabled = true; s.upmix = Upmix::Stereo;
        s.roomSize = 0.2; s.reverbMix = 0.12;
    });
    make("Gaming", [](SoundSettings& s) {
        s.spatialEnabled = true; s.upmix = Upmix::Surround51;
        s.roomSize = 0.15; s.reverbMix = 0.05;
        s.bands[7].gainDb = 2; s.bands[1].gainDb = 2;
    });
    make("Wide Speakers", [](SoundSettings& s) {
        s.mode = OutputMode::Speakers; s.width = 1.5; s.crosstalkEnabled = true; s.crosstalkStrength = 0.6;
    });
    make("Laptop Speakers", [](SoundSettings& s) {
        s.mode = OutputMode::Speakers; s.width = 1.4; s.crosstalkEnabled = true; s.speakerSpan = 20;
        s.bassBoostDb = 6; s.bands[0].type = BandType::HighPass; s.bands[0].frequency = 80; s.bands[0].q = 0.7;
        s.levelerEnabled = true; s.preampDb = -3;
    });
    return all;
}

// -------------------------------------------------------------------------------------------------
// Persistence

namespace {

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

bool parseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

std::vector<double> parseList(const std::string& s) {
    std::vector<double> v;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        double d;
        if (parseDouble(item, d)) v.push_back(d);
    }
    return v;
}

template <typename E>
E clampEnum(double v, int maxValue) {
    return E(std::clamp(int(v), 0, maxValue));
}

} // namespace

void writeSettings(const SoundSettings& s, std::string& out) {
    auto kv = [&](const char* k, double v) { out += k; out += '='; out += num(v); out += '\n'; };
    kv("preampDb", s.preampDb);
    kv("eqEnabled", s.eqEnabled);
    for (const EqBand& b : s.bands) {
        out += "band=" + num(b.enabled) + "," + num(int(b.type)) + "," + num(b.frequency) + "," + num(b.gainDb) + "," +
               num(b.q) + "\n";
    }
    kv("mode", int(s.mode));
    kv("spatialEnabled", s.spatialEnabled);
    kv("upmix", int(s.upmix));
    for (const VirtualSource& v : s.sources) {
        out += "source=" + num(v.azimuth) + "," + num(v.elevation) + "," + num(v.distance) + "," + num(v.gain) + "\n";
    }
    kv("width", s.width);
    kv("crosstalkEnabled", s.crosstalkEnabled);
    kv("crosstalkStrength", s.crosstalkStrength);
    kv("speakerSpan", s.speakerSpan);
    kv("roomSize", s.roomSize);
    kv("reverbMix", s.reverbMix);
    kv("bassBoostDb", s.bassBoostDb);
    kv("dialogueBoost", s.dialogueBoost);
    kv("levelerEnabled", s.levelerEnabled);
    kv("levelerTargetDb", s.levelerTargetDb);
    kv("limiterEnabled", s.limiterEnabled);
    kv("limiterCeilingDb", s.limiterCeilingDb);
    kv("outputGainDb", s.outputGainDb);
}

void beginRead(SoundSettings& s) {
    s.bands.clear();
    s.sources.clear();
}

void finishRead(SoundSettings& s) {
    const auto defaults = SoundSettings::defaultSources();
    while (s.sources.size() < defaults.size()) s.sources.push_back(defaults[s.sources.size()]);
}

bool readSettingsLine(SoundSettings& s, const std::string& key, const std::string& value) {
    if (key == "band") {
        const auto v = parseList(value);
        if (v.size() != 5) return true;
        if (int(s.bands.size()) >= SoundSettings::kMaxBands) return true;
        EqBand b;
        b.enabled = v[0] != 0;
        b.type = clampEnum<BandType>(v[1], 4);
        b.frequency = std::clamp(v[2], 10.0, 24000.0);
        b.gainDb = std::clamp(v[3], -30.0, 30.0);
        b.q = std::clamp(v[4], 0.05, 30.0);
        s.bands.push_back(b);
        return true;
    }
    if (key == "source") {
        const auto v = parseList(value);
        if (v.size() != 4) return true;
        if (int(s.sources.size()) >= SoundSettings::kSourceCount) return true;
        s.sources.push_back({v[0], v[1], std::clamp(v[2], 0.3, 5.0), std::clamp(v[3], 0.0, 4.0)});
        return true;
    }

    double d;
    if (!parseDouble(value, d)) return false;
    struct Field {
        const char* name;
        double* dbl;
        bool* flag;
    };
    const Field fields[] = {
        {"preampDb", &s.preampDb, nullptr},
        {"eqEnabled", nullptr, &s.eqEnabled},
        {"spatialEnabled", nullptr, &s.spatialEnabled},
        {"width", &s.width, nullptr},
        {"crosstalkEnabled", nullptr, &s.crosstalkEnabled},
        {"crosstalkStrength", &s.crosstalkStrength, nullptr},
        {"speakerSpan", &s.speakerSpan, nullptr},
        {"roomSize", &s.roomSize, nullptr},
        {"reverbMix", &s.reverbMix, nullptr},
        {"bassBoostDb", &s.bassBoostDb, nullptr},
        {"dialogueBoost", &s.dialogueBoost, nullptr},
        {"levelerEnabled", nullptr, &s.levelerEnabled},
        {"levelerTargetDb", &s.levelerTargetDb, nullptr},
        {"limiterEnabled", nullptr, &s.limiterEnabled},
        {"limiterCeilingDb", &s.limiterCeilingDb, nullptr},
        {"outputGainDb", &s.outputGainDb, nullptr},
    };
    for (const Field& f : fields) {
        if (key != f.name) continue;
        if (f.dbl) *f.dbl = d;
        if (f.flag) *f.flag = d != 0;
        return true;
    }
    if (key == "mode") { s.mode = clampEnum<OutputMode>(d, 1); return true; }
    if (key == "upmix") { s.upmix = clampEnum<Upmix>(d, 2); return true; }
    return false;
}

// -------------------------------------------------------------------------------------------------
// AutoEq

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool typeFromToken(const std::string& t, BandType& out) {
    static const struct { const char* code; BandType type; } kMap[] = {
        {"PK", BandType::Peak},       {"PEQ", BandType::Peak},      {"LS", BandType::LowShelf},
        {"LSC", BandType::LowShelf},  {"LSQ", BandType::LowShelf},  {"HS", BandType::HighShelf},
        {"HSC", BandType::HighShelf}, {"HSQ", BandType::HighShelf}, {"LP", BandType::LowPass},
        {"LPQ", BandType::LowPass},   {"HP", BandType::HighPass},   {"HPQ", BandType::HighPass},
    };
    for (const auto& m : kMap) {
        if (t == m.code) { out = m.type; return true; }
    }
    return false;
}

} // namespace

bool parseAutoEq(const std::string& text, AutoEqResult& out) {
    out = {};
    std::stringstream lines(text);
    std::string raw;
    while (std::getline(lines, raw)) {
        std::vector<std::string> tokens;
        {
            std::stringstream ts(raw);
            std::string t;
            while (ts >> t) tokens.push_back(t);
        }
        if (tokens.empty()) continue;
        std::vector<std::string> up;
        for (const auto& t : tokens) up.push_back(upper(t));

        if (up[0].rfind("PREAMP:", 0) == 0) {
            // "Preamp: -6.2 dB" or "Preamp:-6.2 dB"
            std::string v = tokens[0].size() > 7 ? tokens[0].substr(7) : (tokens.size() > 1 ? tokens[1] : "");
            parseDouble(v, out.preampDb);
            continue;
        }
        if (up[0].rfind("FILTER", 0) != 0) continue;

        BandType type{};
        bool haveType = false;
        for (const auto& t : up) {
            if (typeFromToken(t, type)) { haveType = true; break; }
        }
        auto valueAfter = [&](const char* key, double& v) {
            for (size_t i = 0; i + 1 < up.size(); ++i) {
                if (up[i] == key) return parseDouble(tokens[i + 1], v);
            }
            return false;
        };
        double fc = 0;
        if (!haveType || !valueAfter("FC", fc)) { ++out.skipped; continue; }
        double gain = 0, q = type == BandType::Peak ? 1.0 : 0.71;
        valueAfter("GAIN", gain);
        valueAfter("Q", q);
        if (int(out.bands.size()) >= SoundSettings::kMaxBands) { ++out.skipped; continue; }
        EqBand b;
        b.enabled = std::find(up.begin(), up.end(), "OFF") == up.end();
        b.type = type;
        b.frequency = fc;
        b.gainDb = gain;
        b.q = q;
        out.bands.push_back(b);
    }
    return !out.bands.empty();
}

std::string exportAutoEq(const SoundSettings& s) {
    std::string out;
    char line[160];
    std::snprintf(line, sizeof(line), "Preamp: %.1f dB\n", s.preampDb);
    out += line;
    for (size_t i = 0; i < s.bands.size(); ++i) {
        const EqBand& b = s.bands[i];
        static const char* kCodes[] = {"PK", "LSC", "HSC", "LPQ", "HPQ"};
        std::snprintf(line, sizeof(line), "Filter %zu: %s %s Fc %.0f Hz Gain %.1f dB Q %.2f\n", i + 1,
                      b.enabled ? "ON" : "OFF", kCodes[int(b.type)], b.frequency, b.gainDb, b.q);
        out += line;
    }
    return out;
}

std::string formatHz(double f) {
    char buf[32];
    if (f >= 1000)
        std::snprintf(buf, sizeof(buf), f >= 10000 ? "%.0fk" : "%.1fk", f / 1000);
    else
        std::snprintf(buf, sizeof(buf), "%.0f", f);
    return buf;
}

} // namespace sq::app
