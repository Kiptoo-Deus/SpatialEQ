#include "presets.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>

namespace sq {

namespace {

Preset make(const char* name, const std::function<void(sq_params&)>& edit) {
    Preset preset{name, defaultModel()};
    edit(preset.model.p);
    return preset;
}

std::vector<Preset> buildPresets() {
    // Mirrors shared/Swift/Model/BuiltInPresets.swift so the plug-in and the apps sound the same.
    return {
        make("Flat", [](sq_params&) {}),
        make("Bass Boost", [](sq_params& s) {
            s.bands[0].gainDb = 6; s.bands[1].gainDb = 4; s.bands[2].gainDb = 2;
            s.bassBoostDb = 4; s.preampDb = -4;
        }),
        make("Treble Lift", [](sq_params& s) {
            s.bands[7].gainDb = 2; s.bands[8].gainDb = 4; s.bands[9].gainDb = 5; s.preampDb = -4;
        }),
        make("Loudness", [](sq_params& s) {
            s.bands[0].gainDb = 5; s.bands[1].gainDb = 3; s.bands[8].gainDb = 2; s.bands[9].gainDb = 4;
            s.preampDb = -4;
        }),
        make("Vocal & Dialogue", [](sq_params& s) {
            s.bands[0].gainDb = -2; s.bands[6].gainDb = 2; s.bands[7].gainDb = 1.5f;
            s.dialogueBoost = 0.6f; s.levelerEnabled = 1;
        }),
        make("Podcast", [](sq_params& s) {
            s.bands[0].gainDb = -6; s.bands[1].gainDb = -3; s.bands[6].gainDb = 2;
            s.dialogueBoost = 0.8f; s.levelerEnabled = 1; s.levelerTargetDb = -18;
        }),
        make("Cinema", [](sq_params& s) {
            s.spatialEnabled = 1; s.upmix = SQ_UPMIX_7_1;
            s.roomSize = 0.55f; s.reverbMix = 0.25f;
            s.bands[0].gainDb = 4; s.bassBoostDb = 3; s.dialogueBoost = 0.35f; s.levelerEnabled = 1;
            s.preampDb = -3;
        }),
        make("Music Hall", [](sq_params& s) {
            s.spatialEnabled = 1; s.upmix = SQ_UPMIX_STEREO;
            s.sources[0].distance = 2.5f; s.sources[1].distance = 2.5f;
            s.sources[0].azimuthDeg = -40; s.sources[1].azimuthDeg = 40;
            s.roomSize = 0.85f; s.reverbMix = 0.4f;
        }),
        make("Studio Speakers", [](sq_params& s) {
            s.spatialEnabled = 1; s.upmix = SQ_UPMIX_STEREO;
            s.roomSize = 0.2f; s.reverbMix = 0.12f;
        }),
        make("Gaming", [](sq_params& s) {
            s.spatialEnabled = 1; s.upmix = SQ_UPMIX_5_1;
            s.roomSize = 0.15f; s.reverbMix = 0.05f;
            s.bands[7].gainDb = 2; s.bands[1].gainDb = 2;
        }),
        make("Wide Speakers", [](sq_params& s) {
            s.mode = SQ_MODE_SPEAKERS; s.width = 1.5f; s.xtcEnabled = 1; s.xtcStrength = 0.6f;
        }),
        make("Laptop Speakers", [](sq_params& s) {
            s.mode = SQ_MODE_SPEAKERS; s.width = 1.4f; s.xtcEnabled = 1; s.speakerSpanDeg = 20;
            s.bassBoostDb = 6; s.bands[0].type = SQ_BAND_HIGHPASS; s.bands[0].freq = 80; s.bands[0].q = 0.7f;
            s.levelerEnabled = 1; s.preampDb = -3;
        }),
    };
}

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool bandTypeFromCode(const std::string& code, int& type) {
    if (code == "PK" || code == "PEQ" || code == "MODAL") type = SQ_BAND_PEAK;
    else if (code == "LS" || code == "LSC" || code == "LSQ" || code == "LS6" || code == "LS12") type = SQ_BAND_LOWSHELF;
    else if (code == "HS" || code == "HSC" || code == "HSQ" || code == "HS6" || code == "HS12") type = SQ_BAND_HIGHSHELF;
    else if (code == "LP" || code == "LPQ") type = SQ_BAND_LOWPASS;
    else if (code == "HP" || code == "HPQ") type = SQ_BAND_HIGHPASS;
    else return false;
    return true;
}

const char* codeFromBandType(int type) {
    switch (type) {
    case SQ_BAND_LOWSHELF: return "LSC";
    case SQ_BAND_HIGHSHELF: return "HSC";
    case SQ_BAND_LOWPASS: return "LPQ";
    case SQ_BAND_HIGHPASS: return "HPQ";
    default: return "PK";
    }
}

bool valueAfter(const std::vector<std::string>& tokens, const char* key, double& out) {
    for (size_t i = 0; i + 1 < tokens.size(); ++i)
        if (upper(tokens[i]) == key) {
            char* end = nullptr;
            const double v = std::strtod(tokens[i + 1].c_str(), &end);
            if (end != tokens[i + 1].c_str()) {
                out = v;
                return true;
            }
        }
    return false;
}

} // namespace

const std::vector<Preset>& builtInPresets() {
    static const std::vector<Preset> presets = buildPresets();
    return presets;
}

bool importParametricEQ(const std::string& text, const Model& base, Model& out, int& imported, int& skipped) {
    out = base;
    imported = skipped = 0;
    double preamp = 0;
    std::vector<sq_band> bands;

    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::vector<std::string> tokens;
        for (std::string w; words >> w;) tokens.push_back(w);
        if (tokens.empty()) continue;
        const std::string head = upper(tokens[0]);
        if (head.rfind("PREAMP", 0) == 0) {
            if (tokens.size() > 1) preamp = std::strtod(tokens[1].c_str(), nullptr);
            continue;
        }
        if (head.rfind("FILTER", 0) != 0) continue;

        // "Filter 1: ON PK Fc 56 Hz Gain 18.0 dB Q 5.00"
        size_t i = 1;
        while (i < tokens.size() && upper(tokens[i]) != "ON" && upper(tokens[i]) != "OFF") ++i;
        if (i + 1 >= tokens.size()) { ++skipped; continue; }
        sq_band band{};
        band.enabled = upper(tokens[i]) == "ON";
        double fc = 0, gain = 0, q = 0.707;
        if (!bandTypeFromCode(upper(tokens[i + 1]), band.type) || !valueAfter(tokens, "FC", fc) || fc <= 0) {
            ++skipped;
            continue;
        }
        valueAfter(tokens, "GAIN", gain);
        valueAfter(tokens, "Q", q);
        band.freq = float(std::clamp(fc, 10.0, 22000.0));
        band.gainDb = float(std::clamp(gain, -24.0, 24.0));
        band.q = float(std::clamp(q, 0.1, 20.0));
        if (int(bands.size()) >= kMaxBands) { ++skipped; continue; }
        bands.push_back(band);
        ++imported;
    }
    if (bands.empty()) return false;

    const Model defaults = defaultModel();
    for (int b = 0; b < kMaxBands; ++b) out.p.bands[b] = b < int(bands.size()) ? bands[size_t(b)] : defaults.p.bands[b];
    out.p.numBands = int(bands.size());
    out.p.eqEnabled = 1;
    out.p.preampDb = float(std::clamp(preamp, -24.0, 6.0));
    return true;
}

std::string exportParametricEQ(const Model& m) {
    std::string s;
    char buf[160];
    std::snprintf(buf, sizeof buf, "Preamp: %.1f dB\n", double(m.p.preampDb));
    s += buf;
    for (int b = 0; b < m.p.numBands; ++b) {
        const sq_band& band = m.p.bands[b];
        std::snprintf(buf, sizeof buf, "Filter %d: %s %s Fc %.0f Hz Gain %.1f dB Q %.2f\n", b + 1,
                      band.enabled ? "ON" : "OFF", codeFromBandType(band.type), double(band.freq),
                      double(band.gainDb), double(band.q));
        s += buf;
    }
    return s;
}

} // namespace sq
