// SpatialEQ VST3 plug-in: the SpatialEQ DSP engine as a stereo insert effect, with a web-view UI.
#pragma once

#include "params.h"
#include "spatialeq_dsp.h"

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include <atomic>
#include <string>

namespace sq {

class SpatialEQPlugin : public Steinberg::Vst::SingleComponentEffect {
public:
    SpatialEQPlugin();
    ~SpatialEQPlugin() override;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new SpatialEQPlugin);
    }

    // IComponent / IAudioProcessor
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs,
                                                     Steinberg::int32 numOuts) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;
    Steinberg::uint32 PLUGIN_API getTailSamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    // IEditController
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) override;

    // ---- Used by the editor (UI thread) ----
    sq_engine* engine() const { return engine_; }
    double sampleRate() const { return processSetup.sampleRate > 0 ? processSetup.sampleRate : 48000.0; }
    Model modelFromParameters();
    /// Moves every parameter that differs to `target` through the host (undoable, automatable).
    void applyModel(const Model& target);
    /// UI edit of one parameter in plain units. phase: "begin", "set", "end" or "once".
    void editPlain(ParamID id, double plain, const std::string& phase);
    /// UI commands: preset, import, export, addBand, removeBand, resetEq, resetSources.
    void command(const std::string& name, double arg, void* parentWindow);

    /// Set whenever any parameter changes (host automation, state load or UI); the editor clears it.
    std::atomic<bool> paramsChanged{true};
    std::atomic<bool> infoChanged{true};
    std::string presetName = "Flat"; // UI thread only

    OBJ_METHODS(SpatialEQPlugin, SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)

private:
    void loadModel(const Model& m);

    sq_engine* engine_ = nullptr;
    Model audio_;             // audio thread's copy
    bool audioDirty_ = true;  // audio thread: publish audio_ to the engine on the next block

    // State loads (UI thread) -> audio thread. The audio thread only ever try-locks.
    std::atomic_flag pendingLock_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> pendingDirty_{false};
    Model pending_;
};

} // namespace sq
