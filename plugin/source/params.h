// Parameter layout of the SpatialEQ VST3 plug-in, and the mapping between host-normalized values,
// plain values (the units the UI works in) and the DSP engine's sq_params.
// The IDs and plain ranges must match plugin/ui/src/lib/params.js.
#pragma once

#include "pluginterfaces/vst/vsttypes.h"
#include "spatialeq_dsp.h"

#include <string>
#include <vector>

namespace sq {

using Steinberg::Vst::ParamID;

enum : ParamID {
    kBypass = 0,
    kPreamp,
    kEqOn,
    kMode,
    kSpatialOn,
    kUpmix,
    kWidth,
    kXtcOn,
    kXtcStrength,
    kSpeakerSpan,
    kRoomSize,
    kReverbMix,
    kBassBoost,
    kDialogue,
    kLevelerOn,
    kLevelerTarget,
    kLimiterOn,
    kLimiterCeiling,
    kOutputGain,
    kHeadYaw,
    kNumBands,

    kBandBase = 100,   // + band * 10 + BandField
    kSourceBase = 300, // + source * 10 + SourceField
};

enum BandField { kBandOn, kBandType, kBandFreq, kBandGain, kBandQ };
enum SourceField { kSrcAzimuth, kSrcElevation, kSrcDistance, kSrcGain };

constexpr int kMaxBands = SQ_MAX_BANDS;
constexpr int kNumSources = 7; // L R C Ls Rs Lb Rb: enough for the 7.1 upmix

inline ParamID bandParam(int band, int field) { return kBandBase + ParamID(band * 10 + field); }
inline ParamID sourceParam(int source, int field) { return kSourceBase + ParamID(source * 10 + field); }

/// Everything the plug-in controls: the engine parameters plus the head yaw.
struct Model {
    sq_params p{};
    float headYawDeg = 0;
};

/// Plug-in defaults: the app's defaults (10 bands) with 16 band slots.
Model defaultModel();

enum class Scale { Linear, Log, Toggle, List };

struct ParamDef {
    ParamID id;
    std::string title;
    std::string units;
    double min = 0, max = 1;
    Scale scale = Scale::Linear;
    std::vector<std::string> items; // List entries
    int decimals = 1;
    double displayScale = 1; // 100 shows 0..1 as a percentage
    bool isBypass = false;
};

const std::vector<ParamDef>& paramDefs();
const ParamDef* findDef(ParamID id);

double toPlain(const ParamDef& d, double normalized);
double toNormalized(const ParamDef& d, double plain);
std::string formatPlain(const ParamDef& d, double plain);
bool parsePlain(const ParamDef& d, const std::string& text, double& plain);

/// Reads/writes one parameter of the model in plain units.
double readPlain(const Model& m, ParamID id);
void writePlain(Model& m, ParamID id, double plain);

inline double readNormalized(const Model& m, const ParamDef& d) { return toNormalized(d, readPlain(m, d.id)); }

} // namespace sq
