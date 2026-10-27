// Application state for the Windows app: settings, presets, device routing and persistence.
// Everything here runs on the UI thread; the audio thread lives inside AudioRouter.
#pragma once

#include "AudioRouter.h"
#include "Devices.h"
#include "Ducker.h"
#include "Settings.h"
#include "spatialeq_spectrum.h"

#include <chrono>
#include <string>
#include <vector>

namespace sq::app {

class App {
public:
    static constexpr int kSpectrumBands = 64; // as the UI and the Mac app draw

    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // `hwnd` receives WM_APP+1 when audio devices change.
    void start(void* hwnd);
    // Once per UI frame: device changes, route restarts, parameter updates, metering, autosave.
    void tick();
    void shutdown();

    // ---- Sound settings (edit `settings` directly; tick() pushes changes to the engine)
    SoundSettings settings;
    bool enabled = true;
    double headYawDeg = 0; // turns the listener's head (the UI's head rotation slider); not saved
    int selectedBand = -1;

    // ---- Presets
    const std::vector<Preset>& builtIns() const { return builtIns_; }
    const std::vector<Preset>& userPresets() const { return userPresets_; }
    const std::string& presetName() const { return presetName_; }
    bool presetIsUser() const;
    bool presetModified() const { return settings != presetBaseline_; }
    void applyPreset(const Preset& p);
    void saveAsPreset(const std::string& name);
    void updateCurrentPreset();
    void deleteCurrentPreset();
    void resetEq();
    // Returns an empty string on success, otherwise the reason.
    std::string importAutoEq(const std::wstring& path);
    std::string exportAutoEq(const std::wstring& path) const;

    // ---- Routing. Empty ids mean "automatic".
    DeviceList& devices() { return devices_; }
    const std::wstring& sourceChoice() const { return sourceChoice_; }
    const std::wstring& outputChoice() const { return outputChoice_; }
    void setSource(const std::wstring& id);
    void setOutput(const std::wstring& id);
    std::wstring resolvedSource() const;
    std::wstring resolvedOutput() const;
    void restartRoute();
    AudioRouter::Status routeStatus() const { return router_.status(); }
    // "All apps" source (no cable needed): available on Windows 10 2004+.
    bool allAppsAvailable() const { return processLoopbackSupported(); }
    bool capturingAllApps() const { return runningSource_ == kAllAppsSource; }
    int appsInMix() const { return ducker_.duckedCount(); }
    // Puts every app volume SpatialEQ may have lowered back; returns how many changed.
    int resetAppVolumes() { return ducker_.resetAll(); }

    // ---- Live analysis (refreshed by tick)
    const float* spectrum() const { return spectrumBands_; }
    float spectrumFrequency(int band) const { return sq_spectrum_band_frequency(spectrum_, band); }
    float level() const { return level_; }
    const sq_meters& meters() const { return meters_; }
    double sampleRate() const { return sampleRate_; }

    // ---- App options
    bool closeToTray = true;
    bool startWithWindows() const { return startWithWindows_; }
    void setStartWithWindows(bool on);

    // Last user-facing error (import failures etc.); the UI shows and clears it.
    std::string lastError;

private:
    void load();
    void save();
    void pushParams();

    sq_engine* engine_ = nullptr;
    sq_spectrum* spectrum_ = nullptr;
    DeviceList devices_;
    AudioRouter router_;
    SessionDucker ducker_;
    std::chrono::steady_clock::time_point lastDuckPoll_{};

    std::vector<Preset> builtIns_, userPresets_;
    std::string presetName_ = "Flat";
    SoundSettings presetBaseline_;

    std::wstring sourceChoice_, outputChoice_;
    std::wstring runningSource_, runningOutput_;
    int failures_ = 0;
    std::chrono::steady_clock::time_point retryAt_{};

    sq_params lastPushed_{};
    double lastYaw_ = 0;
    bool lastEnabled_ = true;
    bool havePushed_ = false;

    float spectrumBands_[kSpectrumBands]{};
    float level_ = 0;
    sq_meters meters_{};
    double sampleRate_ = 48000;
    std::chrono::steady_clock::time_point lastSpectrum_{};

    std::string savedSnapshot_;
    std::chrono::steady_clock::time_point dirtySince_{};
    bool dirty_ = false;
    bool startWithWindows_ = false;
    bool started_ = false;

    std::string snapshot() const;
};

} // namespace sq::app
