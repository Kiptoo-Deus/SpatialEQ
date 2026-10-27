#include "plugin.h"

#include "filedialog.h"
#include "presets.h"
#include "version.h"
#include "webeditor.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace sq {

namespace {

constexpr uint32 kStateMagic = 0x53514551; // 'SQEQ'
constexpr int32 kStateVersion = 2;         // 2: + preset name

/// A host parameter that converts and formats through the ParamDef table (log frequencies, dB, lists).
class SqParameter : public Parameter {
public:
    SqParameter(const ParamDef& d, double defaultNormalized) : def_(d) {
        StringConvert::convert(d.title, info.title);
        StringConvert::convert(d.title, info.shortTitle);
        StringConvert::convert(d.units, info.units);
        info.id = d.id;
        info.stepCount = (d.scale == Scale::Toggle || d.scale == Scale::List) ? int32(d.max - d.min) : 0;
        info.defaultNormalizedValue = defaultNormalized;
        info.unitId = kRootUnitId;
        info.flags = ParameterInfo::kCanAutomate;
        if (d.scale == Scale::List) info.flags |= ParameterInfo::kIsList;
        if (d.isBypass) info.flags |= ParameterInfo::kIsBypass;
        setNormalized(defaultNormalized);
    }

    void toString(ParamValue normalized, String128 string) const override {
        StringConvert::convert(formatPlain(def_, sq::toPlain(def_, normalized)), string);
    }
    bool fromString(const TChar* string, ParamValue& normalized) const override {
        double plain = 0;
        if (!parsePlain(def_, StringConvert::convert(string), plain)) return false;
        normalized = sq::toNormalized(def_, plain);
        return true;
    }
    ParamValue toPlain(ParamValue normalized) const override { return sq::toPlain(def_, normalized); }
    ParamValue toNormalized(ParamValue plain) const override { return sq::toNormalized(def_, plain); }

private:
    const ParamDef& def_;
};

} // namespace

SpatialEQPlugin::SpatialEQPlugin() {
    engine_ = sq_engine_create();
    audio_ = defaultModel();
    pending_ = audio_;
}

SpatialEQPlugin::~SpatialEQPlugin() { sq_engine_destroy(engine_); }

tresult PLUGIN_API SpatialEQPlugin::initialize(FUnknown* context) {
    const tresult result = SingleComponentEffect::initialize(context);
    if (result != kResultOk) return result;
    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    const Model defaults = defaultModel();
    for (const ParamDef& d : paramDefs()) parameters.addParameter(new SqParameter(d, readNormalized(defaults, d)));
    return kResultOk;
}

tresult PLUGIN_API SpatialEQPlugin::terminate() { return SingleComponentEffect::terminate(); }

tresult PLUGIN_API SpatialEQPlugin::setupProcessing(ProcessSetup& setup) {
    const tresult r = SingleComponentEffect::setupProcessing(setup);
    sq_engine_prepare(engine_, setup.sampleRate, setup.maxSamplesPerBlock);
    infoChanged = true;
    return r;
}

tresult PLUGIN_API SpatialEQPlugin::setActive(TBool state) {
    if (state) {
        sq_engine_prepare(engine_, processSetup.sampleRate, processSetup.maxSamplesPerBlock);
        sq_engine_set_params(engine_, &audio_.p);
        sq_engine_set_head_yaw(engine_, audio_.headYawDeg);
        sq_engine_reset(engine_);
        audioDirty_ = false;
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API SpatialEQPlugin::canProcessSampleSize(int32 symbolicSampleSize) {
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API SpatialEQPlugin::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                       SpeakerArrangement* outputs, int32 numOuts) {
    if (numIns == 1 && numOuts == 1 && inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo)
        return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

uint32 PLUGIN_API SpatialEQPlugin::getLatencySamples() { return uint32(sq_engine_latency(engine_)); }

uint32 PLUGIN_API SpatialEQPlugin::getTailSamples() { return uint32(processSetup.sampleRate * 2.5); } // room reverb

tresult PLUGIN_API SpatialEQPlugin::process(ProcessData& data) {
    // 1) Parameter changes (automation and UI edits); last point per block, the engine smooths.
    if (IParameterChanges* changes = data.inputParameterChanges) {
        const int32 count = changes->getParameterCount();
        for (int32 i = 0; i < count; ++i) {
            IParamValueQueue* queue = changes->getParameterData(i);
            if (!queue || queue->getPointCount() <= 0) continue;
            ParamValue value = 0;
            int32 offset = 0;
            if (queue->getPoint(queue->getPointCount() - 1, offset, value) != kResultTrue) continue;
            if (const ParamDef* d = findDef(queue->getParameterId())) {
                writePlain(audio_, d->id, toPlain(*d, value));
                audioDirty_ = true;
            }
        }
    }

    // 2) A state load from the UI thread; try-lock only, never wait.
    if (pendingDirty_.load(std::memory_order_acquire) && !pendingLock_.test_and_set(std::memory_order_acquire)) {
        audio_ = pending_;
        pendingDirty_.store(false, std::memory_order_relaxed);
        pendingLock_.clear(std::memory_order_release);
        audioDirty_ = true;
    }

    if (audioDirty_) {
        sq_engine_set_params(engine_, &audio_.p);
        sq_engine_set_head_yaw(engine_, audio_.headYawDeg);
        audioDirty_ = false;
    }

    // 3) Audio.
    if (data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1) return kResultOk;
    AudioBusBuffers& in = data.inputs[0];
    AudioBusBuffers& out = data.outputs[0];
    if (in.numChannels < 2 || out.numChannels < 2) return kResultOk;
    float* outL = out.channelBuffers32[0];
    float* outR = out.channelBuffers32[1];
    const size_t bytes = sizeof(float) * size_t(data.numSamples);
    if (in.channelBuffers32[0] != outL) std::memcpy(outL, in.channelBuffers32[0], bytes);
    if (in.channelBuffers32[1] != outR) std::memcpy(outR, in.channelBuffers32[1], bytes);
    sq_engine_process(engine_, outL, outR, data.numSamples);
    out.silenceFlags = 0;
    return kResultOk;
}

// ---- State: (id, normalized value) pairs so future versions can add parameters; then the preset name.

tresult PLUGIN_API SpatialEQPlugin::getState(IBStream* state) {
    IBStreamer s(state, kLittleEndian);
    const auto& defs = paramDefs();
    s.writeInt32u(kStateMagic);
    s.writeInt32(kStateVersion);
    s.writeInt32(int32(defs.size()));
    for (const ParamDef& d : defs) {
        s.writeInt32u(d.id);
        s.writeDouble(getParamNormalized(d.id));
    }
    s.writeStr8(presetName.c_str());
    return kResultOk;
}

tresult PLUGIN_API SpatialEQPlugin::setState(IBStream* state) {
    IBStreamer s(state, kLittleEndian);
    uint32 magic = 0;
    int32 version = 0, count = 0;
    if (!s.readInt32u(magic) || magic != kStateMagic || !s.readInt32(version) || !s.readInt32(count) || count < 0)
        return kResultFalse;
    Model m = defaultModel();
    for (int32 i = 0; i < count; ++i) {
        uint32 id = 0;
        double value = 0;
        if (!s.readInt32u(id) || !s.readDouble(value)) return kResultFalse;
        if (const ParamDef* d = findDef(id)) writePlain(m, id, toPlain(*d, value));
    }
    if (version >= 2)
        if (char8* name = s.readStr8()) {
            presetName = name;
            delete[] name;
        }
    loadModel(m);
    infoChanged = true;
    return kResultOk;
}

void SpatialEQPlugin::loadModel(const Model& m) {
    for (const ParamDef& d : paramDefs()) setParamNormalized(d.id, readNormalized(m, d));
    while (pendingLock_.test_and_set(std::memory_order_acquire)) {
    } // the audio thread holds it only for one struct copy
    pending_ = m;
    pendingDirty_.store(true, std::memory_order_release);
    pendingLock_.clear(std::memory_order_release);
}

tresult PLUGIN_API SpatialEQPlugin::setParamNormalized(ParamID tag, ParamValue value) {
    const tresult r = SingleComponentEffect::setParamNormalized(tag, value);
    paramsChanged = true;
    return r;
}

// ---- Editing from the UI

Model SpatialEQPlugin::modelFromParameters() {
    Model m = defaultModel();
    for (const ParamDef& d : paramDefs()) writePlain(m, d.id, toPlain(d, getParamNormalized(d.id)));
    return m;
}

void SpatialEQPlugin::editPlain(ParamID id, double plain, const std::string& phase) {
    const ParamDef* d = findDef(id);
    if (!d) return;
    if (phase == "begin") { beginEdit(id); return; }
    if (phase == "end") { endEdit(id); return; }
    const double n = toNormalized(*d, plain);
    if (phase == "once") beginEdit(id);
    setParamNormalized(id, n);
    performEdit(id, getParamNormalized(id));
    if (phase == "once") endEdit(id);
}

void SpatialEQPlugin::applyModel(const Model& target) {
    for (const ParamDef& d : paramDefs()) {
        const double n = readNormalized(target, d);
        if (std::fabs(n - getParamNormalized(d.id)) > 1e-7) {
            beginEdit(d.id);
            setParamNormalized(d.id, n);
            performEdit(d.id, getParamNormalized(d.id));
            endEdit(d.id);
        }
    }
}

void SpatialEQPlugin::command(const std::string& name, double arg, void* parentWindow) {
    Model m = modelFromParameters();
    if (name == "preset") {
        const auto& presets = builtInPresets();
        const int i = int(arg);
        if (i < 0 || i >= int(presets.size())) return;
        Model target = presets[size_t(i)].model;
        target.p.enabled = m.p.enabled; // presets never toggle bypass
        target.headYawDeg = m.headYawDeg;
        applyModel(target);
        presetName = presets[size_t(i)].name;
        infoChanged = true;
    } else if (name == "import") {
        const std::string path = chooseFile(parentWindow, false, "Import AutoEq / Equalizer APO file", "");
        if (path.empty()) return;
        std::ifstream file(path);
        std::stringstream text;
        text << file.rdbuf();
        Model imported;
        int used = 0, skipped = 0;
        if (!importParametricEQ(text.str(), m, imported, used, skipped)) return;
        applyModel(imported);
        const size_t slash = path.find_last_of("/\\");
        presetName = path.substr(slash == std::string::npos ? 0 : slash + 1);
        infoChanged = true;
    } else if (name == "export") {
        const std::string path = chooseFile(parentWindow, true, "Export EQ", "ParametricEQ.txt");
        if (path.empty()) return;
        std::ofstream(path) << exportParametricEQ(m);
    } else if (name == "addBand") {
        const int n = m.p.numBands;
        if (n >= kMaxBands) return;
        m.p.bands[n] = {1, SQ_BAND_PEAK, 1000.0f, 0.0f, 1.0f};
        m.p.numBands = n + 1;
        applyModel(m);
    } else if (name == "removeBand") {
        const int i = int(arg), n = m.p.numBands;
        if (i < 0 || i >= n || n <= 1) return;
        for (int b = i; b < n - 1; ++b) m.p.bands[b] = m.p.bands[b + 1];
        m.p.numBands = n - 1;
        applyModel(m);
    } else if (name == "resetEq") {
        const Model d = defaultModel();
        std::memcpy(m.p.bands, d.p.bands, sizeof m.p.bands);
        m.p.numBands = d.p.numBands;
        m.p.preampDb = 0;
        applyModel(m);
    } else if (name == "resetSources") {
        const Model d = defaultModel();
        std::memcpy(m.p.sources, d.p.sources, sizeof m.p.sources);
        applyModel(m);
    } else if (name == "ready") {
        paramsChanged = true;
        infoChanged = true;
    }
}

IPlugView* PLUGIN_API SpatialEQPlugin::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor)) return new WebEditor(this);
    return nullptr;
}

} // namespace sq

// ---- Factory

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID(0xC8E2E1A4, 0x112144A3, 0xBB102BDF, 0xBD9F195B),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           0, // single-component effect
           "Fx|EQ|Spatial",
           FULL_VERSION_STR,
           kVstVersionString,
           sq::SpatialEQPlugin::createInstance)

END_FACTORY
