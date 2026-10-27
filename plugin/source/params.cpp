#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace sq {

namespace {

const char* kSourceNames[kNumSources] = {"L", "R", "C", "Ls", "Rs", "Lb", "Rb"};
const std::vector<std::string> kOnOff = {"Off", "On"};
const std::vector<std::string> kBandTypes = {"Peak", "Low Shelf", "High Shelf", "Low Pass", "High Pass"};
const float kDefaultAzimuths[kNumSources] = {-30, 30, 0, -110, 110, -150, 150};

ParamDef lin(ParamID id, std::string title, std::string units, double min, double max, int decimals = 1) {
    ParamDef d;
    d.id = id;
    d.title = std::move(title);
    d.units = std::move(units);
    d.min = min;
    d.max = max;
    d.decimals = decimals;
    return d;
}

ParamDef percent(ParamID id, std::string title, double max = 1) {
    ParamDef d = lin(id, std::move(title), "%", 0, max, 0);
    d.displayScale = 100;
    return d;
}

ParamDef logp(ParamID id, std::string title, std::string units, double min, double max, int decimals) {
    ParamDef d = lin(id, std::move(title), std::move(units), min, max, decimals);
    d.scale = Scale::Log;
    return d;
}

ParamDef toggle(ParamID id, std::string title) {
    ParamDef d = lin(id, std::move(title), "", 0, 1, 0);
    d.scale = Scale::Toggle;
    d.items = kOnOff;
    return d;
}

ParamDef list(ParamID id, std::string title, std::vector<std::string> items, double min = 0) {
    ParamDef d = lin(id, std::move(title), "", min, min + double(items.size() - 1), 0);
    d.scale = Scale::List;
    d.items = std::move(items);
    return d;
}

std::vector<ParamDef> buildDefs() {
    std::vector<ParamDef> v;
    ParamDef bypass = toggle(kBypass, "Bypass");
    bypass.isBypass = true;
    v.push_back(bypass);
    v.push_back(lin(kPreamp, "Preamp", "dB", -24, 6));
    v.push_back(toggle(kEqOn, "EQ"));
    v.push_back(list(kMode, "Output Mode", {"Headphones", "Speakers"}));
    v.push_back(toggle(kSpatialOn, "Virtual Surround"));
    v.push_back(list(kUpmix, "Upmix", {"Stereo", "Virtual 5.1", "Virtual 7.1"}));
    v.push_back(percent(kWidth, "Width", 2.5));
    v.push_back(toggle(kXtcOn, "Crosstalk Cancellation"));
    v.push_back(percent(kXtcStrength, "Crosstalk Strength"));
    v.push_back(lin(kSpeakerSpan, "Speaker Span", "deg", 10, 120, 0));
    v.push_back(percent(kRoomSize, "Room Size"));
    v.push_back(percent(kReverbMix, "Reverb"));
    v.push_back(lin(kBassBoost, "Bass Boost", "dB", 0, 12));
    v.push_back(percent(kDialogue, "Dialogue Boost"));
    v.push_back(toggle(kLevelerOn, "Volume Leveler"));
    v.push_back(lin(kLevelerTarget, "Target Loudness", "dB", -30, -10, 0));
    v.push_back(toggle(kLimiterOn, "Limiter"));
    v.push_back(lin(kLimiterCeiling, "Limiter Ceiling", "dB", -6, 0));
    v.push_back(lin(kOutputGain, "Output Gain", "dB", -12, 12));
    v.push_back(lin(kHeadYaw, "Head Rotation", "deg", -180, 180, 0));
    std::vector<std::string> counts;
    for (int i = 1; i <= kMaxBands; ++i) counts.push_back(std::to_string(i));
    v.push_back(list(kNumBands, "EQ Bands", counts, 1));

    for (int b = 0; b < kMaxBands; ++b) {
        const std::string n = "Band " + std::to_string(b + 1) + " ";
        v.push_back(toggle(bandParam(b, kBandOn), n + "On"));
        v.push_back(list(bandParam(b, kBandType), n + "Type", kBandTypes));
        v.push_back(logp(bandParam(b, kBandFreq), n + "Freq", "Hz", 10, 22000, 0));
        v.push_back(lin(bandParam(b, kBandGain), n + "Gain", "dB", -24, 24));
        v.push_back(logp(bandParam(b, kBandQ), n + "Q", "", 0.1, 20, 2));
    }
    for (int s = 0; s < kNumSources; ++s) {
        const std::string n = std::string("Source ") + kSourceNames[s] + " ";
        v.push_back(lin(sourceParam(s, kSrcAzimuth), n + "Azimuth", "deg", -180, 180, 0));
        v.push_back(lin(sourceParam(s, kSrcElevation), n + "Elevation", "deg", -60, 75, 0));
        v.push_back(lin(sourceParam(s, kSrcDistance), n + "Distance", "m", 0.3, 5, 2));
        v.push_back(lin(sourceParam(s, kSrcGain), n + "Level", "dB", -24, 6));
    }
    return v;
}

} // namespace

Model defaultModel() {
    Model m;
    sq_params_default(&m.p); // 10 bands: low shelf, 8 peaks, high shelf (same as the app)
    static const float kExtraFreqs[] = {100, 300, 700, 1500, 3000, 6000};
    for (int i = 10; i < kMaxBands; ++i) m.p.bands[i] = {1, SQ_BAND_PEAK, kExtraFreqs[i - 10], 0.0f, 1.0f};
    m.p.numBands = 10;
    for (int s = 0; s < kNumSources; ++s) m.p.sources[s] = {kDefaultAzimuths[s], 0.0f, 1.5f, 1.0f};
    return m;
}

const std::vector<ParamDef>& paramDefs() {
    static const std::vector<ParamDef> defs = buildDefs();
    return defs;
}

const ParamDef* findDef(ParamID id) {
    static const std::unordered_map<ParamID, const ParamDef*> index = [] {
        std::unordered_map<ParamID, const ParamDef*> m;
        for (const auto& d : paramDefs()) m[d.id] = &d;
        return m;
    }();
    const auto it = index.find(id);
    return it == index.end() ? nullptr : it->second;
}

double toPlain(const ParamDef& d, double n) {
    n = std::clamp(n, 0.0, 1.0);
    switch (d.scale) {
    case Scale::Log: return d.min * std::pow(d.max / d.min, n);
    case Scale::Toggle:
    case Scale::List: return std::round(d.min + n * (d.max - d.min));
    case Scale::Linear: break;
    }
    return d.min + n * (d.max - d.min);
}

double toNormalized(const ParamDef& d, double plain) {
    plain = std::clamp(plain, d.min, d.max);
    if (d.max <= d.min) return 0;
    if (d.scale == Scale::Log) return std::log(plain / d.min) / std::log(d.max / d.min);
    return (plain - d.min) / (d.max - d.min);
}

std::string formatPlain(const ParamDef& d, double plain) {
    if (d.scale == Scale::Toggle || d.scale == Scale::List) {
        const int i = std::clamp(int(std::lround(plain - d.min)), 0, int(d.items.size()) - 1);
        return d.items[size_t(i)];
    }
    char buf[64];
    const double shown = plain * d.displayScale;
    if (d.units == "Hz" && plain >= 1000)
        std::snprintf(buf, sizeof buf, "%.2f kHz", plain / 1000.0);
    else if (d.units.empty())
        std::snprintf(buf, sizeof buf, "%.*f", d.decimals, shown);
    else
        std::snprintf(buf, sizeof buf, "%.*f %s", d.decimals, shown, d.units.c_str());
    return buf;
}

bool parsePlain(const ParamDef& d, const std::string& text, double& plain) {
    if (d.scale == Scale::Toggle || d.scale == Scale::List) {
        for (size_t i = 0; i < d.items.size(); ++i)
            if (text == d.items[i]) {
                plain = d.min + double(i);
                return true;
            }
    }
    char* end = nullptr;
    double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) return false;
    while (*end == ' ') ++end;
    if (d.units == "Hz" && (*end == 'k' || *end == 'K')) v *= 1000.0;
    plain = std::clamp(v / d.displayScale, d.min, d.max);
    return true;
}

double readPlain(const Model& m, ParamID id) {
    const sq_params& p = m.p;
    if (id >= kSourceBase) {
        const int s = int(id - kSourceBase) / 10, f = int(id - kSourceBase) % 10;
        if (s >= kNumSources) return 0;
        const sq_source& src = p.sources[s];
        switch (f) {
        case kSrcAzimuth: return src.azimuthDeg;
        case kSrcElevation: return src.elevationDeg;
        case kSrcDistance: return src.distance;
        case kSrcGain: return src.gain > 0 ? 20.0 * std::log10(double(src.gain)) : -24.0;
        default: return 0;
        }
    }
    if (id >= kBandBase) {
        const int b = int(id - kBandBase) / 10, f = int(id - kBandBase) % 10;
        if (b >= kMaxBands) return 0;
        const sq_band& band = p.bands[b];
        switch (f) {
        case kBandOn: return band.enabled ? 1 : 0;
        case kBandType: return band.type;
        case kBandFreq: return band.freq;
        case kBandGain: return band.gainDb;
        case kBandQ: return band.q;
        default: return 0;
        }
    }
    switch (id) {
    case kBypass: return p.enabled ? 0 : 1;
    case kPreamp: return p.preampDb;
    case kEqOn: return p.eqEnabled ? 1 : 0;
    case kMode: return p.mode;
    case kSpatialOn: return p.spatialEnabled ? 1 : 0;
    case kUpmix: return p.upmix;
    case kWidth: return p.width;
    case kXtcOn: return p.xtcEnabled ? 1 : 0;
    case kXtcStrength: return p.xtcStrength;
    case kSpeakerSpan: return p.speakerSpanDeg;
    case kRoomSize: return p.roomSize;
    case kReverbMix: return p.reverbMix;
    case kBassBoost: return p.bassBoostDb;
    case kDialogue: return p.dialogueBoost;
    case kLevelerOn: return p.levelerEnabled ? 1 : 0;
    case kLevelerTarget: return p.levelerTargetDb;
    case kLimiterOn: return p.limiterEnabled ? 1 : 0;
    case kLimiterCeiling: return p.limiterCeilingDb;
    case kOutputGain: return p.outputGainDb;
    case kHeadYaw: return m.headYawDeg;
    case kNumBands: return p.numBands;
    default: return 0;
    }
}

void writePlain(Model& m, ParamID id, double v) {
    sq_params& p = m.p;
    const float f = float(v);
    const int on = v > 0.5 ? 1 : 0;
    if (id >= kSourceBase) {
        const int s = int(id - kSourceBase) / 10, field = int(id - kSourceBase) % 10;
        if (s >= kNumSources) return;
        sq_source& src = p.sources[s];
        switch (field) {
        case kSrcAzimuth: src.azimuthDeg = f; break;
        case kSrcElevation: src.elevationDeg = f; break;
        case kSrcDistance: src.distance = f; break;
        case kSrcGain: src.gain = float(std::pow(10.0, v / 20.0)); break;
        default: break;
        }
        return;
    }
    if (id >= kBandBase) {
        const int b = int(id - kBandBase) / 10, field = int(id - kBandBase) % 10;
        if (b >= kMaxBands) return;
        sq_band& band = p.bands[b];
        switch (field) {
        case kBandOn: band.enabled = on; break;
        case kBandType: band.type = int(std::lround(v)); break;
        case kBandFreq: band.freq = f; break;
        case kBandGain: band.gainDb = f; break;
        case kBandQ: band.q = f; break;
        default: break;
        }
        return;
    }
    switch (id) {
    case kBypass: p.enabled = on ? 0 : 1; break;
    case kPreamp: p.preampDb = f; break;
    case kEqOn: p.eqEnabled = on; break;
    case kMode: p.mode = int(std::lround(v)); break;
    case kSpatialOn: p.spatialEnabled = on; break;
    case kUpmix: p.upmix = int(std::lround(v)); break;
    case kWidth: p.width = f; break;
    case kXtcOn: p.xtcEnabled = on; break;
    case kXtcStrength: p.xtcStrength = f; break;
    case kSpeakerSpan: p.speakerSpanDeg = f; break;
    case kRoomSize: p.roomSize = f; break;
    case kReverbMix: p.reverbMix = f; break;
    case kBassBoost: p.bassBoostDb = f; break;
    case kDialogue: p.dialogueBoost = f; break;
    case kLevelerOn: p.levelerEnabled = on; break;
    case kLevelerTarget: p.levelerTargetDb = f; break;
    case kLimiterOn: p.limiterEnabled = on; break;
    case kLimiterCeiling: p.limiterCeilingDb = f; break;
    case kOutputGain: p.outputGainDb = f; break;
    case kHeadYaw: m.headYawDeg = f; break;
    case kNumBands: p.numBands = std::clamp(int(std::lround(v)), 1, kMaxBands); break;
    default: break;
    }
}

} // namespace sq
