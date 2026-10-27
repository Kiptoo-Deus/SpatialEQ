// choc's WebView is built the way the VST3 builds it: full windows.h, ANSI macros. This must come first,
// before anything includes windows.h.
#undef UNICODE
#undef _UNICODE
#undef WIN32_LEAN_AND_MEAN
#include "choc/gui/choc_WebView.h"
#include "choc/text/choc_JSON.h"

#include "WebUi.h"

#include "filedialog.h" // plugin/source: file dialogs and child-window embedding (shared with the VST3)
#include "ui_html.h"    // generated from plugin/ui/dist/index.html by CMake

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace sq::app {

namespace {

// Parameter IDs: must match plugin/ui/src/lib/params.js.
enum : int {
    kBypass = 0, kPreamp, kEqOn, kMode, kSpatialOn, kUpmix, kWidth, kXtcOn, kXtcStrength, kSpeakerSpan,
    kRoomSize, kReverbMix, kBassBoost, kDialogue, kLevelerOn, kLevelerTarget, kLimiterOn, kLimiterCeiling,
    kOutputGain, kHeadYaw, kNumBands,
    kBandBase = 100,   // + band * 10 + field: on, type, freq, gain, q
    kSourceBase = 300, // + source * 10 + field: azimuth, elevation, distance, level (dB)
};
constexpr int kUiBands = 16, kUiSources = 7;

std::vector<int> allParamIds() {
    std::vector<int> ids;
    for (int i = kBypass; i <= kNumBands; ++i) ids.push_back(i);
    for (int b = 0; b < kUiBands; ++b)
        for (int f = 0; f < 5; ++f) ids.push_back(kBandBase + b * 10 + f);
    for (int s = 0; s < kUiSources; ++s)
        for (int f = 0; f < 4; ++f) ids.push_back(kSourceBase + s * 10 + f);
    return ids;
}

double toDb(double gain) { return 20 * std::log10(std::max(gain, 1e-6)); }

double getParam(const App& app, int id) {
    const SoundSettings& s = app.settings;
    if (id >= kSourceBase) {
        const int i = (id - kSourceBase) / 10, f = (id - kSourceBase) % 10;
        if (i >= int(s.sources.size())) return 0;
        const VirtualSource& v = s.sources[size_t(i)];
        return f == 0 ? v.azimuth : f == 1 ? v.elevation : f == 2 ? v.distance : toDb(v.gain);
    }
    if (id >= kBandBase) {
        const int b = (id - kBandBase) / 10, f = (id - kBandBase) % 10;
        if (b >= int(s.bands.size())) { // unused slot: the plug-in's defaults
            static const double freqs[] = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000, 100, 300, 700, 1500, 3000, 6000};
            return f == 0 ? 1 : f == 1 ? 0 : f == 2 ? freqs[b % 16] : f == 3 ? 0 : 1;
        }
        const EqBand& e = s.bands[size_t(b)];
        return f == 0 ? (e.enabled ? 1 : 0) : f == 1 ? double(int(e.type)) : f == 2 ? e.frequency : f == 3 ? e.gainDb : e.q;
    }
    switch (id) {
    case kBypass: return app.enabled ? 0 : 1;
    case kPreamp: return s.preampDb;
    case kEqOn: return s.eqEnabled;
    case kMode: return double(int(s.mode));
    case kSpatialOn: return s.spatialEnabled;
    case kUpmix: return double(int(s.upmix));
    case kWidth: return s.width;
    case kXtcOn: return s.crosstalkEnabled;
    case kXtcStrength: return s.crosstalkStrength;
    case kSpeakerSpan: return s.speakerSpan;
    case kRoomSize: return s.roomSize;
    case kReverbMix: return s.reverbMix;
    case kBassBoost: return s.bassBoostDb;
    case kDialogue: return s.dialogueBoost;
    case kLevelerOn: return s.levelerEnabled;
    case kLevelerTarget: return s.levelerTargetDb;
    case kLimiterOn: return s.limiterEnabled;
    case kLimiterCeiling: return s.limiterCeilingDb;
    case kOutputGain: return s.outputGainDb;
    case kHeadYaw: return app.headYawDeg;
    case kNumBands: return double(s.bands.size());
    default: return 0;
    }
}

void setParam(App& app, int id, double v) {
    SoundSettings& s = app.settings;
    const bool on = v >= 0.5;
    if (id >= kSourceBase) {
        const int i = (id - kSourceBase) / 10, f = (id - kSourceBase) % 10;
        if (i >= int(s.sources.size())) return;
        VirtualSource& src = s.sources[size_t(i)];
        if (f == 0) src.azimuth = v;
        else if (f == 1) src.elevation = v;
        else if (f == 2) src.distance = v;
        else src.gain = std::pow(10.0, v / 20);
        return;
    }
    if (id >= kBandBase) {
        const int b = (id - kBandBase) / 10, f = (id - kBandBase) % 10;
        if (b >= int(s.bands.size())) return;
        EqBand& e = s.bands[size_t(b)];
        if (f == 0) e.enabled = on;
        else if (f == 1) e.type = BandType(std::clamp(int(std::lround(v)), 0, 4));
        else if (f == 2) e.frequency = v;
        else if (f == 3) e.gainDb = v;
        else e.q = v;
        return;
    }
    switch (id) {
    case kBypass: app.enabled = !on; break;
    case kPreamp: s.preampDb = v; break;
    case kEqOn: s.eqEnabled = on; break;
    case kMode: s.mode = OutputMode(std::clamp(int(std::lround(v)), 0, 1)); break;
    case kSpatialOn: s.spatialEnabled = on; break;
    case kUpmix: s.upmix = Upmix(std::clamp(int(std::lround(v)), 0, 2)); break;
    case kWidth: s.width = v; break;
    case kXtcOn: s.crosstalkEnabled = on; break;
    case kXtcStrength: s.crosstalkStrength = v; break;
    case kSpeakerSpan: s.speakerSpan = v; break;
    case kRoomSize: s.roomSize = v; break;
    case kReverbMix: s.reverbMix = v; break;
    case kBassBoost: s.bassBoostDb = v; break;
    case kDialogue: s.dialogueBoost = v; break;
    case kLevelerOn: s.levelerEnabled = on; break;
    case kLevelerTarget: s.levelerTargetDb = v; break;
    case kLimiterOn: s.limiterEnabled = on; break;
    case kLimiterCeiling: s.limiterCeilingDb = v; break;
    case kOutputGain: s.outputGainDb = v; break;
    case kHeadYaw: app.headYawDeg = v; break;
    case kNumBands: {
        const size_t n = size_t(std::clamp(int(std::lround(v)), 1, std::min(kUiBands, int(SoundSettings::kMaxBands))));
        s.bands.resize(n, EqBand{});
        break;
    }
    default: break;
    }
}

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

std::string quoted(const std::string& s) { return choc::json::getEscapedQuotedString(s); }

std::string argString(const choc::value::ValueView& args, uint32_t i) {
    if (!args.isArray() || args.size() <= i) return {};
    if (args[i].isString()) return std::string(args[i].getString());
    return {};
}

double argNumber(const choc::value::ValueView& args, uint32_t i) {
    if (!args.isArray() || args.size() <= i) return 0;
    return args[i].getWithDefault<double>(0);
}

void openSoundSettings() { ShellExecuteW(nullptr, L"open", L"ms-settings:sound", nullptr, nullptr, SW_SHOWNORMAL); }

} // namespace

struct WebUi::Impl {
    App& app;
    void* parent;
    std::unique_ptr<choc::ui::WebView> webview;
    bool ready = false;
    std::vector<int> ids = allParamIds();
    std::vector<double> sent; // values last sent to the page (empty: send everything)
    std::string sentApp, sentInfo;

    Impl(App& a, void* p) : app(a), parent(p) {
        choc::ui::WebView::Options options;
        options.acceptsFirstMouseClick = true;
#ifndef NDEBUG
        options.enableDebugMode = true;
#endif
        options.fetchResource = [](const std::string& path) -> std::optional<choc::ui::WebView::Options::Resource> {
            if (path == "/" || path == "/index.html")
                return choc::ui::WebView::Options::Resource(
                    std::string_view(reinterpret_cast<const char*>(kUiHtml), sizeof kUiHtml), "text/html");
            return {};
        };
        webview = std::make_unique<choc::ui::WebView>(options);
        webview->bind("sqEdit", [this](const choc::value::ValueView& args) -> choc::value::Value {
            setParam(app, int(argNumber(args, 0)), argNumber(args, 1));
            return {};
        });
        webview->bind("sqCommand", [this](const choc::value::ValueView& args) -> choc::value::Value {
            command(argString(args, 0), args);
            return {};
        });
        RECT r{};
        GetClientRect(static_cast<HWND>(parent), &r);
        embedChildView(parent, webview->getViewHandle(), r.right - r.left, r.bottom - r.top);
    }

    void command(const std::string& name, const choc::value::ValueView& args) {
        const int index = int(argNumber(args, 1));
        SoundSettings& s = app.settings;
        if (name == "ready") {
            ready = true;
            sent.clear();
            sentApp.clear();
            sentInfo.clear();
        } else if (name == "preset") {
            if (index >= 0 && index < int(app.builtIns().size())) app.applyPreset(app.builtIns()[size_t(index)]);
        } else if (name == "userPreset") {
            if (index >= 0 && index < int(app.userPresets().size())) app.applyPreset(app.userPresets()[size_t(index)]);
        } else if (name == "savePreset") {
            app.saveAsPreset(argString(args, 1));
        } else if (name == "updatePreset") {
            app.updateCurrentPreset();
        } else if (name == "deletePreset") {
            app.deleteCurrentPreset();
        } else if (name == "import") {
            const std::string path = chooseFile(parent, false, "Import AutoEq / Equalizer APO file", "");
            if (!path.empty()) app.lastError = app.importAutoEq(fromUtf8(path));
        } else if (name == "export") {
            const std::string path = chooseFile(parent, true, "Export EQ", "ParametricEQ.txt");
            if (!path.empty()) app.lastError = app.exportAutoEq(fromUtf8(path));
        } else if (name == "addBand") {
            if (int(s.bands.size()) < std::min(kUiBands, int(SoundSettings::kMaxBands))) s.bands.push_back(EqBand{});
        } else if (name == "removeBand") {
            if (s.bands.size() > 1 && index >= 0 && index < int(s.bands.size()))
                s.bands.erase(s.bands.begin() + index);
        } else if (name == "resetEq") {
            app.resetEq();
        } else if (name == "resetSources") {
            s.resetSources();
        } else if (name == "setSource") {
            app.setSource(fromUtf8(argString(args, 1)));
        } else if (name == "setOutput") {
            app.setOutput(fromUtf8(argString(args, 1)));
        } else if (name == "retry") {
            app.restartRoute();
        } else if (name == "soundSettings") {
            openSoundSettings();
        } else if (name == "resetAppVolumes") {
            app.resetAppVolumes();
        } else if (name == "closeToTray") {
            app.closeToTray = index != 0;
        } else if (name == "startWithWindows") {
            app.setStartWithWindows(index != 0);
        } else if (name == "clearError") {
            app.lastError.clear();
        }
    }

    /// The device line under the title, or what's wrong with the routing (the old interface's notice).
    std::pair<std::string, bool> routingNotice() const {
        const std::wstring srcId = app.resolvedSource(), outId = app.resolvedOutput();
        if (srcId == kAllAppsSource && app.devices().find(outId)) return {};
        const AudioDevice* src = app.devices().find(srcId);
        const AudioDevice* out = app.devices().find(outId);
        const AudioDevice* def = app.devices().defaultDevice();
        if (!src && !out) return {"No playback device found.", true};
        if (!src) return {"Choose what to capture.", false};
        if (!out)
            return {"Add a second playback device (headphones, speakers or a virtual cable) for the processed sound.", true};
        if (!src->isVirtual)
            return {"Capturing \"" + src->name + "\" directly, so its unprocessed sound is still audible." +
                        (app.allAppsAvailable() ? " Choose \"All apps\" to process everything instead."
                                                : " Use a virtual cable such as VB-CABLE as the Windows default device."),
                    !app.allAppsAvailable()};
        if (def && def->id != src->id)
            return {"Apps are playing to \"" + def->name + "\". Make \"" + src->name + "\" the default playback device.", true};
        return {};
    }

    std::string appState() const {
        const auto st = app.routeStatus();
        const std::wstring src = app.resolvedSource(), out = app.resolvedOutput();
        std::string state = "running", text;
        const auto [notice, settings] = routingNotice();
        switch (st.state) {
        case AudioRouter::State::Stopped: state = "stopped"; text = "Stopped"; break;
        case AudioRouter::State::Starting: state = "stopped"; text = "Starting…"; break;
        case AudioRouter::State::Failed: state = "failed"; text = st.message; break;
        case AudioRouter::State::Running: {
            const AudioDevice* d = app.devices().find(out);
            char buf[64];
            std::snprintf(buf, sizeof buf, " · %.0f kHz · %.0f ms", st.sampleRate / 1000.0, st.latencyMs);
            text = (d ? d->name : std::string("Output")) + buf;
            if (app.capturingAllApps()) {
                const int n = app.appsInMix();
                text += " · " + std::to_string(n) + (n == 1 ? " app" : " apps");
            }
            break;
        }
        }
        if (!notice.empty() && st.state != AudioRouter::State::Failed) {
            state = "failed";
            text = notice;
        }

        std::string devices = "\"outputs\":[", sources = "\"sources\":[";
        for (const AudioDevice& d : app.devices().devices()) {
            const std::string item = "{\"id\":" + quoted(toUtf8(d.id)) + ",\"name\":" + quoted(d.name) + "}";
            if (d.id != src || src == kAllAppsSource) devices += item + ",";
            if (d.id != out) sources += item + ",";
        }
        if (devices.back() == ',') devices.pop_back();
        if (sources.back() == ',') sources.pop_back();
        devices += "]";
        sources += "]";

        std::string presets = "\"presets\":[", users = "\"userPresets\":[";
        for (const Preset& p : app.builtIns()) presets += quoted(p.name) + ",";
        for (const Preset& p : app.userPresets()) users += quoted(p.name) + ",";
        if (presets.back() == ',') presets.pop_back();
        if (users.back() == ',') users.pop_back();
        presets += "]";
        users += "]";

        // The "automatic" choices are empty ids (System Default Output / All apps).
        const bool failed = st.state == AudioRouter::State::Failed;
        return "{\"route\":{\"state\":\"" + state + "\",\"text\":" + quoted(text) +
               ",\"retry\":" + (failed ? "true" : "false") +
               ",\"settings\":" + (failed || settings ? "true" : "false") + "}," + devices + "," + sources +
               ",\"output\":" + quoted(toUtf8(app.outputChoice())) + ",\"source\":" + quoted(toUtf8(app.sourceChoice())) +
               ",\"allApps\":" + (app.allAppsAvailable() ? "true" : "false") + "," + presets + "," + users +
               ",\"preset\":" + quoted(app.presetName()) + ",\"presetIsUser\":" + (app.presetIsUser() ? "true" : "false") +
               ",\"modified\":" + (app.presetModified() ? "true" : "false") +
               ",\"appsInMix\":" + std::to_string(app.capturingAllApps() ? app.appsInMix() : 0) +
               ",\"closeToTray\":" + (app.closeToTray ? "true" : "false") +
               ",\"startWithWindows\":" + (app.startWithWindows() ? "true" : "false") +
               ",\"error\":" + quoted(app.lastError) + "}";
    }

    void tick() {
        if (!webview || !ready) return;
        const auto st = app.routeStatus();
        const std::string info = "{\"sampleRate\":" + number(st.sampleRate > 0 ? st.sampleRate : app.sampleRate()) +
                                 ",\"latencyMs\":" + number(st.latencyMs) + ",\"version\":\"" SQ_VERSION_STRING "\"}";
        if (info != sentInfo) {
            sentInfo = info;
            webview->evaluateJavascript("window.sqInfo(" + info + ")");
        }
        const std::string state = appState();
        if (state != sentApp) {
            sentApp = state;
            webview->evaluateJavascript("window.sqApp(" + state + ")");
        }

        // Settings that changed since the last update (presets, imports, the tray menu, the page itself).
        sent.resize(ids.size(), std::nan(""));
        std::string js;
        for (size_t i = 0; i < ids.size(); ++i) {
            const double v = getParam(app, ids[i]);
            if (v == sent[i]) continue;
            sent[i] = v;
            js += (js.empty() ? "" : ",") + ("\"" + std::to_string(ids[i]) + "\":" + number(v));
        }
        if (!js.empty()) webview->evaluateJavascript("window.sqParams({" + js + "})");

        const float* bands = app.spectrum();
        const sq_meters& m = app.meters();
        std::string frame = "window.sqFrame({bands:[";
        for (int i = 0; i < App::kSpectrumBands; ++i) frame += (i ? "," : "") + number(bands[i]);
        frame += "],level:" + number(app.level()) + ",peakL:" + number(m.peakL) + ",peakR:" + number(m.peakR) +
                 ",lim:" + number(m.limiterReductionDb) + ",lev:" + number(m.levelerGainDb) + "})";
        webview->evaluateJavascript(frame);
    }
};

WebUi::WebUi(App& app, void* parentHwnd) : impl_(std::make_unique<Impl>(app, parentHwnd)) {}
WebUi::~WebUi() = default;

void WebUi::resize(int width, int height) {
    if (impl_->webview) resizeChildView(impl_->webview->getViewHandle(), width, height);
}

void WebUi::tick() { impl_->tick(); }

} // namespace sq::app
